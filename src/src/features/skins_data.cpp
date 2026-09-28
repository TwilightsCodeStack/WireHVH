#include <w1re/features/skins.hpp>
#include <third_party/nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Skins {
using Json = nlohmann::json;
bool IsKnife(int d) {
    static const int ids[]={42,59,500,503,505,506,507,508,509,512,514,515,516,517,518,519,520,521,522,523,525,526};
    return std::find(std::begin(ids),std::end(ids),d)!=std::end(ids);
}
bool IsGlove(int d) { return d == 4725 || (d >= 5027 && d <= 5035); }
void Validate(const Preset& p) {
    static const int weapons[] = {1,2,3,4,7,8,9,10,11,13,14,16,17,19,23,24,25,26,27,28,29,30,31,32,33,34,35,36,38,39,40,60,61,63,64};
    if (!IsKnife(p.definition) && !IsGlove(p.definition) &&
        std::find(std::begin(weapons), std::end(weapons), p.definition) == std::end(weapons))
        throw std::runtime_error("Choose a weapon, knife, or glove item definition.");
    if (p.paint < 0 || p.paint > 100000 || p.seed < 0 || p.seed > 1000 ||
        !std::isfinite(p.wear) || p.wear < 0 || p.wear > 1)
        throw std::runtime_error("Paint must be 0-100000, pattern 0-1000, and wear 0-1.");
    if (p.name.empty() || p.name.size() > 160 || p.name.find('\0') != std::string::npos)
        throw std::runtime_error("Preset name must contain 1-160 bytes of text.");
    for (const auto& s : p.stickers) {
        if (s.id < 0 || s.id > 100000 || !std::isfinite(s.wear) || s.wear < 0 || s.wear > 1 ||
            !std::isfinite(s.scale) || s.scale <= 0 || s.scale > 5 ||
            !std::isfinite(s.rotation) || std::abs(s.rotation) > 360 ||
            !std::isfinite(s.x) || !std::isfinite(s.y) || std::abs(s.x) > 10 || std::abs(s.y) > 10)
            throw std::runtime_error("Invalid sticker ID, wear, scale, rotation, or placement.");
        if (s.id && (IsKnife(p.definition) || IsGlove(p.definition)))
            throw std::runtime_error("Stickers can only be applied to guns.");
    }
}
std::string ListingId(const std::string& input) {
    auto id = input;
    const auto start = id.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) throw std::runtime_error("Paste a CSFloat listing URL or numeric listing ID.");
    id = id.substr(start, id.find_last_not_of(" \t\r\n") - start + 1);
    const std::string prefix = "https://csfloat.com/item/";
    if (id.compare(0, prefix.size(), prefix) == 0) {
        id.erase(0, prefix.size());
        id = id.substr(0, id.find_first_of("?#"));
        if (!id.empty() && id.back() == '/') id.pop_back();
    }
    if (id.empty() || id.size() > 20 || id.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Use https://csfloat.com/item/<listing ID> or the numeric listing ID.");
    return id;
}
static int Integer(const Json& value) {
    if (!value.is_number_integer()) throw std::runtime_error("Expected a whole-number item field.");
    const auto number = value.get<double>();
    if (number < 0 || number > 100000) throw std::runtime_error("Item field is outside its supported range.");
    return static_cast<int>(number);
}
static float Number(const Json& value) {
    if (!value.is_number()) throw std::runtime_error("Expected a numeric item field.");
    const float result = value.get<float>();
    if (!std::isfinite(result)) throw std::runtime_error("Item values must be finite.");
    return result;
}
static Preset ReadItem(const Json& root) {
    if (!root.is_object()) throw std::runtime_error("Expected one listing or item object.");
    const auto& item = root.contains("item") ? root.at("item") : root;
    Preset p;
    p.definition = Integer(item.at("def_index"));
    p.paint = Integer(item.at("paint_index"));
    p.seed = Integer(item.at("paint_seed"));
    p.wear = Number(item.at("float_value"));
    p.name = item.value("market_hash_name", item.value("item_name", std::string("Imported finish")));
    if (root.contains("item") && root.contains("id")) p.listing = ListingId(root.at("id").get<std::string>());
    if (item.contains("stickers")) {
        const auto& stickers = item.at("stickers");
        if (!stickers.is_array() || stickers.size() > 5) throw std::runtime_error("Expected up to five stickers.");
        bool used[5]{};
        for (const auto& entry : stickers) {
            const int slot = Integer(entry.at("slot"));
            if (slot > 4 || used[slot]) throw std::runtime_error("Sticker slots must be unique and between 0 and 4.");
            used[slot] = true;
            auto& s = p.stickers[slot];
            s.id = Integer(entry.at("sticker_id"));
            s.wear = Number(entry.value("wear", Json(0.0)));
            s.scale = Number(entry.value("scale", Json(1.0)));
            s.rotation = Number(entry.value("rotation", Json(0.0)));
            s.x = Number(entry.value("offset_x", Json(0.0)));
            s.y = Number(entry.value("offset_y", Json(0.0)));
        }
    }
    Validate(p);
    return p;
}
Preset ParseListing(const std::string& text) {
    if (text.size() > 1024 * 1024) throw std::runtime_error("Listing JSON exceeds 1 MB.");
    return ReadItem(Json::parse(text));
}
std::string Serialize(const std::vector<Preset>& presets) {
    if (presets.size() > 64) throw std::runtime_error("The preset library is limited to 64 items.");
    Json items = Json::array();
    for (const auto& p : presets) {
        Validate(p);
        Json item{{"def_index",p.definition},{"paint_index",p.paint},{"paint_seed",p.seed},
                  {"float_value",p.wear},{"market_hash_name",p.name},{"stickers",Json::array()}};
        for (int i=0; i<5; ++i) {
            const auto& s=p.stickers[i];
            if (s.id) item["stickers"].push_back({{"slot",i},{"sticker_id",s.id},{"wear",s.wear},
                {"scale",s.scale},{"rotation",s.rotation},{"offset_x",s.x},{"offset_y",s.y}});
        }
        Json entry{{"item",item}};
        if (!p.listing.empty()) entry["id"] = ListingId(p.listing);
        items.push_back(entry);
    }
    return Json{{"version",1},{"presets",items}}.dump(2);
}
std::vector<Preset> Deserialize(const std::string& text) {
    if (text.size() > 1024*1024) throw std::runtime_error("Preset library exceeds 1 MB.");
    const auto root=Json::parse(text);
    if (root.at("version") != 1 || !root.at("presets").is_array() || root.at("presets").size()>64)
        throw std::runtime_error("Unsupported preset library.");
    std::vector<Preset> result;
    for (const auto& item:root.at("presets")) result.push_back(ReadItem(item));
    return result;
}
}
