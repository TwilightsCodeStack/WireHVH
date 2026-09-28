#include "../features/skins.hpp"
#include <w1re/features/skins_game.hpp>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <functional>
using namespace Skins;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Reject(const std::function<void()>& action){bool rejected=false;try{action();}catch(...){rejected=true;}Require(rejected,"Invalid data was accepted");}
int main(){
    try {
        Require(ListingId(" https://csfloat.com/item/1234567890123456789?x=1 ")=="1234567890123456789","URL parse failed");
        Reject([]{ListingId("https://csfloat.com.evil/item/12");});
        Reject([]{ListingId("https://csfloat.com/item/../profile");});
        Reject([]{ListingId("12\r\nAuthorization: test");});
        const std::string json=R"({"id":"1234","item":{"def_index":7,"paint_index":180,"paint_seed":321,"float_value":0.013,"market_hash_name":"Example AK","stickers":[{"slot":4,"sticker_id":123,"wear":0.2,"rotation":45,"offset_x":0.1,"offset_y":-0.2,"scale":1.2}]}})";
        auto p=ParseListing(json);
        Require(p.definition==7 && p.seed==321 && p.stickers[4].id==123,"Listing values lost");
        const auto decoded=Deserialize(Serialize({p}));
        Require(decoded.size()==1 && decoded[0].stickers[4].y==p.stickers[4].y && decoded[0].listing=="1234","Round-trip failed");
        auto knife=p;knife.definition=515;knife.stickers={};Validate(knife);
        auto gloves=knife;gloves.definition=5030;Validate(gloves);gloves.definition=4725;Validate(gloves);
        Reject([&]{auto q=p;q.wear=std::numeric_limits<float>::quiet_NaN();Validate(q);});
        Reject([&]{auto q=p;q.seed=1001;Validate(q);});
        Reject([&]{auto q=p;q.definition=501;Validate(q);});
        Reject([&]{auto q=p;q.definition=515;Validate(q);});
        Reject([]{ParseListing(R"({"def_index":7,"paint_index":180,"paint_seed":1.5,"float_value":0.1})");});
        Reject([]{ParseListing(R"({"def_index":7,"paint_index":180,"paint_seed":1,"float_value":0.1,"stickers":[{"slot":5,"sticker_id":1}]})");});
        Reject([]{ParseListing(R"({"def_index":7,"paint_index":180,"paint_seed":1,"float_value":0.1,"stickers":[{"slot":1,"sticker_id":1},{"slot":1,"sticker_id":2}]})");});
        Reject([]{Deserialize(R"({"version":2,"presets":[]})");});
        std::string gameError;
        Require(!QueueApply(p,gameError),"Application must refuse a non-game host");
        Require(gameError.find("-insecure")!=std::string::npos,"Offline launch diagnostic was lost");
        ShutdownGame();
        std::cout<<"PASS: listing URLs, imported values, five slots, knives, gloves, validation, preset round-trips, and offline gate\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
