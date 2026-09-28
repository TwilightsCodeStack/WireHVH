#include <w1re/features/music.hpp>
#include <d3d11.h>
#include <algorithm>
#include <imgui.h>

namespace Music {
namespace {
    ID3D11ShaderResourceView* coverTexture = nullptr;
    std::shared_ptr<const CoverImage> displayedCover;
    std::string coverError;

    void UpdateCover(ID3D11Device* device, const std::shared_ptr<const CoverImage>& cover) {
        if (cover == displayedCover) return;
        if (coverTexture) { coverTexture->Release(); coverTexture = nullptr; }
        displayedCover = cover;
        coverError.clear();
        if (!cover || !device) return;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = cover->width;
        desc.Height = cover->height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = cover->rgba.data();
        data.SysMemPitch = cover->width * 4;
        ID3D11Texture2D* texture = nullptr;
        if (SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture))) {
            if (FAILED(device->CreateShaderResourceView(texture, nullptr, &coverTexture)))
                coverError = "Cover artwork could not be displayed.";
            texture->Release();
        } else coverError = "Cover artwork could not be displayed.";
    }
}

    void CleanupUI() {
        if (coverTexture) { coverTexture->Release(); coverTexture = nullptr; }
        displayedCover.reset();
        coverError.clear();
    }

    void RenderUI(ID3D11Device* device) {
        const auto state = GetSnapshot();
        const float scale = ImGui::GetStyle().FontScaleMain;
        ImGui::PushID("MusicPlayer");
        ImGui::TextColored(ImVec4(0.74f, 0.58f, 0.94f, 1), "MUSIC");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu tracks%s", state->tracks.size(), state->busy ? "  /  Loading..." : "");
        ImGui::Separator();
        ImGui::TextWrapped("Folder: %s", state->directory.u8string().c_str());
        if (ImGui::Button("Refresh tracks")) Send(Action::Refresh);
        static char loadPath[4096] = {};
        ImGui::SetNextItemWidth(std::max(100.0f * scale, ImGui::GetContentRegionAvail().x - 110.0f * scale));
        const bool entered = ImGui::InputTextWithHint("##Path", "Paste an MP3 file or folder path", loadPath, sizeof(loadPath), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const bool load = ImGui::Button("Load path");
        if ((entered || load) && loadPath[0]) {
            std::string path(loadPath);
            if (path.size() >= 2 && path.front() == '"' && path.back() == '"') path = path.substr(1, path.size() - 2);
            Send(Action::OpenPath, std::filesystem::u8path(path));
        }

        static std::filesystem::path selected, inspected, lastPlaying;
        if (state->currentTrack != lastPlaying) {
            lastPlaying = state->currentTrack;
            if (!lastPlaying.empty()) selected = lastPlaying;
        }
        if (std::find(state->tracks.begin(), state->tracks.end(), selected) == state->tracks.end())
            selected = state->tracks.empty() ? std::filesystem::path{} : state->tracks.front();
        const auto selectedName = selected.empty() ? std::string("No MP3 files found") : selected.filename().u8string();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Track", selectedName.c_str())) {
            for (const auto& track : state->tracks) {
                ImGui::PushID(track.u8string().c_str());
                if (ImGui::Selectable("##Row", selected == track, 0, ImVec2(0, ImGui::GetTextLineHeight()))) selected = track;
                ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin(), ImGui::GetColorU32(ImGuiCol_Text), track.filename().u8string().c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (state->ready && selected != inspected) {
            inspected = selected;
            if (!selected.empty()) Send(Action::Inspect, selected);
        }
        const auto metadata = state->metadataTrack == selected ? state->metadata : nullptr;
        UpdateCover(device, metadata ? metadata->cover : nullptr);
        ImGui::Spacing();
        const float coverSize = 116 * scale;
        ImGui::BeginGroup();
        if (coverTexture && displayedCover) {
            const float imageScale = coverSize / std::max(displayedCover->width, displayedCover->height);
            const ImVec2 size(displayedCover->width * imageScale, displayedCover->height * imageScale);
            const auto pos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(coverSize, coverSize));
            const ImVec2 start(pos.x + (coverSize - size.x) / 2, pos.y + (coverSize - size.y) / 2);
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(coverTexture)), start, ImVec2(start.x + size.x, start.y + size.y));
        } else {
            ImGui::Button("No cover", ImVec2(coverSize, coverSize));
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 16 * scale);
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextDisabled("SELECTED TRACK");
        ImGui::TextUnformatted(metadata ? metadata->title.c_str() : selectedName.c_str());
        ImGui::TextUnformatted(metadata && !metadata->artist.empty() ? metadata->artist.c_str() : "Unknown artist");
        ImGui::TextDisabled("%s", metadata && !metadata->album.empty() ? metadata->album.c_str() : "Unknown album");
        if (metadata && (!metadata->year.empty() || !metadata->trackNumber.empty()))
            ImGui::TextDisabled("%s%s%s", metadata->year.c_str(), metadata->trackNumber.empty() ? "" : "  /  Track ", metadata->trackNumber.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::Spacing();

        ImGui::BeginDisabled(!state->ready || selected.empty());
        if (ImGui::Button("Play", ImVec2(90 * scale, 32 * scale))) Send(Action::Play, selected);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(state->playback == Playback::Stopped);
        if (ImGui::Button(state->playback == Playback::Paused ? "Resume" : "Pause", ImVec2(90 * scale, 32 * scale)))
            Send(state->playback == Playback::Paused ? Action::Play : Action::Pause);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(state->currentTrack.empty());
        if (ImGui::Button("Stop", ImVec2(90 * scale, 32 * scale))) Send(Action::Stop);
        ImGui::EndDisabled();

        const char* status = state->playback == Playback::Playing ? "Playing" : state->playback == Playback::Paused ? "Paused" : "Stopped";
        ImGui::TextWrapped("%s%s%s", status, state->currentTrack.empty() ? "" : ": ", state->currentTrack.filename().u8string().c_str());
        ImGui::Text("%d:%02d / %d:%02d", state->positionMs / 60000, state->positionMs / 1000 % 60, state->durationMs / 60000, state->durationMs / 1000 % 60);
        static int seekMs = 0;
        ImGui::BeginDisabled(state->durationMs <= 0 || state->playback == Playback::Stopped);
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##Seek", &seekMs, 0, std::max(1, state->durationMs), "Seek");
        if (ImGui::IsItemDeactivatedAfterEdit()) Send(Action::Seek, {}, seekMs);
        if (!ImGui::IsItemActive()) seekMs = state->positionMs;
        ImGui::EndDisabled();

        static int volume = 50;
        ImGui::SetNextItemWidth(160 * scale);
        if (ImGui::SliderInt("Volume", &volume, 0, 100, "%d%%")) Send(Action::Volume, {}, volume);
        if (!ImGui::IsItemActive() && !state->busy) volume = state->volume;
        ImGui::SameLine();
        bool repeat = state->repeat;
        if (ImGui::Checkbox("Repeat track", &repeat)) Send(Action::Repeat, {}, repeat ? 1 : 0);
        if (!state->error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.55f, 0.5f, 1));
            ImGui::TextWrapped("%s", state->error.c_str());
            ImGui::PopStyleColor();
        }
        if (!coverError.empty()) ImGui::TextWrapped("%s", coverError.c_str());
        if (!state->ready && state->error.empty()) ImGui::TextUnformatted("Starting music player...");
        ImGui::TextWrapped("Music keeps playing with the menu hidden. End stops it.");
        ImGui::PopID();
    }
}
