#pragma once
#include <w1re/features/skins.hpp>
namespace Skins {
// Presets stay enabled across respawns and server changes. All game access runs
// on the client frame callback, never in Present or a local server callback.
bool QueueApply(const Preset& preset, std::string& error);
void ClearAppliedPresets();
size_t AppliedPresetCount();
std::string GameStatus();
void ShutdownGame();
}
