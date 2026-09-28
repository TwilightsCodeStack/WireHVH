#include <w1re/features/music.hpp>

#include <windows.h>
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable: 4456 4245) // Upstream miniaudio internals.
#include <third_party/miniaudio/miniaudio.h>
#pragma warning(pop)
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <mutex>
#include <thread>

namespace Music {
namespace {
    struct Request {
        Action action;
        std::filesystem::path track;
        int value;
    };

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Request> requests;
    std::thread worker;
    bool stopping = true;
    std::shared_ptr<const Snapshot> published = std::make_shared<Snapshot>();

    void Publish(const Snapshot& state) {
        auto next = std::make_shared<Snapshot>(state);
        std::lock_guard<std::mutex> lock(mutex);
        published = std::move(next);
    }

    bool IsMp3(const std::filesystem::path& path) {
        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return extension == L".mp3";
    }

    class Player {
    public:
        Snapshot state;
        explicit Player(const std::filesystem::path& directory) { state.directory = directory; }
        ~Player() { Close(); }

        void Refresh() {
            std::error_code error;
            std::filesystem::create_directories(state.directory, error);
            if (error) {
                state.error = "Cannot access music folder: " + state.directory.u8string() + " (" + error.message() + ")";
                return;
            }
            std::vector<std::filesystem::path> tracks;
            std::filesystem::directory_iterator it(state.directory, error), end;
            while (!error && it != end) {
                std::error_code entryError;
                if (IsMp3(it->path()) && it->is_regular_file(entryError)) tracks.push_back(it->path());
                it.increment(error);
            }
            if (error) {
                state.error = "Cannot scan music folder: " + error.message();
                return;
            }
            std::sort(tracks.begin(), tracks.end());
            state.tracks = std::move(tracks);
        }

        void Inspect(const std::filesystem::path& track) {
            state.metadataTrack = track;
            state.metadata = std::make_shared<TrackMetadata>(ReadMetadata(track));
        }

        void Handle(const Request& request) {
            // Inspect should not erase a playback error before the user can read it.
            if (request.action != Action::Inspect) state.error.clear();
            switch (request.action) {
            case Action::Refresh: Refresh(); break;
            case Action::Inspect: Inspect(request.track); break;
            case Action::OpenPath: {
                std::error_code error;
                auto path = std::filesystem::absolute(request.track, error);
                if (error || !std::filesystem::exists(path, error)) {
                    state.error = "Path not found. Paste an existing MP3 file or folder path.";
                } else if (std::filesystem::is_directory(path, error)) {
                    state.directory = path;
                    Refresh();
                } else if (IsMp3(path)) {
                    state.directory = path.parent_path();
                    Refresh();
                    Play(path);
                } else state.error = "Please select an MP3 file or a music folder.";
                break;
            }
            case Action::Play: Play(request.track); break;
            case Action::Pause:
                if (opened && state.playback == Playback::Playing && Check(ma_sound_stop(&sound), "Pause"))
                    state.playback = Playback::Paused;
                break;
            case Action::Stop: Close(); break;
            case Action::Volume:
                state.volume = std::clamp(request.value, 0, 100);
                if (opened) ma_sound_set_volume(&sound, state.volume / 100.0f);
                break;
            case Action::Repeat:
                state.repeat = request.value != 0;
                if (opened) ma_sound_set_looping(&sound, state.repeat);
                break;
            case Action::Seek:
                if (opened && state.durationMs > 0) {
                    const int position = std::clamp(request.value, 0, state.durationMs - 1);
                    if (Check(ma_sound_seek_to_second(&sound, position / 1000.0f), "Seek")) {
                        state.positionMs = position;
                        if (state.playback == Playback::Stopped) state.playback = Playback::Paused;
                    }
                }
                break;
            }
        }

        void Poll() {
            if (!opened) return;
            float seconds = 0;
            if (ma_sound_get_cursor_in_seconds(&sound, &seconds) == MA_SUCCESS)
                state.positionMs = std::max(0, static_cast<int>(seconds * 1000));
            if (state.playback == Playback::Playing && ma_sound_at_end(&sound)) {
                state.playback = Playback::Stopped;
                state.positionMs = state.durationMs;
            }
        }

    private:
        ma_engine engine = {};
        ma_sound sound = {};
        bool engineReady = false, opened = false;

        bool Check(ma_result result, const char* action) {
            if (result == MA_SUCCESS) return true;
            state.error = std::string(action) + ": " + ma_result_description(result) + " (" + std::to_string(result) + ")";
            return false;
        }

        void Close() {
            if (opened) { ma_sound_uninit(&sound); opened = false; }
            if (engineReady) { ma_engine_uninit(&engine); engineReady = false; }
            state.outputOpen = false;
            state.playback = Playback::Stopped;
            state.positionMs = 0;
        }

        void Play(const std::filesystem::path& requested) {
            const auto track = requested.empty() ? state.currentTrack : requested;
            if (track.empty()) { state.error = "Select an MP3 first."; return; }
            if (opened && track == state.currentTrack) {
                if (state.playback == Playback::Playing) return;
                if (ma_sound_at_end(&sound) && !Check(ma_sound_seek_to_pcm_frame(&sound, 0), "Restart")) return;
                if (Check(ma_sound_start(&sound), "Play")) state.playback = Playback::Playing;
                return;
            }
            std::error_code error;
            if (!IsMp3(track) || !std::filesystem::is_regular_file(track, error)) {
                state.error = "MP3 file not found: " + track.u8string();
                return;
            }
            Close();
            state.currentTrack.clear();
            state.durationMs = 0;
            Inspect(track);
            auto config = ma_engine_config_init();
            if (!Check(ma_engine_init(&config, &engine), "Open audio output")) return;
            engineReady = true;
            state.outputOpen = true;
            if (!Check(ma_sound_init_from_file_w(&engine, track.c_str(), MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                                 nullptr, nullptr, &sound), "Decode MP3")) {
                Close();
                return;
            }
            opened = true;
            ma_sound_set_volume(&sound, state.volume / 100.0f);
            ma_sound_set_looping(&sound, state.repeat);
            float length = 0;
            ma_sound_get_length_in_seconds(&sound, &length);
            state.durationMs = static_cast<int>(length * 1000);
            if (!Check(ma_sound_start(&sound), "Play")) { Close(); return; }
            state.currentTrack = track;
            state.playback = Playback::Playing;
        }
    };

    void Run(std::filesystem::path directory) {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            Player player(directory);
            try {
                if (FAILED(com)) {
                    player.state.error = "Could not initialize the Windows audio worker.";
                    Publish(player.state);
                } else {
                    player.Refresh();
                    player.state.ready = true;
                    Publish(player.state);
                    for (;;) {
                        std::deque<Request> pending;
                        {
                            std::unique_lock<std::mutex> lock(mutex);
                            wake.wait_for(lock, std::chrono::milliseconds(100),
                                [] { return stopping || !requests.empty(); });
                            if (stopping)
                                break;
                            pending.swap(requests);
                        }
                        for (const auto& request : pending) {
                            player.state.busy = true;
                            Publish(player.state);
                            player.Handle(request);
                            player.state.busy = false;
                        }
                        player.Poll();
                        Publish(player.state);
                    }
                }
            } catch (const std::exception& error) {
                player.state.ready = false;
                player.state.busy = false;
                player.state.playback = Playback::Stopped;
                player.state.error = std::string("Music player failed: ") + error.what();
                Publish(player.state);
            }
        } // Close the audio device before releasing COM or unloading the DLL.
        if (SUCCEEDED(com))
            CoUninitialize();
    }
}

    std::filesystem::path ResolveDirectory(const std::filesystem::path& moduleDirectory) {
        std::error_code error;
        const auto current = std::filesystem::current_path(error);
        // Match the GIF resource fallbacks, including a DLL copied away from its assets.
        const auto sourceDirectory = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        const std::filesystem::path candidates[] = {
            moduleDirectory / "resources" / "music",
            current / "resources" / "music",
            sourceDirectory / "resources" / "music"
        };
        for (const auto& candidate : candidates) {
            std::filesystem::directory_iterator it(candidate, error), end;
            while (!error && it != end) {
                if (IsMp3(it->path()) && it->is_regular_file(error)) return candidate;
                if (!error) it.increment(error);
            }
            error.clear();
        }
        return candidates[0];
    }

    void Initialize(const std::filesystem::path& directory) {
        std::lock_guard<std::mutex> lock(mutex);
        if (worker.joinable())
            return;
        stopping = false;
        Snapshot initial;
        initial.directory = directory;
        published = std::make_shared<Snapshot>(initial);
        try {
            worker = std::thread(Run, directory);
        } catch (const std::exception& error) {
            stopping = true;
            initial.error = std::string("Could not start music player: ") + error.what();
            published = std::make_shared<Snapshot>(initial);
        }
    }

    void Shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            requests.clear();
        }
        wake.notify_one();
        if (worker.joinable())
            worker.join();
        Snapshot stopped;
        Publish(stopped);
    }

    std::shared_ptr<const Snapshot> GetSnapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return published;
    }

    void Send(Action action, const std::filesystem::path& track, int value) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping || !published->ready)
                return;
            // Coalesce slider updates to keep the queue bounded during dragging.
            if (action == Action::Volume || action == Action::Seek || action == Action::Inspect) {
                requests.erase(std::remove_if(requests.begin(), requests.end(),
                    [action](const Request& request) { return request.action == action; }), requests.end());
            }
            if (requests.size() >= 64)
                return;
            requests.push_back({ action, track, value });
        }
        wake.notify_one();
    }
}
