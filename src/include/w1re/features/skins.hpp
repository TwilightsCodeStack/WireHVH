#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace Skins {
struct Sticker {
    int id = 0;
    float wear = 0, scale = 1, rotation = 0, x = 0, y = 0;
};
struct Preset {
    std::string name = "Custom finish", listing;
    int definition = 7, paint = 0, seed = 0;
    float wear = 0.01f;
    std::array<Sticker, 5> stickers{};
};
bool IsKnife(int definition);
bool IsGlove(int definition);
void Validate(const Preset& preset);
std::string ListingId(const std::string& input);
Preset ParseListing(const std::string& text);
std::string Serialize(const std::vector<Preset>& presets);
std::vector<Preset> Deserialize(const std::string& text);
Preset FetchListing(const std::string& input);
void RenderUI();
void Shutdown();
}
