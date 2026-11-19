#include <doctest/doctest.h>

#include <bromaida/broma/broma_io.hpp>
#include <bromaida/typegen/bundle.hpp>
#include <bromaida/typegen/class_graph.hpp>
#include <bromaida/typegen/profile.hpp>

#include "common.hpp"

using namespace bromaida;

TEST_CASE("game versions parse the way the bindings write them") {
    CHECK(GdVersion::parse("2.2091") == GdVersion{2, 2091});
    CHECK(GdVersion::parse("2.209") == GdVersion{2, 2090});
    CHECK(GdVersion::parse("2.2081") == GdVersion{2, 2081});
    CHECK(GdVersion::parse("1.920") == GdVersion{1, 9200});
    CHECK(GdVersion::parse("v2.209-beta") == GdVersion{2, 2090});
    CHECK(GdVersion::parse("release 10.5") == GdVersion{10, 5000});
    CHECK(GdVersion::parse("2.123456") == GdVersion{2, 1234});
    CHECK(GdVersion::parse("abc2.x3.4") == GdVersion{3, 4000});
    CHECK(GdVersion::parse("(x86) 2.2091") == GdVersion{2, 2091});

    CHECK_FALSE(GdVersion::parse("Entry.bro").has_value());
    CHECK_FALSE(GdVersion::parse("2.").has_value());
    CHECK_FALSE(GdVersion::parse(".5").has_value());
    CHECK_FALSE(GdVersion::parse("2").has_value());
    CHECK_FALSE(GdVersion::parse("").has_value());

    CHECK(GdVersion{2, 2081} < GdVersion{2, 2090});
    CHECK(GdVersion{2, 2090} < GdVersion{2, 2091});
    CHECK(GdVersion{1, 9999} < GdVersion{2, 0});
    CHECK(GdVersion{2, 2090}.to_string() == "2.209");
    CHECK(GdVersion{2, 2091}.to_string() == "2.2091");
    CHECK(GdVersion{2, 2000}.to_string() == "2.200");
}

TEST_CASE("a version is found in the last path components with either separator") {
    CHECK(GdVersion::guess("/home/me/bindings/bindings/2.2091") == GdVersion{2, 2091});
    CHECK(GdVersion::guess("/home/me/bindings/bindings/2.2081/Entry.bro") == GdVersion{2, 2081});
    CHECK(GdVersion::guess("C:\\Users\\me\\bindings\\2.2091\\Entry.bro") == GdVersion{2, 2091});
    CHECK(GdVersion::guess("C:\\Program Files (x86)\\bindings\\2.2091") == GdVersion{2, 2091});
    CHECK(GdVersion::guess("/home/me/bindings/2.2091/") == GdVersion{2, 2091});
    CHECK_FALSE(GdVersion::guess("/home/me/stuff/bro-files").has_value());
    CHECK_FALSE(GdVersion::guess("").has_value());
}

static Profile make(std::string name, std::string low, std::string high) {
    Profile p;
    p.name = std::move(name);
    p.min_version = std::move(low);
    p.max_version = std::move(high);
    return p;
}

TEST_CASE("a profile's version range is inclusive at both ends") {
    auto const p = make("p", "2.209", "2.2091");
    CHECK_FALSE(p.matches({2, 2089}));
    CHECK(p.matches({2, 2090}));
    CHECK(p.matches({2, 2091}));
    CHECK_FALSE(p.matches({2, 2092}));
    CHECK(make("open", "", "").matches({0, 0}));
    CHECK(make("open", "", "").matches({99, 9999}));
}

TEST_CASE("the profile with the highest lower bound that matches wins") {
    std::vector<Profile> profiles{make("old", "", "2.2089"), make("new", "2.209", ""), make("newer", "2.2091", "")};

    auto pick = [&](GdVersion v) {
        auto const* p = select_profile(profiles, v);
        REQUIRE(p != nullptr);
        return p->name;
    };
    CHECK(pick({2, 2081}) == "old");
    CHECK(pick({2, 2089}) == "old");
    CHECK(pick({2, 2090}) == "new");
    CHECK(pick({2, 2091}) == "newer");
    CHECK(pick({2, 3000}) == "newer");

    std::vector<Profile> gap{make("only", "2.2", "2.201")};
    CHECK(select_profile(gap, {2, 2090}) == nullptr);
    CHECK(select_profile(std::span<Profile const>{}, {2, 2090}) == nullptr);
    CHECK(find_profile(profiles, "new") == &profiles[1]);
    CHECK(find_profile(profiles, "missing") == nullptr);

    std::vector<Profile> tie{make("first", "2.2", ""), make("second", "2.2", "")};
    REQUIRE(select_profile(tie, {2, 2090}) != nullptr);
    CHECK(select_profile(tie, {2, 2090})->name == "first");
}

TEST_CASE("profile files are validated one by one with a message naming the file") {
    auto dir = fresh_dir("profiles-validated");
    write_file(
        dir / "a.json",
        R"({"format":1,"name":"a","title":"Built-in A","max_version":"2.2089","stl":{"win":"msvc","*":"gnustl"}})");
    write_file(dir / "b.json", R"({"name":"b","min_version":"2.209","future_key":[1,2,3]})");
    write_file(dir / "broken.json", R"({"name":"c","min_version":"2.2091","max_version":"2.2081"})");
    write_file(dir / "nonsense.json", "{ not json");
    write_file(dir / "nameless.json", R"({"title":"no name"})");
    write_file(dir / "badversion.json", R"({"name":"d","min_version":"soon"})");
    write_file(dir / "wrongtype.json", R"({"name":5})");
    write_file(dir / "readme.txt", "ignored");

    auto set = load_profiles(dir);
    REQUIRE(set.profiles.size() == 2);
    CHECK(set.profiles[0].name == "a");
    CHECK(set.profiles[1].name == "b");
    CHECK(set.profiles[1].min_version == "2.209");

    // list order is by file name: badversion, broken, nameless, nonsense, wrongtype
    REQUIRE(set.errors.size() == 5);
    CHECK(set.errors[0].contains("badversion.json"));
    CHECK(set.errors[0].contains("min_version"));
    CHECK(set.errors[1].contains("broken.json"));
    CHECK(set.errors[1].contains("lower than"));
    CHECK(set.errors[2].contains("nameless.json"));
    CHECK(set.errors[2].contains("name"));
    CHECK(set.errors[3].contains("nonsense.json"));
    CHECK(set.errors[4].contains("wrongtype.json"));
}

TEST_CASE("a user profile replaces a built-in of the same name and the others stay") {
    auto builtin = fresh_dir("profiles-builtin");
    write_file(builtin / "a.json", R"({"name":"a","title":"Built-in A","stl":{"win":"msvc","*":"gnustl"}})");
    write_file(builtin / "b.json", R"({"name":"b","title":"Built-in B"})");
    auto set = load_profiles(builtin);

    auto user = fresh_dir("profiles-user");
    write_file(user / "mine.json", R"({"name":"a","title":"My A","min_version":"2.0"})");
    write_file(user / "extra.json", R"({"name":"c","title":"Extra"})");
    set.merge(load_profiles(user));

    REQUIRE(set.profiles.size() == 3);
    CHECK(set.profiles[0].name == "a");
    CHECK(set.profiles[0].title == "My A");
    CHECK(set.profiles[0].min_version == "2.0");
    CHECK(set.profiles[0].stl.empty());
    CHECK(set.profiles[1].name == "b");
    CHECK(set.profiles[1].title == "Built-in B");
    CHECK(set.profiles[2].name == "c");
    CHECK(set.errors.empty());
}

TEST_CASE("a missing profiles folder is not an error, a file in its place gives an empty set") {
    auto dir = fresh_dir("profiles-none");
    auto missing = load_profiles(dir / "missing");
    CHECK(missing.profiles.empty());
    CHECK(missing.errors.empty());

    write_file(dir / "file.json", R"({"name":"x"})");
    auto not_a_folder = load_profiles(dir / "file.json");
    CHECK(not_a_folder.profiles.empty());
    CHECK(not_a_folder.errors.empty());
}

TEST_CASE("a profile names its STL flavor per platform with a wildcard fallback") {
    auto dir = fresh_dir("profiles-stl");
    write_file(dir / "a.json", R"({"name":"a","stl":{"win":"msvc","*":"gnustl"}})");
    write_file(dir / "b.json", R"({"name":"b","stl":{"win":"msvc"}})");
    auto set = load_profiles(dir);
    REQUIRE(set.profiles.size() == 2);

    auto const& a = set.profiles[0];
    REQUIRE(a.stl_flavor(Platform::Windows) != nullptr);
    CHECK(*a.stl_flavor(Platform::Windows) == "msvc");
    REQUIRE(a.stl_flavor(Platform::Android64) != nullptr);
    CHECK(*a.stl_flavor(Platform::Android64) == "gnustl");
    REQUIRE(a.stl_flavor(Platform::MacArm) != nullptr);
    CHECK(*a.stl_flavor(Platform::MacArm) == "gnustl");

    CHECK(set.profiles[1].stl_flavor(Platform::Android64) == nullptr);
}

TEST_CASE("a bundle names its headers in order and each entry is a file or the enums") {
    auto dir = fresh_dir("bundle-ok");
    write_file(
        dir / "bundle.json",
        R"({"name":"t","headers":[{"file":"a.hpp"},{"enums":"../x/Enums.hpp"},{"file":"b.hpp","includes":"parse","optional":true}]})");
    auto bundle = load_bundle(dir);
    REQUIRE(bundle.isOk());
    auto const& entries = bundle.unwrap().manifest.headers;
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].file == "a.hpp");
    CHECK(entries[0].enums.empty());
    CHECK_FALSE(entries[0].optional);
    CHECK(entries[1].enums == "../x/Enums.hpp");
    CHECK(entries[1].file.empty());
    CHECK(entries[2].file == "b.hpp");
    CHECK(entries[2].includes == "parse");
    CHECK(entries[2].optional);
}

TEST_CASE("a bundle is rejected for a bad entry, bad JSON or a wrong schema") {
    struct Bad {
        std::string_view name;
        std::string_view json;
    };
    for (auto const& bad : {
             Bad{"both", R"({"name":"t","headers":[{"file":"a.hpp","enums":"x"}]})"},
             Bad{"neither", R"({"name":"t","headers":[{}]})"},
             Bad{"not-json", "{ nope"},
             Bad{"headers-not-a-list", R"({"name":"t","headers":"a.hpp"})"},
             Bad{"file-not-a-string", R"({"name":"t","headers":[{"file":3}]})"},
             Bad{"empty", ""},
         }) {
        CAPTURE(bad.name);
        auto dir = fresh_dir(std::string{"bundle-bad-"} + std::string{bad.name});
        write_file(dir / "bundle.json", bad.json);
        auto loaded = load_bundle(dir);
        REQUIRE(loaded.isErr());
        CHECK_FALSE(loaded.unwrapErr().empty());
    }
    CHECK(load_bundle(fresh_dir("bundle-missing")).isErr());
}

TEST_CASE("a flavor the profile asks for but isn't installed falls back to the platform default") {
    auto root = fresh_dir("flavors");
    write_file(root / "gnustl" / "flavor.json", R"({"name":"gnustl","provides":["std::vector","std::string"]})");
    write_file(root / "msvc" / "flavor.json", R"({"name":"msvc","provides":["std::vector","std::mutex"]})");

    Profile profile = make("p", "", "");
    profile.stl = {{"android64", "libcxx-ndk"}, {"win", "msvc"}};

    auto android = resolve_flavor(profile, Platform::Android64, root);
    REQUIRE(android.flavor.isOk());
    CHECK(android.requested == "libcxx-ndk");
    CHECK(android.fell_back);
    CHECK(android.flavor.unwrap().manifest.name == "gnustl");

    auto windows = resolve_flavor(profile, Platform::Windows, root);
    REQUIRE(windows.flavor.isOk());
    CHECK(windows.requested == "msvc");
    CHECK_FALSE(windows.fell_back);
    CHECK(windows.flavor.unwrap().has("std::mutex"));
    CHECK_FALSE(windows.flavor.unwrap().has("std::string"));

    // the platform default (libcxx-apple) isn't installed either: nothing to fall back to
    auto mac = resolve_flavor(profile, Platform::MacArm, root);
    CHECK(mac.requested == "libcxx-apple");
    CHECK(mac.flavor.isErr());
}

TEST_CASE("a flavor with broken JSON is an error rather than an empty flavor") {
    auto root = fresh_dir("flavor-broken");
    write_file(root / "msvc" / "flavor.json", "{ \"name\": ");
    CHECK(load_flavor(root / "msvc").isErr());
    CHECK(load_flavor(root / "absent").isErr());
}

TEST_CASE("std types the classes use are found, nested ones too, and compared against the flavor") {
    auto parsed = parse_broma(R"(
class Holder {
    std::vector<std::pair<std::string, int>> m_pairs;
    std::mutex m_lock;
    std::atomic_bool m_busy;
    std::unordered_map<int, std::list<float>> m_lists;
    int m_plain;
}
)");
    REQUIRE(parsed.isOk());
    auto root = std::move(parsed).unwrap();
    std::vector<broma::Class const*> classes{&root.classes.front()};
    ClassGraph graph{classes, Platform::Windows};

    auto const used = graph.used_std_types();
    CHECK(
        used == std::set<std::string>{
                    "std::atomic_bool", "std::list", "std::mutex", "std::pair", "std::string", "std::unordered_map",
                    "std::vector"});

    Flavor flavor;
    flavor.provides = {"std::vector", "std::pair", "std::string", "std::unordered_map"};
    CHECK(missing_std_types(used, flavor) == Strings{"std::atomic_bool", "std::list", "std::mutex"});

    flavor.provides.insert({"std::atomic_bool", "std::list", "std::mutex"});
    CHECK(missing_std_types(used, flavor).empty());
}
