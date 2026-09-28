#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ID3D11Device;

namespace Music {
    enum class Playback { Stopped, Playing, Paused };
    enum class Action { Refresh, Play, Pause, Stop, Volume, Repeat, Seek, OpenPath, Inspect };

    struct CoverImage {
        unsigned int width = 0, height = 0;
        std::vector<unsigned char> rgba;
    };

    struct TrackMetadata {
        std::string title, artist, album, year, trackNumber;
        std::shared_ptr<const CoverImage> cover;
    };

    // Bounded ID3 reader. Bad/missing tags never prevent audio playback.
    TrackMetadata ReadMetadata(const std::filesystem::path& path);

    struct Snapshot {
        std::filesystem::path directory;
        std::vector<std::filesystem::path> tracks;
        std::filesystem::path currentTrack;
        std::filesystem::path metadataTrack;
        std::shared_ptr<const TrackMetadata> metadata;
        Playback playback = Playback::Stopped;
        int volume = 50;
        bool repeat = false;
        int positionMs = 0;
        int durationMs = 0;
        std::string error;
        bool ready = false;
        bool busy = false;
        bool outputOpen = false;
    };

    // The worker owns all audio devices and file I/O. UI calls only queue work.
    void Initialize(const std::filesystem::path& directory);
    std::filesystem::path ResolveDirectory(const std::filesystem::path& moduleDirectory);
    void Shutdown();
    std::shared_ptr<const Snapshot> GetSnapshot();
    void Send(Action action, const std::filesystem::path& track = {}, int value = 0);
    void RenderUI(ID3D11Device* device);
    void CleanupUI();
}
