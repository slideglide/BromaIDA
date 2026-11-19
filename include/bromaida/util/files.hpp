#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <Geode/Result.hpp>

#include <bromaida/util/path.hpp>

namespace bromaida {

    [[nodiscard]] geode::Result<std::vector<Path>, std::string> list_files(PathView folder, std::string_view extension);

    [[nodiscard]] geode::Result<void, std::string> write_atomic(PathView file, std::string_view content);
}
