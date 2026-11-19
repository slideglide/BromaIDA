#pragma once

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include <bromaida/util/path.hpp>

using Strings = std::vector<std::string>;

struct Case {
    std::string_view input;
    std::string_view expected;
};

inline bromaida::Path fresh_dir(std::string_view name) {
    auto dir = bromaida::Path::tmp().unwrap() / "bromaida-tests" / name;
    (void)asp::fs::removeDirAll(dir, false);
    REQUIRE(asp::fs::createDirAll(dir).isOk());
    return dir;
}

inline void write_file(bromaida::Path const& file, std::string_view text) {
    if (auto parent = file.parent()) REQUIRE(asp::fs::createDirAll(*parent).isOk());
    REQUIRE(asp::fs::write(file, text).isOk());
}

inline std::size_t count_of(std::string_view text, std::string_view what) {
    std::size_t n = 0;
    for (auto at = text.find(what); at != std::string_view::npos; at = text.find(what, at + what.size()))
        ++n;
    return n;
}
