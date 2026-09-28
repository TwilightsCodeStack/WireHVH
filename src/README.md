# W1RE

W1RE is a Windows x64 DirectX 11 overlay DLL with an ImGui interface and feature modules.

## Layout

| Path | Purpose |
| --- | --- |
| `src/app/` | DLL entry point, DirectX hooks, input capture, and standalone loader |
| `src/features/` | Feature implementations, including music, skins, visuals, and movement |
| `include/w1re/` | Shared configuration, runtime, offset, and workspace headers |
| `include/w1re/features/` | Feature interfaces and generated profile headers |
| `third_party/miniaudio/` | Bundled MP3 decoder and audio engine, including upstream license |
| `resources/` | Runtime background assets and the `music/` MP3 folder |
| `imgui-1.92.7/` | Dear ImGui dependency |
| `MinHook_134_bin (1)/` | MinHook dependency |
| `cs2-dumper-main/` | Offset-generation tool and generated reference output |
| `features/` and root source/header names | Compatibility forwarding files for existing paths |

## Build

Install Visual Studio 2022 Build Tools with the **Desktop development with C++** workload, then run:

```bat
build.bat
```

The PowerShell equivalent is:

```powershell
.\build.ps1
```

The build creates `W1RE.dll` and `W1RE-Loader.exe` in the project root, and copies
the required `MinHook.x64.dll` beside them. Runtime assets should remain in `resources\`.

## GitHub build

The `GitHub-Ready` folder is a clean handoff containing source and a Windows x64
build. Its source package includes a GitHub Actions workflow that builds the
loader, DLL, and cs2-dumper from source and publishes a ZIP artifact. Generated
builds use compiler path remapping to avoid embedding the build machine's local
project path.

User presets, MP3 files, and background images are intentionally excluded from
the public packages. Add your own runtime assets under `resources\` after
extracting the build. No project license is included; choose and add one before
publishing the source repository.

## Loader

Start CS2, then open `W1RE-Loader.exe`. It finds `cs2.exe`, checks that both DLLs
are valid x64 files, loads MinHook followed by W1RE, and reports the result.
It retries a transient cs2-dumper process-discovery failure for up to two
minutes while the game is starting. Keep the loader and both DLLs together;
their paths are resolved beside the loader regardless of the current working
directory. The console stays open when launched by double-click so errors
remain visible.

```powershell
.\W1RE-Loader.exe --check             # Check files and target without loading
.\W1RE-Loader.exe --pid 1234          # Select CS2 if multiple instances exist
.\W1RE-Loader.exe --no-pause          # Do not wait for Enter when finished
.\W1RE-Loader.exe --install "C:\path\to\CS2\game\bin\win64" # Install bundle under that folder's W1RE\
```

`--install` requires the folder containing `cs2.exe` and copies the loader,
DLLs, dumper, and runtime resources into its `W1RE\` subfolder. Existing runtime
binaries are updated on reinstall, while existing resource files are preserved.
This keeps the bundle in the game directory but does not make CS2 load it
automatically: start the installed `W1RE-Loader.exe` each game session.

An already-loaded copy is left alone. A different loaded copy of either DLL
produces an error; restart CS2 before switching copies. The loader uses the
standard Windows LoadLibrary mechanism and has no anti-cheat bypass. Errors
return exit code 1; successful checks, loads, and `--help` return 0. A successful
load confirms module presence; overlay initialization continues separately in
the DLL's console. If W1RE fails after MinHook loads, MinHook may remain loaded
until the game exits. A timed-out load is not forcibly terminated; check the
game before retrying.

Run `.\tests\run_loader_smoke.ps1` to test with a disposable local host and a
small fixture DLL. This does not launch or inject into the actual game.

The overlay prefers the original animated background in this order:

1. `resources\bg.gif` — original animated background
2. `resources\cs2-home-menu.gif` / `cs2-home-menu.png` — fallback captures
3. `resources\w1re-background.gif` — existing fallback background

Keep **Live scene** turned off to display the GIF.

## Offline skins (experimental)

Open **Skins** to import a CSFloat listing URL or numeric listing ID, paste listing
JSON, or edit a preset manually. Imports include the item definition, paint kit,
pattern seed, wear, and five sticker slots with wear, scale, rotation, and X/Y
placement. Guns, knives, and gloves (including Broken Fang) are supported.
Stickers are restricted to guns. Imported data is validated before use.

1. Launch CS2 with `-insecure` and open a local practice/workshop/surf map.
2. Spawn and hold the gun you want to customize, or hold any knife for a knife
   preset. Glove presets target your local player's gloves.
3. Import a listing or choose a saved preset and click **Apply to local map**.
4. For knife model changes, enable `sv_cheats 1` in the offline map first; the
   application uses the game's `subclass_change` command for the model.

Updates run on the local server simulation thread, with a listen-server loopback
check immediately before applying. Guns must match the preset's item definition.
Application is explicit: apply again after respawning or obtaining another weapon.
Reload the map to restore normal items. These are entity-local cosmetic changes;
no Steam inventory, purchase, sale, or trade requests are made.

**In-game appearance is not yet verified.** The implementation targets build
14183 and checks the installed client, server, and engine module identities.
Import tests and compilation do not verify knife animations, glove refresh, or
sticker rendering. The status message reports attribute updates rather than
visual verification. Different builds fail closed. For maintenance,
`tools\update_skin_profile.ps1` generates a profile from a supplied dump and game
directory; interface compatibility must also be checked before accepting a new
build. The generated header is `include\w1re\features\skins_profile.hpp`.

Presets are saved explicitly to `resources\skins\presets.json` with atomic file
replacement. HTTPS imports run in a background worker. If CSFloat requests
authorization, set `CSFLOAT_API_KEY` in the environment before starting CS2 or
paste listing JSON. Keys are not saved in presets. Only the listing endpoint is
requested, with redirects disabled. Failed imports leave the current preset intact.

The JSON parser is nlohmann/json 3.11.3 (MIT), bundled in `third_party\nlohmann`.
API fields follow [CSFloat's documentation](https://docs.csfloat.com/). Native
declarations were checked against the [CS2 SDK](https://github.com/alliedmodders/hl2sdk/tree/cs2),
and attribute encoding against the maintained
[inventory simulator](https://github.com/ianlucas/cs2-css-inventory-simulator).

Use `.\build.ps1 -DllOnly` to rebuild the overlay while the unchanged loader is
open. `tests\skins_data_test.cpp` checks listing validation and preset round-trips;
the workspace UI test includes the Skins page and animated background.

## Controls

- `Insert` toggles the interface.
- `End` unloads the overlay.

## Music

1. Put your `.mp3` files in `resources\music\` beside the built `W1RE.dll`.
2. Open the interface with `Insert`, select **Music**, and click **Refresh tracks**.
3. Select a track and click **Play**. Use **Pause / Resume**, **Stop**, the
   position slider, **Volume**, and **Repeat track** to control playback.

The current folder is shown on the Music page. You can also paste a full MP3
file path or folder path and click **Load path** (quoted Windows paths work).
A file loads and starts playing; a folder updates the library. Resource discovery
checks beside the DLL, the current working directory, and the build's source
directory, preferring a location containing MP3 files.

Selecting a track displays its embedded title, artist, album, year, track number,
and cover artwork when available. The reader supports ID3v2.2, v2.3, v2.4 and
ID3v1 text fallback, with UTF-8, UTF-16 and Latin-1 text. Embedded pictures are
decoded locally, preferring the front cover; nothing is downloaded. Files without
tags use their filename, and missing artwork gets a placeholder. Invalid or
oversized metadata is skipped without preventing playback.

Music continues when the interface is hidden and stops when `End` unloads the
DLL. Playback starts only when you click Play or load a file path. Volume and repeat are session
settings; repeat loops the current track, and the player otherwise stops at its
end. The library scans MP3 files directly inside the music folder (including
uppercase `.MP3` extensions); subfolders are not scanned.

The player uses bundled miniaudio 0.11.23 for decoding and streaming playback,
replacing the Windows MCI backend that failed with tagged MP3 files such as
`Rental.mp3`. Scanning, metadata reads, and playback commands run on a worker;
only the small decoded cover texture is uploaded by the render thread. Artwork
is limited to 512 pixels per side. The worker closes its audio output and joins
its threads before DLL unload. No additional audio DLL is required. Missing
files, unsupported MP3s, and audio-output errors appear on the Music page.

Backgrounds remain handled by `src\app\hooks.cpp`: image bytes are decoded with
`stb_image.h`, GIF frames become DirectX 11 textures, and frame delays control
animation. Music uses the same DLL-relative resource layout but runs independently
of the GIF animation and the optional live game-scene background.

### Music integration check

Run `.\tests\run_music_smoke.ps1` in a normal Windows desktop session. The test
generates a temporary silent MP3 and checks scanning, Unicode paths, playback,
pause/resume, seeking, repeat, stop, invalid-file recovery, and device cleanup.
It does not launch or inject into the game. Windows audio output must be
available. Add `-Mp3 'resources\music\Rental.mp3'` to test the real tagged MP3,
including its title and decoded cover, with playback muted.

Run `.\tests\run_music_ui_preview.ps1` to render the Music page and embedded
cover to `tests\music-preview.png` without opening the game or playing audio.
