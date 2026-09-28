#include <w1re/features/skins_game.hpp>
#include <w1re/features/skins_profile.hpp>
#include <w1re/offsets.hpp>
#include <MinHook.h>
#include <windows.h>
#include <atomic>
#include <map>
#include <mutex>
#include <cstring>
#include <stdexcept>

namespace Skins {
namespace {
using Address = uintptr_t;
using Frame = void(__fastcall*)(void*, int);
using SetAttribute = void(__fastcall*)(void*, const char*, float);
using SubclassChanged = void(__fastcall*)(void*);
using Regenerate = void(__fastcall*)();
std::mutex stateMutex;
struct Selection { Preset preset; uint64_t revision; };
struct Applied { Address entity=0; uint64_t revision=0, attributes=0; };
std::map<int, Selection> selections;
std::map<uint32_t, Applied> applied;
Applied gloveApplied;
uint64_t revision = 0;
Address clientBase = 0, lastPawn = 0, lastSystem = 0;
std::string status = "Select a preset and enable it for your local client.";
Frame originalFrame = nullptr;
void* frameTarget = nullptr;
SetAttribute setAttribute = nullptr;
SubclassChanged subclassChanged = nullptr;
Regenerate regenerate = nullptr;
std::atomic<unsigned> callbacks{0};
bool stopping = false, faulted = false;

// Separate slots for guns, with one shared knife slot and one shared glove slot.
int Slot(int definition) { return IsKnife(definition) ? -1 : IsGlove(definition) ? -2 : definition; }
template<class T> T& Field(Address object, Address offset) { return *reinterpret_cast<T*>(object+offset); }

Address Entity(Address system, uint32_t handle) {
    if(!system || handle==0xffffffffu || handle==0xfffffffeu) return 0;
    const uint32_t index=handle & 0x7fffu;
    const Address page=Field<Address>(system,0x10+8*(index>>9));
    if(!page) return 0;
    const Address identity=page+0x70*(index&0x1ffu);
    // Checking the serial prevents touching a different entity that reused this slot.
    if(Field<uint32_t>(identity,0x10)!=handle) return 0;
    return Field<Address>(identity,0);
}

// CUtlStringToken uses MurmurHash2 with this seed for numeric weapon subclass names.
uint32_t SubclassToken(int definition) {
    const std::string name=std::to_string(definition);
    uint32_t hash=0x31415926u ^ static_cast<uint32_t>(name.size());
    const auto* data=reinterpret_cast<const unsigned char*>(name.data());
    size_t remaining=name.size();
    while(remaining>=4) {
        uint32_t value; std::memcpy(&value,data,4);
        value*=0x5bd1e995u; value^=value>>24; value*=0x5bd1e995u;
        hash=(hash*0x5bd1e995u)^value; data+=4; remaining-=4;
    }
    if(remaining==3) hash^=static_cast<uint32_t>(data[2])<<16;
    if(remaining>=2) hash^=static_cast<uint32_t>(data[1])<<8;
    if(remaining>=1) {hash^=data[0];hash*=0x5bd1e995u;}
    hash^=hash>>13; hash*=0x5bd1e995u; return hash^(hash>>15);
}

uint64_t AttributeFingerprint(Address item) {
    // CEconItemAttribute layout is part of the reviewed native client profile.
    uint64_t hash=14695981039346656037ull;
    for(const auto offset : {Offsets::m_AttributeList,Offsets::m_NetworkedDynamicAttributes}) {
        const Address list=item+offset;
        const int count=Field<int>(list,8);
        const Address data=Field<Address>(list,16);
        if(count<0 || count>128 || (count && !data))
            throw std::runtime_error("Invalid client item attribute list; cosmetic updates stopped.");
        hash=(hash^static_cast<uint64_t>(count))*1099511628211ull;
        for(int i=0;i<count;++i) {
            const Address attr=data+0x48*i;
            hash=(hash^Field<uint16_t>(attr,0x30))*1099511628211ull;
            hash=(hash^Field<uint32_t>(attr,0x34))*1099511628211ull;
        }
    }
    return hash;
}
void WriteAttributes(Address item, const Preset& p) {
    // Call the item-view setter: it owns allocation and invalidates item caches.
    // The server implementation took an attribute-list pointer instead.
    const auto set=[&](const std::string& name,float value) {
        setAttribute(reinterpret_cast<void*>(item),name.c_str(),value);
    };
    set("set item texture prefab",static_cast<float>(p.paint));
    set("set item texture seed",static_cast<float>(p.seed));
    set("set item texture wear",p.wear);
    if(!IsKnife(p.definition) && !IsGlove(p.definition)) {
        for(int i=0;i<5;++i) {
            const auto& s=p.stickers[i];
            const std::string prefix="sticker slot "+std::to_string(i);
            uint32_t id=static_cast<uint32_t>(s.id); float encoded;
            std::memcpy(&encoded,&id,sizeof(id));
            set(prefix+" id",encoded);
            set(prefix+" wear",s.wear); set(prefix+" scale",s.scale);
            set(prefix+" rotation",s.rotation);
            set(prefix+" offset x",s.x); set(prefix+" offset y",s.y);
        }
    }
    // Use local attribute data instead of looking up a real inventory item.
    Field<uint64_t>(item,Offsets::m_iItemID)=UINT64_MAX;
    Field<uint32_t>(item,Offsets::m_iItemIDHigh)=UINT32_MAX;
    Field<uint32_t>(item,Offsets::m_iItemIDLow)=UINT32_MAX;
    Field<bool>(item,Offsets::m_bDisallowSOC)=true;
    Field<bool>(item,Offsets::m_bInitialized)=true;
}
bool NeedsApply(const Applied& record, Address entity, Address item, const Selection& selection) {
    return record.entity!=entity || record.revision!=selection.revision ||
        Field<uint16_t>(item,Offsets::m_iItemDefinitionIndex)!=selection.preset.definition ||
        Field<uint32_t>(item,Offsets::m_iItemIDHigh)!=UINT32_MAX ||
        !Field<bool>(item,Offsets::m_bDisallowSOC) || !Field<bool>(item,Offsets::m_bInitialized) ||
        record.attributes!=AttributeFingerprint(item);
}
void ApplyFrame() {
    if(selections.empty()) return;
    const Address system=Field<Address>(clientBase,Offsets::dwEntityList);
    const Address controller=Field<Address>(clientBase,Offsets::dwLocalPlayerController);
    const Address pawn=controller ? Entity(system,Field<uint32_t>(controller,Offsets::m_hPlayerPawn)) : 0;
    if(lastPawn!=pawn || lastSystem!=system) {
        applied.clear();gloveApplied={};lastPawn=pawn;lastSystem=system;
    }
    if(!pawn || Field<int>(pawn,Offsets::m_iHealth)<=0) {
        status="Presets enabled. Waiting for your player to spawn.";
        return;
    }
    bool changed=false;
    size_t matched=0;
    const auto gloves=selections.find(-2);
    if(gloves!=selections.end()) {
        const auto& selection=gloves->second;
        const Address item=pawn+Offsets::m_EconGloves;
        if(NeedsApply(gloveApplied,pawn,item,selection)) {
            Field<uint16_t>(item,Offsets::m_iItemDefinitionIndex)=static_cast<uint16_t>(selection.preset.definition);
            Field<int>(item,Offsets::m_iEntityQuality)=3;
            WriteAttributes(item,selection.preset);
            ++Field<uint8_t>(pawn,Offsets::m_nEconGlovesChanged);
            Field<bool>(pawn,Offsets::m_bNeedToReApplyGloves)=true;
            gloveApplied={pawn,selection.revision,AttributeFingerprint(item)};
            changed=true;
        }
        ++matched;
    }
    const Address services=Field<Address>(pawn,Offsets::m_pWeaponServices);
    std::map<uint32_t,Applied> nextApplied;
    if(services) {
        const Address vector=services+Offsets::m_hMyWeapons;
        const int count=Field<int>(vector,0);
        const Address handles=Field<Address>(vector,8);
        if(count<0 || count>64 || (count && !handles))
            throw std::runtime_error("Invalid local weapon list; cosmetic updates stopped.");
        for(int i=0;i<count;++i) {
            const uint32_t handle=Field<uint32_t>(handles,4*i);
            const Address weapon=Entity(system,handle);
            if(!weapon || Entity(system,Field<uint32_t>(weapon,Offsets::m_hOwnerEntity))!=pawn) continue;
            const Address item=weapon+Offsets::m_AttributeManager+Offsets::m_Item;
            const int definition=Field<uint16_t>(item,Offsets::m_iItemDefinitionIndex);
            const auto found=selections.find(Slot(definition));
            if(found==selections.end()) continue;
            const auto& selection=found->second;
            const auto& p=selection.preset;
            Applied record=applied.count(handle) ? applied.at(handle) : Applied{};
            const bool knife=IsKnife(definition);
            const bool subclassMismatch=knife && Field<uint32_t>(weapon,Offsets::m_nSubclassID)!=SubclassToken(p.definition);
            if(NeedsApply(record,weapon,item,selection) || subclassMismatch ||
               Field<int>(weapon,Offsets::m_nFallbackPaintKit)!=p.paint ||
               Field<int>(weapon,Offsets::m_nFallbackSeed)!=p.seed ||
               Field<float>(weapon,Offsets::m_flFallbackWear)!=p.wear) {
                Field<uint16_t>(item,Offsets::m_iItemDefinitionIndex)=static_cast<uint16_t>(p.definition);
                if(knife) {
                    Field<int>(item,Offsets::m_iEntityQuality)=3;
                    if(subclassMismatch || definition!=p.definition) {
                        Field<uint32_t>(weapon,Offsets::m_nSubclassID)=SubclassToken(p.definition);
                        // Native client callback reloads vdata and recreates the HUD model.
                        subclassChanged(reinterpret_cast<void*>(weapon));
                    }
                }
                Field<int>(weapon,Offsets::m_nFallbackPaintKit)=p.paint;
                Field<int>(weapon,Offsets::m_nFallbackSeed)=p.seed;
                Field<float>(weapon,Offsets::m_flFallbackWear)=p.wear;
                WriteAttributes(item,p);
                record={weapon,selection.revision,AttributeFingerprint(item)};
                changed=true;
            }
            nextApplied[handle]=record;
            ++matched;
        }
    }
    applied.swap(nextApplied);
    // Never rebuild materials every frame; refresh only when an item actually changes.
    if(changed) regenerate();
    status=matched ? "Client cosmetics active on "+std::to_string(matched)+" item(s)." :
        "Presets enabled. Equip a matching weapon or knife.";
}
int MemoryFault(unsigned code) {
    return code==EXCEPTION_ACCESS_VIOLATION || code==EXCEPTION_IN_PAGE_ERROR ?
        EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
bool ApplySafe() {
    __try {ApplyFrame();return true;} __except(MemoryFault(GetExceptionCode())) {return false;}
}
void __fastcall OnFrame(void* self,int stage) {
    struct Guard {Guard(){++callbacks;}~Guard(){--callbacks;}} guard;
    originalFrame(self,stage);
    if(stage!=Profile::NetUpdateEnd) return;
    std::lock_guard<std::mutex> lock(stateMutex);
    if(stopping || faulted) return;
    try {
        if(!ApplySafe()) throw std::runtime_error("Client cosmetic memory access failed. Restart the game before retrying.");
    } catch(const std::exception& e) {
        faulted=true; status=e.what();applied.clear();gloveApplied={};
    }
}
void Initialize() {
    if(faulted) throw std::runtime_error("Client cosmetics stopped after a fault. Restart the game before retrying.");
    if(frameTarget) return;
    const auto client=GetModuleHandleW(L"client.dll");
    if(!client) throw std::runtime_error("The game client is not loaded yet.");
    const Address base=reinterpret_cast<Address>(client);
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>4096)
        throw std::runtime_error("Invalid client module header.");
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp!=Profile::ClientStamp ||
       nt->OptionalHeader.SizeOfImage!=Profile::ClientSize)
        throw std::runtime_error("Client cosmetic functions need updating for this game build (profile "+std::to_string(Profile::Build)+").");
    const auto factory=reinterpret_cast<void*(*)(const char*,int*)>(GetProcAddress(client,"CreateInterface"));
    void* clientInterface=factory ? factory("Source2Client002",nullptr) : nullptr;
    if(!clientInterface) throw std::runtime_error("Source2Client002 is unavailable.");
    void* target=(*reinterpret_cast<void***>(clientInterface))[Profile::FrameIndex];
    if(target!=reinterpret_cast<void*>(base+Profile::FrameNotify))
        throw std::runtime_error("The client frame callback differs from the reviewed profile.");
    clientBase=base;
    setAttribute=reinterpret_cast<SetAttribute>(base+Profile::SetAttribute);
    subclassChanged=reinterpret_cast<SubclassChanged>(base+Profile::SubclassChanged);
    regenerate=reinterpret_cast<Regenerate>(base+Profile::RegenerateSkins);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&OnFrame),reinterpret_cast<void**>(&originalFrame))!=MH_OK)
        throw std::runtime_error("Could not register the client cosmetic callback.");
    stopping=false;
    if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);throw std::runtime_error("Could not enable the client cosmetic callback.");}
    frameTarget=target;
}
bool InitializeSafe() {
    __try {Initialize();return true;} __except(MemoryFault(GetExceptionCode())) {return false;}
}
}
bool QueueApply(const Preset& preset,std::string& error) {
    std::lock_guard<std::mutex> lock(stateMutex);
    try {
        Validate(preset);
        if(!InitializeSafe()) throw std::runtime_error("Client cosmetic interface validation failed.");
        selections[Slot(preset.definition)]={preset,++revision};
        status="Preset enabled for the client. Waiting for a matching item.";
        error.clear();return true;
    } catch(const std::exception& e) {error=e.what();status=error;return false;}
}
void ClearAppliedPresets() {
    std::lock_guard<std::mutex> lock(stateMutex);
    selections.clear();applied.clear();gloveApplied={};
    status="Overrides stopped. Reconnect or reload the map to restore original appearances.";
}
size_t AppliedPresetCount() {std::lock_guard<std::mutex> lock(stateMutex);return selections.size();}
std::string GameStatus() {std::lock_guard<std::mutex> lock(stateMutex);return status;}
void ShutdownGame() {
    void* target=nullptr;
    {std::lock_guard<std::mutex> lock(stateMutex);stopping=true;selections.clear();target=frameTarget;}
    if(target) {
        MH_DisableHook(target);
        while(callbacks.load()!=0) Sleep(1);
        MH_RemoveHook(target);
    }
    {std::lock_guard<std::mutex> lock(stateMutex);frameTarget=nullptr;applied.clear();gloveApplied={};}
}
}
