#include <bromaida/settings.hpp>

#include <cstdlib>
#include <optional>

#include <asp/fs.hpp>
#include <fmt/format.h>
#include <glaze/glaze.hpp>

#include <bromaida/version.hpp>

namespace bromaida {

// a settings file written by a newer version may contain keys we don't know
static constexpr glz::opts kReadOptions{.error_on_unknown_keys = false};

static std::optional<Path> env_path(char const* name) {
    if (auto const* value = std::getenv(name); value && *value) return Path{value};
    return std::nullopt;
}

static Path home_dir() {
    if (auto home = env_path("HOME")) return *home;
    return Path::home().unwrapOr(Path{"."});
}

Path user_config_dir() {
#if defined(_WIN32)
    auto base = env_path("LOCALAPPDATA").value_or(home_dir() / "AppData" / "Local");
#elif defined(__APPLE__)
    auto base = home_dir() / "Library" / "Application Support";
#else
    auto base = env_path("XDG_CONFIG_HOME").value_or(home_dir() / ".config");
#endif
    return base / kPluginName;
}

Path user_cache_dir() {
#if defined(_WIN32)
    auto base = env_path("LOCALAPPDATA").value_or(home_dir() / "AppData" / "Local");
    return base / kPluginName / "Cache";
#elif defined(__APPLE__)
    return home_dir() / "Library" / "Caches" / kPluginName;
#else
    return env_path("XDG_CACHE_HOME").value_or(home_dir() / ".cache") / kPluginName;
#endif
}

geode::Result<Settings, std::string> Settings::load(PathView file) {
    if (!file.exists()) return geode::Ok(Settings{});  // first run: defaults

    auto text = asp::fs::readToString(file);
    if (text.isErr()) return geode::Err(fmt::format("cannot read {}: {}", file.string(), text.unwrapErr().message()));

    Settings settings;
    if (auto err = glz::read<kReadOptions>(settings, text.unwrap())) {
        return geode::Err(fmt::format("{}: {}", file.string(), glz::format_error(err, text.unwrap())));
    }
    return geode::Ok(std::move(settings));
}

geode::Result<void, std::string> Settings::save(PathView file) const {
    if (auto parent = file.parent()) {
        if (auto made = asp::fs::createDirAll(*parent); made.isErr()) {
            return geode::Err(fmt::format("cannot create {}: {}", parent->string(), made.unwrapErr().message()));
        }
    }

    std::string json;
    if (auto err = glz::write<glz::opts{.prettify = true}>(*this, json)) {
        return geode::Err(fmt::format("cannot serialize settings: {}", glz::format_error(err, json)));
    }

    // write next to the target first so a crash never leaves a half-written file
    auto temp = file.toPath();
    temp += ".tmp";
    if (auto written = asp::fs::write(temp, json); written.isErr()) {
        return geode::Err(fmt::format("cannot write {}: {}", temp.string(), written.unwrapErr().message()));
    }
    if (auto renamed = asp::fs::rename(temp, file); renamed.isErr()) {
        return geode::Err(fmt::format("cannot replace {}: {}", file.string(), renamed.unwrapErr().message()));
    }
    return geode::Ok();
}

}  // namespace bromaida
