#include <doctest/doctest.h>

#include <bromaida/broma_io.hpp>
#include <bromaida/bundle.hpp>
#include <bromaida/class_graph.hpp>
#include <bromaida/profile.hpp>

using namespace bromaida;

static Path fresh(std::string_view name) {
    auto dir = Path::tmp().unwrap() / "bromaida-tests" / name;
    (void)asp::fs::removeDirAll(dir, false);
    REQUIRE(asp::fs::createDirAll(dir).isOk());
    return dir;
}

static void write(Path const& file, std::string_view text) {
    if (auto parent = file.parent()) REQUIRE(asp::fs::createDirAll(*parent).isOk());
    REQUIRE(asp::fs::write(file, text).isOk());
}

TEST_CASE("game versions parse the way the bindings write them") {
    CHECK(GdVersion::parse("2.2091") == GdVersion{2, 2091});
    CHECK(GdVersion::parse("2.209") == GdVersion{2, 2090});  // the release before 2.2091
    CHECK(GdVersion::parse("2.2081") == GdVersion{2, 2081});
    CHECK(GdVersion::parse("1.920") == GdVersion{1, 9200});
    CHECK_FALSE(GdVersion::parse("Entry.bro").has_value());

    CHECK(GdVersion{2, 2081} < GdVersion{2, 2090});
    CHECK(GdVersion{2, 2090} < GdVersion{2, 2091});
    CHECK(GdVersion{2, 2090}.to_string() == "2.209");
    CHECK(GdVersion{2, 2091}.to_string() == "2.2091");

    // the folder the user picked, or the folder of the file they picked
    CHECK(GdVersion::guess("/home/me/bindings/bindings/2.2091") == GdVersion{2, 2091});
    CHECK(GdVersion::guess("/home/me/bindings/bindings/2.2081/Entry.bro") == GdVersion{2, 2081});
    CHECK_FALSE(GdVersion::guess("/home/me/stuff/bro-files").has_value());
}

static Profile make(std::string name, std::string low, std::string high) {
    Profile p;
    p.name = std::move(name);
    p.min_version = std::move(low);
    p.max_version = std::move(high);
    return p;
}

TEST_CASE("the profile with the highest lower bound that matches wins") {
    std::vector<Profile> profiles{make("old", "", "2.2089"), make("new", "2.209", ""), make("newer", "2.2091", "")};

    CHECK(select_profile(profiles, {2, 2081})->name == "old");
    CHECK(select_profile(profiles, {2, 2089})->name == "old");
    CHECK(select_profile(profiles, {2, 2090})->name == "new");
    CHECK(select_profile(profiles, {2, 2091})->name == "newer");
    CHECK(select_profile(profiles, {2, 3000})->name == "newer");

    std::vector<Profile> gap{make("only", "2.2", "2.201")};
    CHECK(select_profile(gap, {2, 2090}) == nullptr);
    CHECK(find_profile(profiles, "new") == &profiles[1]);
    CHECK(find_profile(profiles, "missing") == nullptr);
}

TEST_CASE("profile files are validated, and a user profile replaces a built-in of the same name") {
    auto builtin = fresh("profiles-builtin");
    write(
        builtin / "a.json",
        R"({"format":1,"name":"a","title":"Built-in A","max_version":"2.2089","stl":{"win":"msvc","*":"gnustl"}})");
    write(builtin / "b.json", R"({"name":"b","min_version":"2.209","future_key":[1,2,3]})");
    write(builtin / "broken.json", R"({"name":"c","min_version":"2.2091","max_version":"2.2081"})");
    write(builtin / "nonsense.json", "{ not json");
    write(builtin / "nameless.json", R"({"title":"no name"})");
    write(builtin / "readme.txt", "ignored");

    auto set = load_profiles(builtin);
    CHECK(set.profiles.size() == 2);  // a and b; unknown keys are fine
    CHECK(set.errors.size() == 3);    // broken range, not json, no name

    auto user = fresh("profiles-user");
    write(user / "mine.json", R"({"name":"a","title":"My A","min_version":"2.0"})");
    set.merge(load_profiles(user));
    CHECK(set.profiles.size() == 2);
    CHECK(find_profile(set.profiles, "a")->title == "My A");

    CHECK(load_profiles(fresh("profiles-none") / "missing").profiles.empty());

    // "*" is the fallback for platforms the profile doesn't list
    auto const& a = *find_profile(load_profiles(builtin).profiles, "a");
    CHECK(*a.stl_flavor(Platform::Windows) == "msvc");
    CHECK(*a.stl_flavor(Platform::Android64) == "gnustl");
}

TEST_CASE("a bundle names its headers in order and each entry is a file or the enums") {
    auto dir = fresh("bundle-ok");
    write(
        dir / "bundle.json",
        R"({"name":"t","headers":[{"file":"a.hpp"},{"enums":"../x/Enums.hpp"},{"file":"b.hpp","includes":"parse","optional":true}]})");
    auto bundle = load_bundle(dir);
    REQUIRE(bundle.isOk());
    auto const& entries = bundle.unwrap().manifest.headers;
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].file == "a.hpp");
    CHECK(entries[1].enums == "../x/Enums.hpp");
    CHECK(entries[2].includes == "parse");
    CHECK(entries[2].optional);

    auto both = fresh("bundle-both");
    write(both / "bundle.json", R"({"name":"t","headers":[{"file":"a.hpp","enums":"x"}]})");
    CHECK(load_bundle(both).isErr());
    auto neither = fresh("bundle-neither");
    write(neither / "bundle.json", R"({"name":"t","headers":[{}]})");
    CHECK(load_bundle(neither).isErr());
    CHECK(load_bundle(fresh("bundle-missing")).isErr());
}

TEST_CASE("a flavor the profile asks for but isn't installed falls back to the platform default") {
    auto root = fresh("flavors");
    write(root / "gnustl" / "flavor.json", R"({"name":"gnustl","provides":["std::vector","std::string"]})");
    write(root / "msvc" / "flavor.json", R"({"name":"msvc","provides":["std::vector","std::mutex"]})");

    Profile profile = make("p", "", "");
    profile.stl = {{"android64", "libcxx-ndk"}, {"win", "msvc"}};

    auto android = resolve_flavor(profile, Platform::Android64, root);
    REQUIRE(android.flavor.isOk());
    CHECK(android.requested == "libcxx-ndk");
    CHECK(android.fell_back);
    CHECK(android.flavor.unwrap().manifest.name == "gnustl");

    auto windows = resolve_flavor(profile, Platform::Windows, root);
    REQUIRE(windows.flavor.isOk());
    CHECK_FALSE(windows.fell_back);
    CHECK(windows.flavor.unwrap().has("std::mutex"));

    // nothing installed for macOS: that is an error, not a silent guess
    CHECK(resolve_flavor(profile, Platform::MacArm, root).flavor.isErr());
}

TEST_CASE("std types the classes use are found, nested ones too, and compared against the flavor") {
    auto root = parse_broma(R"(
class Holder {
    std::vector<std::pair<std::string, int>> m_pairs;
    std::mutex m_lock;
    std::atomic_bool m_busy;
    std::unordered_map<int, std::list<float>> m_lists;
    int m_plain;
}
)")
                    .unwrap();
    std::vector<broma::Class const*> classes{&root.classes.front()};
    ClassGraph graph{classes, Platform::Windows};

    auto const used = graph.used_std_types();
    for (char const* name :
         {"std::vector", "std::pair", "std::string", "std::mutex", "std::atomic_bool", "std::unordered_map",
          "std::list"}) {
        CAPTURE(name);
        CHECK(used.contains(name));
    }
    CHECK_FALSE(used.contains("int"));

    Flavor flavor;
    flavor.provides = {"std::vector", "std::pair", "std::string", "std::unordered_map"};
    auto const missing = missing_std_types(used, flavor);
    CHECK(missing == std::vector<std::string>{"std::atomic_bool", "std::list", "std::mutex"});

    flavor.provides.insert({"std::atomic_bool", "std::list", "std::mutex"});
    CHECK(missing_std_types(used, flavor).empty());
}
