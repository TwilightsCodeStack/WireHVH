#include <w1re/features/music.hpp>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <fstream>

namespace Music {
namespace {
    using Bytes = std::vector<unsigned char>;
    using Microsoft::WRL::ComPtr;
    constexpr size_t MaxTagBytes = 32 * 1024 * 1024;

    uint32_t Number(const unsigned char* p, size_t n, bool synchsafe = false) {
        uint32_t value = 0;
        for (size_t i = 0; i < n; ++i) {
            if (synchsafe && (p[i] & 0x80)) return UINT32_MAX;
            value = (value << (synchsafe ? 7 : 8)) | p[i];
        }
        return value;
    }

    Bytes Unsync(const Bytes& input) {
        Bytes output;
        output.reserve(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            output.push_back(input[i]);
            if (input[i] == 0xff && i + 1 < input.size() && input[i + 1] == 0)
                ++i;
        }
        return output;
    }

    std::string Utf8(const std::wstring& wide) {
        if (wide.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string text(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), length, nullptr, nullptr);
        return text;
    }

    std::string Text(const unsigned char* data, size_t size, unsigned char encoding) {
        size = std::min<size_t>(size, 4096);
        std::wstring wide;
        if (encoding == 0) {
            for (size_t i = 0; i < size && data[i]; ++i) wide.push_back(data[i]);
        } else if (encoding == 1 || encoding == 2) {
            bool bigEndian = encoding == 2;
            size_t start = 0;
            if (size >= 2 && ((data[0] == 0xff && data[1] == 0xfe) || (data[0] == 0xfe && data[1] == 0xff))) {
                bigEndian = data[0] == 0xfe;
                start = 2;
            }
            for (size_t i = start; i + 1 < size; i += 2) {
                const wchar_t c = static_cast<wchar_t>(bigEndian ? (data[i] << 8) | data[i + 1] : data[i] | (data[i + 1] << 8));
                if (!c) break;
                wide.push_back(c);
            }
        } else if (encoding == 3) {
            size_t length = 0;
            while (length < size && data[length]) ++length;
            const int count = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(data), static_cast<int>(length), nullptr, 0);
            wide.resize(count);
            MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(data), static_cast<int>(length), wide.data(), count);
        }
        for (auto& c : wide) if (c < 32) c = L' ';
        while (!wide.empty() && wide.back() == L' ') wide.pop_back();
        return Utf8(wide);
    }

    std::shared_ptr<const CoverImage> DecodeCover(const unsigned char* bytes, size_t size) {
        if (!size || size > 16 * 1024 * 1024) return {};
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICBitmapScaler> scaler;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
            FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), static_cast<DWORD>(size))) ||
            FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
            FAILED(decoder->GetFrame(0, &frame))) return {};
        UINT width = 0, height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || !width || !height ||
            width > 16384 || height > 16384 || static_cast<uint64_t>(width) * height > 64000000) return {};
        const double scale = std::min(1.0, 512.0 / std::max(width, height));
        width = std::max(1u, static_cast<UINT>(width * scale));
        height = std::max(1u, static_cast<UINT>(height * scale));
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(frame.Get(), width, height, WICBitmapInterpolationModeFant)) ||
            FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                        nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
        auto image = std::make_shared<CoverImage>();
        image->width = width;
        image->height = height;
        image->rgba.resize(static_cast<size_t>(width) * height * 4);
        if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(image->rgba.size()), image->rgba.data()))) return {};
        return image;
    }

    void Picture(const Bytes& body, bool v22, TrackMetadata& metadata, bool& frontCover) {
        if (body.size() < 5) return;
        const auto encoding = body[0];
        if (encoding > 3) return;
        size_t pos = 1;
        if (v22) pos += 3; // PIC uses a three-byte format in place of a MIME string.
        else {
            while (pos < body.size() && body[pos]) ++pos;
            ++pos;
        }
        if (pos >= body.size()) return;
        const unsigned char type = body[pos++];
        const size_t step = (encoding == 1 || encoding == 2) ? 2 : 1;
        bool terminated = false;
        while (pos + step <= body.size()) {
            const bool end = body[pos] == 0 && (step == 1 || body[pos + 1] == 0);
            pos += step;
            if (end) { terminated = true; break; }
        }
        if (!terminated || pos >= body.size() || (metadata.cover && (frontCover || type != 3))) return;
        if (auto cover = DecodeCover(body.data() + pos, body.size() - pos)) {
            metadata.cover = std::move(cover);
            frontCover = type == 3;
        }
    }

    void ParseTag(Bytes data, int version, unsigned char flags, TrackMetadata& metadata) {
        if (version < 2 || version > 4 || (version == 2 && (flags & 0x40))) return;
        if (version < 4 && (flags & 0x80)) data = Unsync(data);
        size_t pos = 0;
        if (version >= 3 && (flags & 0x40)) {
            if (data.size() < 4) return;
            const auto length = Number(data.data(), 4, version == 4);
            if (length > data.size() || (version == 4 && length < 6)) return;
            pos = length + (version == 3 ? 4 : 0);
        }
        const size_t header = version == 2 ? 6 : 10;
        bool frontCover = false;
        while (pos <= data.size() && data.size() - pos >= header) {
            const size_t idSize = version == 2 ? 3 : 4;
            bool validId = true;
            for (size_t i = 0; i < idSize; ++i)
                if (!((data[pos + i] >= 'A' && data[pos + i] <= 'Z') || (data[pos + i] >= '0' && data[pos + i] <= '9')))
                    validId = false;
            if (!validId) break;
            const std::string id(reinterpret_cast<const char*>(data.data() + pos), idSize);
            const size_t length = Number(data.data() + pos + idSize, version == 2 ? 3 : 4, version == 4);
            const unsigned char format = version == 2 ? 0 : data[pos + 9];
            pos += header;
            if (length > data.size() - pos) break;
            Bytes body(data.begin() + pos, data.begin() + pos + length);
            pos += length;
            // Unsupported compression/encryption is skipped without blocking other frames.
            if ((version == 3 && (format & 0xc0)) || (version == 4 && (format & 0x0c))) continue;
            if (version == 4 && ((flags & 0x80) || (format & 0x02))) body = Unsync(body);
            size_t prefix = ((version == 3 && (format & 0x20)) || (version == 4 && (format & 0x40))) ? 1 : 0;
            if (version == 4 && (format & 0x01)) prefix += 4;
            if (prefix >= body.size()) continue;
            body.erase(body.begin(), body.begin() + prefix);
            if (id == "APIC" || id == "PIC") { Picture(body, version == 2, metadata, frontCover); continue; }
            std::string* target = nullptr;
            if (id == "TIT2" || id == "TT2") target = &metadata.title;
            else if (id == "TPE1" || id == "TP1") target = &metadata.artist;
            else if (id == "TALB" || id == "TAL") target = &metadata.album;
            else if (id == "TDRC" || id == "TYER" || id == "TYE") target = &metadata.year;
            else if (id == "TRCK" || id == "TRK") target = &metadata.trackNumber;
            if (target) *target = Text(body.data() + 1, body.size() - 1, body[0]);
        }
    }
}

    TrackMetadata ReadMetadata(const std::filesystem::path& path) {
        TrackMetadata metadata;
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            const auto fileSize = file ? static_cast<std::streamoff>(file.tellg()) : 0;
            if (fileSize >= 10) {
                unsigned char header[10] = {};
                file.seekg(0);
                file.read(reinterpret_cast<char*>(header), 10);
                if (header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
                    const auto length = Number(header + 6, 4, true);
                    if (length <= MaxTagBytes && length <= static_cast<uint64_t>(fileSize - 10)) {
                        Bytes data(length);
                        if (file.read(reinterpret_cast<char*>(data.data()), length))
                            ParseTag(std::move(data), header[3], header[5], metadata);
                    }
                }
            }
            if (fileSize >= 128) {
                unsigned char tag[128] = {};
                file.clear();
                file.seekg(-128, std::ios::end);
                if (file.read(reinterpret_cast<char*>(tag), 128) && tag[0] == 'T' && tag[1] == 'A' && tag[2] == 'G') {
                    if (metadata.title.empty()) metadata.title = Text(tag + 3, 30, 0);
                    if (metadata.artist.empty()) metadata.artist = Text(tag + 33, 30, 0);
                    if (metadata.album.empty()) metadata.album = Text(tag + 63, 30, 0);
                    if (metadata.year.empty()) metadata.year = Text(tag + 93, 4, 0);
                    if (metadata.trackNumber.empty() && tag[125] == 0 && tag[126]) metadata.trackNumber = std::to_string(tag[126]);
                }
            }
        } catch (const std::exception&) {
            // A malformed tag is optional data, not a playback failure.
        }
        if (metadata.title.empty()) metadata.title = path.stem().u8string();
        if (SUCCEEDED(com)) CoUninitialize();
        return metadata;
    }
}
