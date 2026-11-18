# BromaIDA

Broma support for IDA.

Reads the Broma files from the [Geometry Dash Geode bindings](https://github.com/geode-sdk/bindings) and applies as much of them as possible to your IDB: names, signatures, types, and vtables. It can also export what a database knows back into Broma.

BromaIDA is a native C++23 plugin built on the IDA SDK. It parses Broma with the official [Broma parser](https://github.com/geode-sdk/Broma), so there is no Python, no PyBroma and nothing to `pip install`.

> [!WARNING]
> The C++ rewrite has been built and unit-tested (including against the real 2.2091 bindings), but it has not been run inside IDA yet. Expect rough edges and please report them.

## Features

- **Importing bindings**
  - Names functions at the addresses Broma gives, merging functions that share an address (with a `Merged with:` comment).
  - Resolves symbols that are already named in the binary (Android, libraries) by overload and applies the Broma signature to them.
  - Asks before it overwrites a name or a comment you may have written, unless told not to.
- **Importing types** (needs IDAClang, see [Importing types](#importing-types))
  - Class members and virtual function tables.
  - Return types, argument types and argument names, including hidden return pointers and `this`.
  - Fixes the signatures IDA guessed.
- **Exporting bindings**
  - Writes addresses into an existing `.bro` file and everything it includes, leaving comments and formatting alone.
  - Creates missing methods and classes. New classes go into the file you picked (choosing a file per class is in the core but not yet exposed in the plugin's UI).
  - Optionally writes return types and argument names.
  - Reads exported Itanium symbols (Android builds) as well as names applied by an earlier import.
- **Export current function** from the context menu of the disassembly and pseudocode views.

## Requirements

- IDA Pro 9.4. (The plugin has to be built against the SDK that matches your IDA, see [Building](#building).)
- IDAClang, only for importing types.
- To build: CMake 3.25+, Ninja, a C++23 compiler (clang is what the presets use) and **Qt 6.8** (see [Building](#building)).

## Installation

Build it (below) or take a package for your platform from the releases, then put these two things in IDA's plugin folder, next to each other:

```
plugins/bromaida.so          (.dll on Windows, .dylib on macOS)
plugins/bromaida/            profiles/, sdk/ and stl/: what the type import is built from
```

The plugin folders are `~/.idapro/plugins/` on Linux and macOS, and `%APPDATA%\Hex-Rays\IDA Pro\plugins\` on Windows. You can also use `<IDA install dir>/plugins/`, or point the `IDAUSR` environment variable at a folder that contains `plugins/`. Restart IDA afterwards.

Without the `bromaida` folder the plugin starts, but type import will refuse to run. If you are updating from an older build, delete the old `bromaida/types` folder; nothing reads it any more.

## Usage

Open the plugin from **Edit → Plugins → BromaIDA** or press `Ctrl+Shift+B`. The window shows the detected platform and lets you pick Import or Export, with a Settings button.

| Action                  | What it does                                                                  |
|-------------------------|-------------------------------------------------------------------------------|
| Import                  | Pick the folder that holds the `.bro` files and apply them to the database.    |
| Export                  | Pick the `.bro` file to write into; a missing file is created.                 |
| Settings                | Opens the settings dialog.                                                     |
| Export current function | Right-click in the disassembly or pseudocode view; shows the Broma line with a Copy button. |

**Importing a folder.** The import dialog asks for the folder with your `.bro` files, for example `bindings/2.2091`. If it has an `Entry.bro`, that file is used and pulls in the rest through its includes. Without one, every `.bro` file in the folder (subfolders are not searched) is loaded and merged, and a class defined in two different files is reported and taken from the first. The `include/Geode/Enums.hpp` next to the folder is used for the enums, as before.

The dialogs are Qt widgets. In IDA's text version, or if IDA's Qt is not the 6.8 the plugin was built against, BromaIDA falls back to IDA's own dialogs instead of risking a crash.

Wait for the `AU: idle` indicator before running anything. Changing a database while auto-analysis is still running can corrupt it, and BromaIDA refuses to start until analysis has finished.

The platform is detected from the open binary: PE is Windows, Mach-O is macOS Intel, macOS ARM or iOS (told apart by its architecture and build version), and ELF is Android 32 or 64 depending on its bitness.

### Scripting

The plugin can run without dialogs, for batch jobs and idalib. Set `BROMAIDA_FILE` and run the plugin with argument `1` (import: a folder of `.bro` files, or a single `.bro` file) or `2` (export: the `.bro` file to write into):

```python
import ida_loader
ida_loader.load_and_run_plugin("bromaida", 1)
```

## Importing types

Type import generates one header from the Broma classes plus the headers for everything Broma doesn't define (Cocos2d-x, FMOD, helpers), and hands it to IDAClang. Do this once per database; it can take a while and may freeze IDA for a few minutes.

- What goes into it depends on the game version, through a [profile](#profiles). You don't need to supply any headers.
- Before the import, BromaIDA checks that its STL stand-in declares every `std::` type the bindings use (`std::mutex`, `std::atomic_bool`...). IDAClang crashes on a class with a member of an undeclared type, so if one is missing you are asked whether to skip the type import.
- The imported types go into a `BromaIDA` folder in the Local Types view. A re-import clears that folder first.
- The header's digest is remembered in the IDB. If the input didn't change, the next import skips the type step (disable that in the settings).
- If IDAClang isn't available, the option turns itself off.
- Type import is tuned for recent Geometry Dash versions; older bindings may need manual fixing.

If a previous import left broken or mismatched structs behind, delete the `BromaIDA` folder in Local Types (right-click, *Show folders*), save the IDB and import again.

## Profiles

The game changed between versions (Cocos2d-x vtables, and the standard library RobTop builds with), so what a type import needs depends on the version. A **profile** names the pieces:

- an **SDK bundle** (`sdk/<name>/`): the headers for what Broma doesn't define, listed in order in its `bundle.json`;
- an **STL flavor** (`stl/<name>/`) for each platform: a stand-in for the game's standard library with only the layouts IDA needs, described by a `flavor.json` that lists the `std::` types it provides;
- `#define`s set before the bundle, to switch declarations that differ between versions.

Built-in profiles ship in `profiles/`: `gd-2.208x` (2.208x and older) and `gd-2.209x`. The profile is chosen from the game version in the bindings folder's name (`bindings/2.2091`), or in the settings. Your own profiles go in `<your config folder>/profiles/*.json` (see Settings for where that is); one with the same `name` replaces a built-in. A profile file:

```json
{
  "format": 1,
  "name": "gd-2.209x",
  "title": "Geometry Dash 2.209x",
  "min_version": "2.209",
  "sdk": "legacy",
  "defines": ["BROMAIDA_GD_2209"],
  "stl": { "win": "msvc", "imac": "libcxx-apple", "android64": "libcxx-ndk", "android32": "libcxx-ndk" }
}
```

`"*"` in `stl` covers the platforms not listed. If a flavor isn't installed, the platform's default is used and the log says so. Android from 2.209 on uses libc++ rather than libstdc++ (`libcxx-ndk`, the Apple libc++ model with bionic's `std::mutex`); older versions use `gnustl`. `libcxx-ndk` was checked against the class sizes of real 2.2091 Android binaries (arm64 and arm32): the sizes of classes with `std::` members match, except where the bindings themselves lack members.

## Settings

Settings are stored as JSON in your user configuration folder:

| OS      | File                                                  |
|---------|-------------------------------------------------------|
| Linux   | `~/.config/BromaIDA/settings.json`                    |
| macOS   | `~/Library/Application Support/BromaIDA/settings.json`|
| Windows | `%LOCALAPPDATA%\BromaIDA\settings.json`               |

The generated type header is written to the matching cache folder (`~/.cache/BromaIDA/` on Linux).

The dialog has two groups:

- **General Settings**: overwrite prompts, the input hash check, the export options (return types, argument names, only classes found in the template), unmarked functions, debug logging.
- **Type Import Settings**: import types, set IDAClang's parser arguments for the platform, ignore mismatched structs, the profile, and the type parser (IDA has two: `clang`, the new one, and `old_clang`; Automatic prefers `old_clang`, which handles the layout-only STL stand-ins with far fewer errors, and the log says which was used).

Hover over an option for details. Nothing is saved until you press OK. "Import Types" is greyed out when IDAClang isn't available.

## Building

Presets use clang and Ninja, export `compile_commands.json` and come in `release`, `debug` and `relwithdebinfo` for each platform: `linux`, `macos-arm64`, `macos-x64` and `windows-x64` (clang-cl).

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
cmake --install build/linux-release --prefix ~/.idapro
```

`cmake --list-presets` shows what is available on your machine. The dependencies (Broma, glaze, fmt, asp, result, doctest) and the IDA SDK are fetched through CPM.

**Qt.** IDA ships its own Qt (6.8.2 in IDA 9.4, built inside a `QT` namespace), and the plugin has to be built against the same major.minor: a newer Qt can reference symbols IDA's runtime doesn't have, and the build refuses Qt 6.9 or newer. You only need Qt's headers and `moc`; the plugin uses IDA's copy at run time. A quick way to get it:

```sh
pip install aqtinstall
aqt install-qt linux desktop 6.8.2 linux_gcc_64 --archives qtbase -O ~/Qt
cmake --preset linux-release -DBROMAIDA_QT_DIR=$HOME/Qt/6.8.2/gcc_64
```

CMake also looks in `$QT_ROOT_DIR` (set by `install-qt-action`) and `~/Qt/6.8.*`. On Windows the plugin links the namespaced Qt import libraries shipped in the IDA SDK.

Useful options:

| Option                        | Default          | Meaning                                              |
|-------------------------------|------------------|------------------------------------------------------|
| `BROMAIDA_IDASDK_DIR`         | empty            | Use this SDK checkout; otherwise `$IDASDK`, then a download. |
| `BROMAIDA_IDASDK_TAG`         | `v9.4.0-sdk.1`   | SDK tag to download. Match it to your IDA.           |
| `BROMAIDA_QT_DIR`             | empty            | Qt kit to build against (6.8.x).                     |
| `BROMAIDA_QT_VERSION`         | `6.8`            | Qt major.minor IDA ships; the build requires it.     |
| `BROMAIDA_BUILD_PLUGIN`       | `ON`             | Build the plugin. Turn it off to build only the core and tests. |
| `BROMAIDA_BUILD_TESTS`        | `ON` (top level) | Build the unit tests.                                |
| `BROMAIDA_WARNINGS_AS_ERRORS` | `OFF`            | Treat warnings as errors.                            |

### Generating SDK headers

The bundled headers can drift from the SDK, which leaves classes with wrong vtables, and IDAClang crashes on those. `tools/sdkgen` (off by default, needs libclang) writes the types the bindings use but nothing declares, from a Geode SDK checkout, and reports bundled classes whose virtual functions differ from the SDK's:

```sh
cmake --preset linux-release -DBROMAIDA_BUILD_SDKGEN=ON
cmake --build --preset linux-release --target bromaida_sdkgen
./build/linux-release/tools/sdkgen/bromaida-sdkgen \
    --bindings /path/to/bindings/bindings/2.2091 \
    --sdk /path/to/geode/loader/include \
    --include-dir <headers of Geode's dependencies: asp, result, matjson> \
    --out /tmp/sdkgen --install
```

`--install` writes `data/sdk/legacy/sdk_supplement.hpp`. The tool also checks its own result: the generated section must have no errors and no invalid classes, or it exits with a failure. It supports `android64` and `android32` so far; the SDK's Windows and Apple parts need stub system headers first (see `tools/sdkgen/stubs`).

`tools/layoutcheck` (built with the same option) guards the STL stand-ins: the sizes in `tests/data/sizes` were read from real GD 2.2091 and 2.2081 binaries, and the tool lets libclang lay the generated header out for a platform and compares. The tests run when `BROMAIDA_TEST_BINDINGS` points at a `geode-sdk/bindings` checkout's `bindings` folder, and are skipped otherwise:

```sh
BROMAIDA_TEST_BINDINGS=/path/to/bindings/bindings ctest --preset linux-release -R layout
```

The tests can also run against a real bindings checkout:

```sh
BROMAIDA_TEST_BROMA=/path/to/bindings/bindings/2.2091/Entry.bro ./build/linux-release/tests/bromaida_tests
```

## Acknowledgements

- [Geode](https://github.com/geode-sdk) for the bindings and the [Broma](https://github.com/geode-sdk/Broma) parser.
- The [IDA SDK](https://github.com/HexRaysSA/ida-sdk).
- [CallocGD](https://github.com/CallocGD) and [Prevter](https://github.com/Prevter) for [PyBroma](https://github.com/prevter/PyBroma), which the Python version of this plugin was built on.
- @sleepyut: issuing 3 trillion bug reports, and suggesting a bunch of features. (They also made BromaBJ.)
- [AngelDev06](https://github.com/AngelDev06): contributing features.
