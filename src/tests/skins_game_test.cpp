// Exercise the production client update path against an isolated entity fixture.
#include "../src/features/skins_game.cpp"
#include <array>
#include <iostream>
#include <vector>
#include <thread>

namespace {
void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
using Bytes=std::vector<unsigned char>;
using Attr=std::array<unsigned char,0x48>;
std::map<Skins::Address,std::vector<Attr>> attributes;
std::map<std::string,uint16_t> names;
unsigned refreshes=0,subclasses=0,frameCalls=0;
void __fastcall FakeFrame(void*,int) {++frameCalls;}
void __fastcall FakeRefresh() {++refreshes;}
void __fastcall FakeSubclass(void*) {++subclasses;}
void __fastcall FakeSet(void* pointer,const char* name,float value) {
    const auto item=reinterpret_cast<Skins::Address>(pointer);
    auto id=names.emplace(name,static_cast<uint16_t>(names.size()+1)).first->second;
    auto& entries=attributes[item];
    auto found=std::find_if(entries.begin(),entries.end(),[&](const Attr& a) {
        uint16_t actual;std::memcpy(&actual,a.data()+0x30,2);return actual==id;
    });
    if(found==entries.end()) {entries.push_back({});found=entries.end()-1;std::memcpy(found->data()+0x30,&id,2);}
    std::memcpy(found->data()+0x34,&value,4);
    Skins::Field<int>(item+Offsets::m_AttributeList,8)=static_cast<int>(entries.size());
    Skins::Field<Skins::Address>(item+Offsets::m_AttributeList,16)=reinterpret_cast<Skins::Address>(entries.data());
}
}
int main() {
    try {
        using namespace Skins;
        Bytes module(64),system(64),page(512*0x70),controller(0x1000),pawnData(0x5000),
            gunData(0x2500),knifeData(0x2500),otherData(0x2500),servicesData(0x200),respawnData(0x5000);
        const auto addr=[](Bytes& b){return reinterpret_cast<Address>(b.data());};
        const Address pawn=addr(pawnData),gun=addr(gunData),knife=addr(knifeData),other=addr(otherData),services=addr(servicesData);
        const uint32_t pawnHandle=0x8001,gunHandle=0x8002,knifeHandle=0x8003,otherHandle=0x8004;
        auto bind=[&](uint32_t handle,Address entity) {
            const auto identity=addr(page)+0x70*(handle&0x1ffu);
            Field<Address>(identity,0)=entity;Field<uint32_t>(identity,0x10)=handle;
        };
        uint32_t handles[]={gunHandle,knifeHandle,otherHandle,0xffffffffu};
        Offsets::dwEntityList=0;Offsets::dwLocalPlayerController=8;
        clientBase=addr(module);Field<Address>(clientBase,0)=addr(system);
        Field<Address>(clientBase,8)=addr(controller);Field<Address>(addr(system),0x10)=addr(page);
        Field<uint32_t>(addr(controller),Offsets::m_hPlayerPawn)=pawnHandle;
        bind(pawnHandle,pawn);bind(gunHandle,gun);bind(knifeHandle,knife);bind(otherHandle,other);
        Field<int>(pawn,Offsets::m_iHealth)=100;Field<Address>(pawn,Offsets::m_pWeaponServices)=services;
        Field<int>(services+Offsets::m_hMyWeapons,0)=4;
        Field<Address>(services+Offsets::m_hMyWeapons,8)=reinterpret_cast<Address>(handles);
        Field<uint32_t>(gun,Offsets::m_hOwnerEntity)=pawnHandle;
        Field<uint32_t>(knife,Offsets::m_hOwnerEntity)=pawnHandle;
        Field<uint32_t>(other,Offsets::m_hOwnerEntity)=0xffffffffu;
        const Address gunItem=gun+Offsets::m_AttributeManager+Offsets::m_Item;
        const Address knifeItem=knife+Offsets::m_AttributeManager+Offsets::m_Item;
        const Address otherItem=other+Offsets::m_AttributeManager+Offsets::m_Item;
        Field<uint16_t>(gunItem,Offsets::m_iItemDefinitionIndex)=7;
        Field<uint16_t>(otherItem,Offsets::m_iItemDefinitionIndex)=7;
        Field<uint16_t>(knifeItem,Offsets::m_iItemDefinitionIndex)=42;
        frameTarget=reinterpret_cast<void*>(1);originalFrame=FakeFrame;
        setAttribute=FakeSet;regenerate=FakeRefresh;subclassChanged=FakeSubclass;
        std::string error;
        Preset gunPreset;gunPreset.paint=180;gunPreset.seed=321;
        gunPreset.stickers[4].id=123;gunPreset.stickers[4].x=.1f;
        Require(QueueApply(gunPreset,error),"Gun preset was rejected");
        OnFrame(nullptr,Profile::PostDataUpdateEnd);
        Require(refreshes==0 && frameCalls==1,"Wrong frame stage updated items");
        OnFrame(nullptr,Profile::NetUpdateEnd);
        Require(!faulted && refreshes==1,"Gun update failed");
        Require(Field<int>(gun,Offsets::m_nFallbackPaintKit)==180,"Paint not applied");
        Require(Field<int>(other,Offsets::m_nFallbackPaintKit)==0,"Nonlocal weapon was modified");
        Require(attributes[gunItem].size()==33,"Five sticker slots were not populated");
        uint32_t stickerBits=0;
        for(const auto& a:attributes[gunItem]) {
            uint16_t id;std::memcpy(&id,a.data()+0x30,2);
            if(id==names.at("sticker slot 4 id")) std::memcpy(&stickerBits,a.data()+0x34,4);
        }
        Require(stickerBits==123,"Sticker ID was not bit encoded");
        OnFrame(nullptr,Profile::NetUpdateEnd);Require(refreshes==1,"Unchanged item regenerated");
        FakeSet(reinterpret_cast<void*>(gunItem),"set item texture prefab",10.f);
        OnFrame(nullptr,Profile::NetUpdateEnd);Require(refreshes==2,"Server attribute replacement wasn't reapplied");
        Field<uint32_t>(gunItem,Offsets::m_iItemIDHigh)=0;
        OnFrame(nullptr,Profile::NetUpdateEnd);Require(refreshes==3,"Network item identity wasn't reapplied");
        Preset knifePreset;knifePreset.definition=515;knifePreset.paint=38;
        Require(QueueApply(knifePreset,error),"Knife preset rejected");
        OnFrame(nullptr,Profile::NetUpdateEnd);
        Require(subclasses==1 && Field<uint16_t>(knifeItem,Offsets::m_iItemDefinitionIndex)==515,"Client knife model callback missing");
        knifePreset.definition=500;Require(QueueApply(knifePreset,error),"Knife replacement rejected");
        OnFrame(nullptr,Profile::NetUpdateEnd);
        Require(subclasses==2 && AppliedPresetCount()==2,"Knife presets did not replace one shared slot");
        Preset glovePreset;glovePreset.definition=5030;glovePreset.paint=10006;
        Require(QueueApply(glovePreset,error),"Gloves rejected");
        OnFrame(nullptr,Profile::NetUpdateEnd);
        Require(Field<bool>(pawn,Offsets::m_bNeedToReApplyGloves),"Glove refresh wasn't requested");
        Require(Field<uint16_t>(pawn+Offsets::m_EconGloves,Offsets::m_iItemDefinitionIndex)==5030,"Glove definition wasn't applied");
        const unsigned beforeDeath=refreshes;
        Field<int>(pawn,Offsets::m_iHealth)=0;gunPreset.paint=302;QueueApply(gunPreset,error);
        OnFrame(nullptr,Profile::NetUpdateEnd);Require(refreshes==beforeDeath,"Dead player was updated");
        const Address newPawn=addr(respawnData);bind(pawnHandle+0x8000,newPawn);
        Field<uint32_t>(addr(controller),Offsets::m_hPlayerPawn)=pawnHandle+0x8000;
        Field<int>(newPawn,Offsets::m_iHealth)=100;Field<Address>(newPawn,Offsets::m_pWeaponServices)=services;
        Field<uint32_t>(gun,Offsets::m_hOwnerEntity)=pawnHandle+0x8000;
        Field<uint32_t>(knife,Offsets::m_hOwnerEntity)=pawnHandle+0x8000;
        OnFrame(nullptr,Profile::NetUpdateEnd);
        Require(refreshes==beforeDeath+1 && Field<int>(gun,Offsets::m_nFallbackPaintKit)==302,"Respawn lost pending presets");
        Require(Field<bool>(newPawn,Offsets::m_bNeedToReApplyGloves),"Respawn did not refresh gloves");
        bind(gunHandle+0x8000,gun);gunPreset.paint=180;QueueApply(gunPreset,error);
        OnFrame(nullptr,Profile::NetUpdateEnd);
        Require(Field<int>(gun,Offsets::m_nFallbackPaintKit)==302,"Stale entity serial was accepted");
        handles[0]=gunHandle+0x8000;
        OnFrame(nullptr,Profile::NetUpdateEnd);
        Require(Field<int>(gun,Offsets::m_nFallbackPaintKit)==180,"Replacement weapon was skipped");
        Field<Address>(clientBase,8)=0;OnFrame(nullptr,Profile::NetUpdateEnd);
        Require(AppliedPresetCount()==3 && applied.empty(),"Disconnect lost selection or retained entity pointers");
        Field<Address>(clientBase,8)=addr(controller);OnFrame(nullptr,Profile::NetUpdateEnd);
        ClearAppliedPresets();const auto beforeStop=refreshes;
        OnFrame(nullptr,Profile::NetUpdateEnd);Require(AppliedPresetCount()==0 && refreshes==beforeStop,"Stop overrides failed");
        QueueApply(gunPreset,error);
        Field<int>(services+Offsets::m_hMyWeapons,0)=10000;
        OnFrame(nullptr,Profile::NetUpdateEnd);
        Require(faulted && !QueueApply(gunPreset,error),"Malformed memory didn't latch a fault");
        frameTarget=nullptr;ShutdownGame();
        Require(callbacks==0,"Callback count leaked");
        std::cout<<"PASS: client lifecycle, five stickers, knives, gloves, ownership, serials, network updates, respawns, reconnect, stop and fault handling\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
