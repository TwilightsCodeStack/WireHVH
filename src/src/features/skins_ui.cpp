#include <w1re/features/skins.hpp>
#include <w1re/features/skins_game.hpp>
#include <imgui.h>
#include <windows.h>
#include <future>
#include <fstream>
#include <chrono>
#include <cstring>
#include <stdexcept>

extern HMODULE g_hModule;
namespace Skins {
namespace {
Preset draft;
std::vector<Preset> library;
std::future<Preset> download;
char listing[512]{}, jsonText[16384]{}, name[161]="Custom finish";
std::string message;
bool loaded=false;
int selected=-1;
std::filesystem::path LibraryPath() {
    wchar_t path[32768]{};
    if(!GetModuleFileNameW(g_hModule,path,32768)) throw std::runtime_error("Cannot locate the preset folder.");
    return std::filesystem::path(path).parent_path()/L"resources"/L"skins"/L"presets.json";
}
void Select(const Preset& p) {draft=p;strncpy_s(name,p.name.c_str(),_TRUNCATE);}
void Load() {
    const auto path=LibraryPath();
    if(!std::filesystem::exists(path)) return;
    if(std::filesystem::file_size(path)>1024*1024) throw std::runtime_error("Preset library exceeds 1 MB.");
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error("Cannot read the preset library.");
    std::string text((std::istreambuf_iterator<char>(file)),{});
    library=Deserialize(text);
}
void Save() {
    const auto text=Serialize(library);
    const auto path=LibraryPath();
    std::filesystem::create_directories(path.parent_path());
    const auto temp=path.wstring()+L".tmp";
    {std::ofstream file(temp,std::ios::binary|std::ios::trunc); file<<text;file.flush();
     if(!file) throw std::runtime_error("Could not save the preset library.");}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not replace the preset library.");
}
template<class Action> void Attempt(Action action) {
    try {action();} catch(const std::exception& e) {message=e.what();}
}
}
void RenderUI() {
    if(!loaded){loaded=true;Attempt([]{Load();});}
    if(download.valid() && download.wait_for(std::chrono::seconds(0))==std::future_status::ready)
        Attempt([]{Select(download.get());selected=-1;message="Listing imported. Review the values, then enable the preset.";});
    ImGui::TextColored(ImVec4(0.74f,0.58f,0.94f,1),"CLIENT SKINS");
    ImGui::TextWrapped("Local cosmetics for weapons, knives, gloves, and five sticker slots. Presets stay enabled across respawns and server changes for this game session.");
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##csfloat","CSFloat listing URL or ID",listing,sizeof(listing));
    ImGui::BeginDisabled(download.valid());
    if(ImGui::Button("Import CSFloat listing")) Attempt([]{
        const auto id=ListingId(listing);
        download=std::async(std::launch::async,[id]{return FetchListing(id);});message="Reading listing...";
    });
    ImGui::EndDisabled();
    if(ImGui::CollapsingHeader("Import listing JSON")) {
        ImGui::InputTextMultiline("##listing-json",jsonText,sizeof(jsonText),ImVec2(-1,100));
        if(ImGui::Button("Read JSON")) Attempt([]{Select(ParseListing(jsonText));selected=-1;message="Listing JSON imported.";});
    }
    if(ImGui::BeginCombo("Saved presets",selected>=0 && selected<static_cast<int>(library.size()) ? library[selected].name.c_str() : "Select a preset")) {
        for(int i=0;i<static_cast<int>(library.size());++i) {
            ImGui::PushID(i);
            if(ImGui::Selectable(library[i].name.c_str(),i==selected)){selected=i;Select(library[i]);}
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::Separator();
    ImGui::InputText("Preset name",name,sizeof(name));
    ImGui::InputInt("Item definition",&draft.definition);
    ImGui::TextDisabled("Type: %s",IsGlove(draft.definition)?"Gloves":IsKnife(draft.definition)?"Knife":"Weapon");
    ImGui::InputInt("Paint kit",&draft.paint);
    ImGui::InputInt("Pattern seed",&draft.seed);
    ImGui::InputFloat("Wear",&draft.wear,0.001f,0.01f,"%.8f");
    if(ImGui::SmallButton("Clear all stickers")) draft.stickers={};
    if(IsKnife(draft.definition)) ImGui::TextWrapped("Applies to your equipped knife using the client model update. Enabling another knife replaces the current knife preset.");
    if(!IsKnife(draft.definition) && !IsGlove(draft.definition) && ImGui::CollapsingHeader("Stickers (five slots)")) {
        for(int i=0;i<5;++i) {
            ImGui::PushID(i);
            const auto label="Slot "+std::to_string(i+1);
            if(ImGui::TreeNode(label.c_str())) {
                auto& s=draft.stickers[i];
                ImGui::InputInt("Sticker ID (0 clears)",&s.id);
                ImGui::SliderFloat("Sticker wear",&s.wear,0,1);
                ImGui::SliderFloat("Scale",&s.scale,0.1f,5);
                ImGui::SliderFloat("Rotation",&s.rotation,-360,360);
                ImGui::InputFloat("Offset X",&s.x,0.01f);
                ImGui::InputFloat("Offset Y",&s.y,0.01f);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    if(ImGui::Button("Save as new preset")) Attempt([]{
        draft.name=name;Validate(draft);
        if(library.size()>=64) throw std::runtime_error("The library is limited to 64 presets.");
        auto previous=library;library.push_back(draft);
        try{Save();}catch(...){library=std::move(previous);throw;}
        selected=static_cast<int>(library.size())-1;message="Preset saved.";
    });
    ImGui::SameLine();
    ImGui::BeginDisabled(selected<0);
    if(ImGui::Button("Delete selected")) Attempt([]{
        auto previous=library;library.erase(library.begin()+selected);
        try{Save();}catch(...){library=std::move(previous);throw;}
        selected=-1;message="Preset deleted.";
    });
    ImGui::EndDisabled();
    ImGui::Separator();
    if(ImGui::Button("Enable preset")) Attempt([]{
        draft.name=name;Validate(draft);
        std::string error;
        if(!QueueApply(draft,error)) throw std::runtime_error(error);
        message="Preset enabled. It will apply whenever you equip a matching item.";
    });
    ImGui::SameLine();
    if(ImGui::Button("Stop overrides")) {ClearAppliedPresets();message.clear();}
    ImGui::TextDisabled("Enabled item presets: %zu",AppliedPresetCount());
    ImGui::TextWrapped("%s",GameStatus().c_str());
    if(!message.empty()) ImGui::TextWrapped("%s",message.c_str());
    ImGui::TextWrapped("Changes are visible on your client. Stopping overrides prevents further updates; reconnect to restore original appearances.");
}
void Shutdown() {
    ShutdownGame();
    if(download.valid()) {try{download.get();}catch(...) {}}
}
}
