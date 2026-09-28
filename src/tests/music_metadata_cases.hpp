#pragma once
#include "../features/music.hpp"
#include <fstream>
#include <algorithm>
#include <stdexcept>

inline void TestMetadata(const std::filesystem::path& path) {
    using Bytes = std::vector<unsigned char>;
    const auto appendNumber = [](Bytes& data, size_t number, int count, bool synchsafe) {
        const int bits = synchsafe ? 7 : 8;
        for (int i = count - 1; i >= 0; --i)
            data.push_back(static_cast<unsigned char>((number >> (i * bits)) & (synchsafe ? 127 : 255)));
    };
    const auto write = [&](const Bytes& data) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    };
    const auto check = [](bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); };
    for (int version : {2, 3, 4}) {
        Bytes frame;
        const std::string id = version == 2 ? "TT2" : "TIT2";
        frame.insert(frame.end(), id.begin(), id.end());
        const Bytes text = version == 4 ? Bytes{3, 'C', 'a', 'f', 0xc3, 0xa9} :
            Bytes{1, 0xff, 0xfe, 'C', 0, 'a', 0, 'f', 0, 0xe9, 0, 0, 0};
        appendNumber(frame, text.size(), version == 2 ? 3 : 4, version == 4);
        if (version != 2) { frame.push_back(0); frame.push_back(0); }
        frame.insert(frame.end(), text.begin(), text.end());
        Bytes tag{'I', 'D', '3', static_cast<unsigned char>(version), 0, 0};
        appendNumber(tag, frame.size(), 4, true);
        tag.insert(tag.end(), frame.begin(), frame.end());
        write(tag);
        check(Music::ReadMetadata(path).title == "Caf\xc3\xa9", "ID3 version and Unicode title");
        if (version == 3) {
            // Whole-tag unsynchronisation, including the UTF-16 BOM.
            Bytes escaped;
            for (size_t i = 0; i < frame.size(); ++i) {
                escaped.push_back(frame[i]);
                if (frame[i] == 0xff) escaped.push_back(0);
            }
            tag = {'I', 'D', '3', 3, 0, 0x80};
            appendNumber(tag, escaped.size(), 4, true);
            tag.insert(tag.end(), escaped.begin(), escaped.end());
            write(tag);
            check(Music::ReadMetadata(path).title == "Caf\xc3\xa9", "ID3v2.3 unsynchronisation");
        }
        if (version == 4) {
            Bytes extension{0, 0, 0, 6, 1, 0};
            extension.insert(extension.end(), frame.begin(), frame.end());
            tag = {'I', 'D', '3', 4, 0, 0x40};
            appendNumber(tag, extension.size(), 4, true);
            tag.insert(tag.end(), extension.begin(), extension.end());
            write(tag);
            check(Music::ReadMetadata(path).title == "Caf\xc3\xa9", "ID3v2.4 extended header");
        }
    }
    Bytes v1(128, 0);
    v1[0] = 'T'; v1[1] = 'A'; v1[2] = 'G';
    const std::string title = "Legacy title";
    std::copy(title.begin(), title.end(), v1.begin() + 3);
    v1[126] = 7;
    write(v1);
    auto metadata = Music::ReadMetadata(path);
    check(metadata.title == title && metadata.trackNumber == "7", "ID3v1 fallback");
    write({'I', 'D', '3', 4, 0, 0, 0x7f, 0x7f, 0x7f, 0x7f});
    check(Music::ReadMetadata(path).title == path.stem().u8string(), "Oversized/truncated tag fallback");
    write({'I', 'D', '3', 3, 0, 0, 0, 0, 0, 10, 'T', 'I', 'T', '2', 0x7f, 0xff, 0xff, 0xff, 0, 0});
    check(Music::ReadMetadata(path).title == path.stem().u8string(), "Invalid frame bounds");
    std::filesystem::remove(path);
}
