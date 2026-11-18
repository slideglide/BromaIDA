# BromaIDA

Broma support for IDA. It reads the Broma files from the [Geode bindings](https://github.com/geode-sdk/bindings) and applies them to your IDB: names, signatures, types and vtables. It can also export what a database knows back into Broma.

It's a native C++23 plugin built on the IDA SDK and the official [Broma parser](https://github.com/geode-sdk/Broma).
> [!WARNING]
> The C++ rewrite is experimental. It has run in IDA and passes its unit tests (including against the real 2.2091 bindings), but expect rough edges and please report them.

## Features

- **Import bindings**: names functions at the addresses Broma gives, merges functions that share an address, and resolves symbols that are already named in the binary (Android, libraries). It asks before overwriting a name or comment you may have written.
- **Import types** (needs IDAClang): class members, vtables, return and argument types, argument names, hidden return pointers and `this`. Also fixes signatures IDA guessed wrong.
- **Export bindings**: writes addresses into an existing `.bro` file and everything it includes without touching comments or formatting. Creates missing methods and classes, and can write return types and argument names. Reads exported Itanium symbols (Android) and names from an earlier import.
- **Export current function**: right-click in the disassembly or pseudocode view.

## Requirements

- IDA Pro 9.4, and a plugin built against the matching SDK.
- IDAClang, only for type import.
- To build: CMake 3.25+, Ninja, a C++23 compiler (the presets use clang) and Qt 6.8.

## Installation

Build it or grab a release, then put these next to each other in IDA's plugin folder:

```
plugins/bromaida.so          (.dll on Windows, .dylib on macOS)
plugins/bromaida/            profiles/, sdk/ and stl/
```

The plugin folder is `~/.idapro/plugins/` on Linux and macOS, `%APPDATA%\Hex-Rays\IDA Pro\plugins\` on Windows, or `<IDA install dir>/plugins/`. `IDAUSR` works too. Restart IDA afterwards.

Without the `bromaida` folder the plugin loads but type import refuses to run. If you're updating from an older build, delete the old `bromaida/types` folder.

## Usage

Open **Edit → Plugins → BromaIDA** or press `Ctrl+Shift+B`, then pick Import or Export. Wait for the `AU: idle` indicator first; the plugin won't start while auto-analysis is running.

**Import** asks for the folder with your `.bro` files, e.g. `bindings/2.2091`. If it has an `Entry.bro`, that's used and pulls in the rest through includes. Otherwise every `.bro` in the folder is merged (subfolders aren't searched), and a class defined in two files is reported and taken from the first. Enums come from `include/Geode/Enums.hpp` next to the folder.

**Export** asks for the `.bro` file to write into. A missing file is created.

The platform is detected from the binary: PE is Windows, Mach-O is macOS Intel, macOS ARM or iOS, and ELF is Android 32 or 64 by bitness.

The dialogs are Qt. In IDA's text version, or if IDA's Qt isn't the 6.8 the plugin was built against, it falls back to IDA's own dialogs.

### Scripting

For batch jobs and idalib, set `BROMAIDA_FILE` and run the plugin with argument `1` (import a folder or a single `.bro`) or `2` (export into that `.bro`):

```python
import ida_loader
ida_loader.load_and_run_plugin("bromaida", 1)
```

## Importing types

Type import generates one header from the Broma classes, adds headers for everything Broma doesn't define (Cocos2d-x, FMOD, helpers) and hands it to IDAClang. Do it once per database. It can take a few minutes and may freeze IDA meanwhile.

- You don't need to supply any headers. What gets used depends on the game version, through a [profile](#profiles).
- Before importing, BromaIDA checks that its STL stand-in declares every `std::` type the bindings use. IDAClang crashes on a member of an undeclared type, so if one is missing you're asked whether to skip.
- Types go into a `BromaIDA` folder in Local Types. A re-import clears it first.
- The header's digest is stored in the IDB, so an unchanged input skips the type step (can be disabled in settings).
- Tuned for recent Geometry Dash versions. Older bindings may need manual fixing.

If a previous import left broken structs behind, delete the `BromaIDA` folder in Local Types, save the IDB and import again.

## Profiles

The game changed between versions (Cocos2d-x vtables, the standard library it's built with), so a type import needs different pieces per version. A profile names them:

- an **SDK bundle** (`sdk/<name>/`): headers for what Broma doesn't define, ordered in its `bundle.json`
- an **STL flavor** (`stl/<name>/`) per platform: a stand-in for the game's standard library with only the layouts IDA needs, listing the `std::` types it provides in `flavor.json`
- `#define`s set before the bundle

Built-in profiles are `gd-2.208x` (and older) and `gd-2.209x`, picked from the version in the bindings folder name or in settings. Your own go in `<config folder>/profiles/*.json`; one with the same `name` replaces a built-in.

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

`"*"` in `stl` covers unlisted platforms. If a flavor isn't installed, the platform default is used and the log says so. Android from 2.209 uses libc++ (`libcxx-ndk`); older versions use `gnustl`.

## Settings

Settings live as JSON in your config folder:

| OS      | File                                                   |
|---------|--------------------------------------------------------|
| Linux   | `~/.config/BromaIDA/settings.json`                     |
| macOS   | `~/Library/Application Support/BromaIDA/settings.json` |
| Windows | `%LOCALAPPDATA%\BromaIDA\settings.json`                |

The generated header goes to the matching cache folder (`~/.cache/BromaIDA/` on Linux). Hover over an option in the dialog for details. Nothing is saved until you press OK.

## Building

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
cmake --install build/linux-release --prefix ~/.idapro
```

Presets exist for `linux`, `macos-arm64`, `macos-x64` and `windows-x64` (clang-cl), each in `release`, `debug` and `relwithdebinfo`. `cmake --list-presets` shows what your machine supports. Dependencies and the IDA SDK are fetched through CPM.

**Qt.** IDA ships Qt 6.8.2 in a `QT` namespace, and the plugin must be built against the same major.minor (the build rejects 6.9+). You only need the headers and `moc`; IDA's copy is used at run time:

```sh
pip install aqtinstall
aqt install-qt linux desktop 6.8.2 linux_gcc_64 --archives qtbase -O ~/Qt
cmake --preset linux-release -DBROMAIDA_QT_DIR=$HOME/Qt/6.8.2/gcc_64
```

CMake also checks `$QT_ROOT_DIR` and `~/Qt/6.8.*`. On Windows it links the namespaced Qt import libraries from the IDA SDK.

| Option                        | Default          | Meaning                                             |
|-------------------------------|------------------|-----------------------------------------------------|
| `BROMAIDA_IDASDK_DIR`         | empty            | SDK checkout to use; otherwise `$IDASDK`, then a download |
| `BROMAIDA_IDASDK_TAG`         | `v9.4.0-sdk.1`   | SDK tag to download; match your IDA                 |
| `BROMAIDA_QT_DIR`             | empty            | Qt kit to build against                             |
| `BROMAIDA_QT_VERSION`         | `6.8`            | Qt major.minor IDA ships                            |
| `BROMAIDA_BUILD_PLUGIN`       | `ON`             | Turn off to build only the core and tests           |
| `BROMAIDA_BUILD_TESTS`        | `ON` (top level) | Build the unit tests                                |
| `BROMAIDA_WARNINGS_AS_ERRORS` | `OFF`            | Treat warnings as errors                            |

To run the tests against a real bindings checkout:

```sh
BROMAIDA_TEST_BROMA=/path/to/bindings/bindings/2.2091/Entry.bro ./build/linux-release/tests/bromaida_tests
```

### Tools

Both need libclang and `-DBROMAIDA_BUILD_SDKGEN=ON`.

`tools/sdkgen` generates the types the bindings use but nothing declares, from a Geode SDK checkout, and reports bundled classes whose virtual functions differ from the SDK's. Wrong vtables make IDAClang crash. It supports `android64` and `android32` so far.

```sh
./build/linux-release/tools/sdkgen/bromaida-sdkgen \
    --bindings /path/to/bindings/bindings/2.2091 \
    --sdk /path/to/geode/loader/include \
    --include-dir <headers of Geode's dependencies: asp, result, matjson> \
    --out /tmp/sdkgen --install
```

`tools/layoutcheck` checks the STL stand-ins against class sizes read from real GD 2.2091 and 2.2081 binaries (`tests/data/sizes`). Those tests run when `BROMAIDA_TEST_BINDINGS` points at a bindings checkout's `bindings` folder:

```sh
BROMAIDA_TEST_BINDINGS=/path/to/bindings/bindings ctest --preset linux-release -R layout
```

## Acknowledgements

- [Geode](https://github.com/geode-sdk) for the bindings and the [Broma](https://github.com/geode-sdk/Broma) parser.
- The [IDA SDK](https://github.com/HexRaysSA/ida-sdk).
- [CallocGD](https://github.com/CallocGD) and [Prevter](https://github.com/Prevter) for [PyBroma](https://github.com/prevter/PyBroma), which the Python version was built on.
- @sleepyut: issuing 3 trillion bug reports, and suggesting a bunch of features. (They also made BromaBJ.)
- [AngelDev06](https://github.com/AngelDev06): contributing features.
