#pragma once
#include <filesystem>
#include <fstream>
#include <w1re/offset_patch.hpp>
#include <third_party/nlohmann/json.hpp>

namespace OffsetPatch {
inline nlohmann::json ReadJson(const std::filesystem::path& file) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size == 0 || size > 32 * 1024 * 1024)
        throw std::runtime_error("Missing, empty or oversized dump: " + file.filename().string());
    std::ifstream stream(file, std::ios::binary);
    return nlohmann::json::parse(stream);
}
inline std::int64_t Number(const nlohmann::json& value) {
    if (!value.is_number_integer() || value < 0 || value > 0x7fffffff)
        throw std::runtime_error("Dumper returned a non-integer or out-of-range offset.");
    return value.get<std::int64_t>();
}
inline Packet ParseDump(const std::filesystem::path& directory) {
    const auto globals = ReadJson(directory / L"offsets.json");
    const auto buttons = ReadJson(directory / L"buttons.json").at("client.dll");
    const auto classes = ReadJson(directory / L"client_dll.json").at("client.dll").at("classes");
    Packet packet;
    packet.build = static_cast<DWORD>(Number(ReadJson(directory / L"info.json").at("build_number")));
    packet.buildOffset = static_cast<DWORD>(Number(globals.at("engine2.dll").at("dwBuildNumber")));
    for (size_t i = 0; i < std::size(Fields); ++i) {
        const auto& field = Fields[i];
        try {
            switch (field.source) {
            case Source::Global: case Source::Relative:
                packet.values[i] = Number(globals.at("client.dll").at(field.key)); break;
            case Source::Button:
                packet.values[i] = Number(buttons.at(field.key)); break;
            case Source::Field: case Source::Glow:
                packet.values[i] = Number(classes.at(field.type).at("fields").at(field.key));
                if (field.source == Source::Glow)
                    packet.values[i] += Number(classes.at("C_BaseModelEntity").at("fields").at("m_Glow"));
                break;
            }
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("Cannot patch ") + field.name + ": " + error.what());
        }
    }
    return packet;
}
}
