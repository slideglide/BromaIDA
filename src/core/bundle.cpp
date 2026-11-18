#include <bromaida/bundle.hpp>

#include <fmt/format.h>
#include <glaze/glaze.hpp>

namespace bromaida {

template <class T>
static geode::Result<T, std::string> read_json(PathView file) {
    auto text = asp::fs::readToString(file);
    if (text.isErr()) return geode::Err(fmt::format("cannot read {}: {}", file.string(), text.unwrapErr().message()));

    T value;
    if (auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(value, text.unwrap())) {
        return geode::Err(fmt::format("{}: {}", file.string(), glz::format_error(err, text.unwrap())));
    }
    return geode::Ok(std::move(value));
}

geode::Result<Bundle, std::string> load_bundle(PathView dir) {
    auto manifest = read_json<BundleManifest>(dir / "bundle.json");
    if (manifest.isErr()) return geode::Err(manifest.unwrapErr());

    auto bundle = Bundle{std::move(manifest).unwrap(), dir.toPath()};
    for (auto const& entry : bundle.manifest.headers) {
        if (entry.file.empty() == entry.enums.empty()) {
            return geode::Err(fmt::format("{}: every header needs exactly one of \"file\" or \"enums\"", dir.string()));
        }
    }
    return geode::Ok(std::move(bundle));
}

geode::Result<Flavor, std::string> load_flavor(PathView dir) {
    auto manifest = read_json<FlavorManifest>(dir / "flavor.json");
    if (manifest.isErr()) return geode::Err(manifest.unwrapErr());

    Flavor flavor;
    flavor.manifest = std::move(manifest).unwrap();
    flavor.dir = dir.toPath();
    flavor.provides.insert(flavor.manifest.provides.begin(), flavor.manifest.provides.end());
    return geode::Ok(std::move(flavor));
}

std::vector<std::string> missing_std_types(std::set<std::string> const& used, Flavor const& flavor) {
    std::vector<std::string> missing;
    for (auto const& type : used) {
        if (!flavor.has(type)) missing.push_back(type);
    }
    return missing;  // `used` is ordered, so this is too
}

std::string_view default_flavor(Platform platform) noexcept {
    if (platform == Platform::Windows) return "msvc";
    if (is_android(platform)) return "gnustl";
    return "libcxx-apple";
}

FlavorChoice resolve_flavor(Profile const& profile, Platform platform, PathView stl_root) {
    FlavorChoice choice;
    auto const* wanted = profile.stl_flavor(platform);
    choice.requested = wanted ? *wanted : std::string{default_flavor(platform)};

    choice.flavor = load_flavor(stl_root / choice.requested);
    if (choice.flavor.isErr() && choice.requested != default_flavor(platform)) {
        choice.fell_back = true;
        choice.flavor = load_flavor(stl_root / default_flavor(platform));
    }
    return choice;
}

}  // namespace bromaida
