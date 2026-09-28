// Standalone Windows integration check. Uses generated silent MP3 frames.
#include "../features/music.hpp"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "music_metadata_cases.hpp"

using namespace std::chrono_literals;

template<class Predicate>
std::shared_ptr<const Music::Snapshot> Wait(Predicate predicate, const char* description) {
    const auto deadline = std::chrono::steady_clock::now() + 8s;
    do {
        auto state = Music::GetSnapshot();
        if (predicate(*state))
            return state;
        std::this_thread::sleep_for(25ms);
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error(std::string(description) + ": " + Music::GetSnapshot()->error);
}

void Require(bool condition, const char* description) {
    if (!condition)
        throw std::runtime_error(description);
}

int wmain(int argc, wchar_t** argv) {
    // Unique directory; cleanup below removes only explicitly created test files.
    const auto directory = std::filesystem::temp_directory_path() /
        (L"w1re-music-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto track = directory / L"silent space \u266b.MP3";
    const auto broken = directory / L"broken.mp3";
    const auto added = directory / L"added.mp3";
    int result = 0;
    bool ownsDirectory = false;
    try {
        Require(!std::filesystem::exists(directory), "Test directory already exists");
        ownsDirectory = std::filesystem::create_directory(directory);
        Require(ownsDirectory, "Could not create test directory");
        std::filesystem::create_directories(directory / "nested");
        TestMetadata(directory / "metadata.mp3");
        {
            // MPEG-1 Layer III, 128 kbps, 44.1 kHz stereo; zero spectral data.
            unsigned char frame[417] = { 0xff, 0xfb, 0x90, 0x00 };
            std::ofstream file(track, std::ios::binary);
            for (int i = 0; i < 115; ++i)
                file.write(reinterpret_cast<const char*>(frame), sizeof(frame));
        }
        std::ofstream(directory / "ignored.txt") << "not music";
        std::ofstream(directory / "nested" / "ignored.mp3") << "not scanned";
        Music::Initialize(directory);
        auto state = Wait([](const auto& s) { return s.ready; }, "Worker startup");
        Require(state->tracks.size() == 1 && state->tracks[0] == track, "MP3 scan and Unicode filename");
        Require(state->playback == Music::Playback::Stopped, "Must not autoplay");
        Music::Send(Music::Action::Volume, {}, 0);
        Music::Send(Music::Action::Play, track);
        state = Wait([](const auto& s) { return s.playback == Music::Playback::Playing && s.positionMs > 150; }, "Play silent MP3");
        Require(state->durationMs > 2500 && state->error.empty() && state->volume == 0, "Duration and muted playback");
        Music::Send(Music::Action::Pause);
        state = Wait([](const auto& s) { return s.playback == Music::Playback::Paused; }, "Pause");
        const int pausedAt = state->positionMs;
        std::this_thread::sleep_for(250ms);
        Require(std::abs(Music::GetSnapshot()->positionMs - pausedAt) < 100, "Paused position must remain stable");
        Music::Send(Music::Action::Play);
        Wait([pausedAt](const auto& s) { return s.playback == Music::Playback::Playing && s.positionMs > pausedAt + 100; }, "Resume");
        Music::Send(Music::Action::Seek, {}, 1500);
        Wait([](const auto& s) { return s.positionMs >= 1450; }, "Seek while playing");
        Music::Send(Music::Action::Repeat, {}, 1);
        Music::Send(Music::Action::Seek, {}, state->durationMs - 200);
        Wait([](const auto& s) { return s.repeat && s.playback == Music::Playback::Playing && s.positionMs < 800; }, "Repeat at end");
        Music::Send(Music::Action::Repeat, {}, 0);
        Music::Send(Music::Action::Seek, {}, state->durationMs - 200);
        Wait([](const auto& s) { return !s.repeat && s.playback == Music::Playback::Stopped; }, "Natural end without repeat");
        Music::Send(Music::Action::Play, track);
        Wait([](const auto& s) { return s.playback == Music::Playback::Playing; }, "Restart finished track");
        Music::Send(Music::Action::Stop);
        Wait([](const auto& s) { return s.playback == Music::Playback::Stopped && s.positionMs == 0; }, "Stop and rewind");
        Require(!Music::GetSnapshot()->outputOpen, "Stop must release audio device");
        std::filesystem::copy_file(track, added);
        std::ofstream(broken) << "invalid mp3 data";
        Music::Send(Music::Action::Refresh);
        Wait([](const auto& s) { return s.tracks.size() == 3; }, "Refresh added files");
        Music::Send(Music::Action::Play, broken);
        Wait([](const auto& s) { return !s.error.empty(); }, "Invalid MP3 error");
        Require(!Music::GetSnapshot()->outputOpen, "Invalid MP3 must not leak a device");
        Music::Send(Music::Action::Play, added);
        Wait([](const auto& s) { return s.playback == Music::Playback::Playing && s.error.empty(); }, "Recover after invalid file");
        Music::Shutdown();
        Require(!Music::GetSnapshot()->outputOpen, "Unload must release audio device");
        Music::Send(Music::Action::Play, added);
        Require(!Music::GetSnapshot()->ready, "Commands after shutdown must be ignored");
        Music::Initialize(directory / "empty");
        state = Wait([](const auto& s) { return s.ready; }, "Reinitialize empty library");
        Require(state->tracks.empty() && state->error.empty(), "Empty library must be usable");
        Music::Send(Music::Action::OpenPath, directory);
        Wait([&](const auto& s) { return s.directory == directory && s.tracks.size() == 3; }, "Load folder path");
        Music::Send(Music::Action::Volume, {}, 0);
        Music::Send(Music::Action::OpenPath, track);
        Wait([](const auto& s) { return s.playback == Music::Playback::Playing; }, "Load file path");
        Music::Send(Music::Action::Pause);
        Wait([](const auto& s) { return s.playback == Music::Playback::Paused; }, "Pause before seek");
        Music::Send(Music::Action::Seek, {}, 1000);
        Wait([](const auto& s) { return s.playback == Music::Playback::Paused && s.positionMs >= 950; }, "Seek while paused");
        Music::Shutdown();
        if (argc > 1) {
            const auto actual = std::filesystem::absolute(argv[1]);
            Music::Initialize(actual.parent_path());
            Wait([](const auto& s) { return s.ready; }, "Real library startup");
            Music::Send(Music::Action::Volume, {}, 0);
            Music::Send(Music::Action::Play, actual);
            auto realState = Wait([](const auto& s) { return s.playback == Music::Playback::Playing && s.positionMs > 200; }, "Play real MP3");
            std::cout << "Real MP3 duration: " << realState->durationMs << " ms\n";
            Require(realState->metadata && !realState->metadata->title.empty(), "Tagged MP3 title");
            Require(realState->metadata->cover && !realState->metadata->cover->rgba.empty(), "Tagged MP3 embedded cover");
            std::cout << "Metadata: " << realState->metadata->title << " / " << realState->metadata->artist
                      << " / " << realState->metadata->album << " / cover " << realState->metadata->cover->width
                      << 'x' << realState->metadata->cover->height << '\n';
            Music::Shutdown();
        }
        std::cout << "PASS: ID3 versions/encodings/bounds, scan, Unicode paths, no autoplay, play, pause, resume, seek, repeat, natural end, stop, refresh, invalid MP3 recovery, shutdown, empty library, file/folder loading.\n";
    } catch (const std::exception& error) {
        Music::Shutdown();
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    if (!ownsDirectory)
        return result;
    std::error_code ignored;
    for (const auto& file : { track, broken, added, directory / "metadata.mp3", directory / "ignored.txt", directory / "nested" / "ignored.mp3" })
        std::filesystem::remove(file, ignored);
    std::filesystem::remove(directory / "nested", ignored);
    std::filesystem::remove(directory / "empty", ignored);
    std::filesystem::remove(directory, ignored);
    return result;
}
