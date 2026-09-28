#pragma once
#include <cstdint>
// Native calls verified against the installed client. Schema fields live in Offsets.
// Never update the fingerprint without reviewing the native calls and their ABI.
namespace Skins::Profile {
inline constexpr int Build = 14183;
inline constexpr uint32_t ClientStamp = 1790366171u, ClientSize = 43614208u;
inline constexpr uintptr_t FrameNotify = 0xB6DD00;
inline constexpr uintptr_t SetAttribute = 0x119E310; // (C_EconItemView*, const char*, float)
inline constexpr uintptr_t RegenerateSkins = 0x82ACA0; // regenerate_weapon_skins callback
inline constexpr uintptr_t SubclassChanged = 0xB06A80; // (C_BaseEntity*) -> reload vdata/model
inline constexpr unsigned FrameIndex = 36;
inline constexpr int PostDataUpdateEnd = 7, NetUpdateEnd = 8;
}
