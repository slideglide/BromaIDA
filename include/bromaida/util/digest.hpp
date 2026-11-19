#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace bromaida {

    [[nodiscard]] std::string digest_hex(std::initializer_list<std::string_view> parts);
}
