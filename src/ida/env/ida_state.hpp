#pragma once

#include <optional>
#include <string>

namespace bromaida::ida {
    [[nodiscard]] std::optional<std::string> load_types_digest();
    void save_types_digest(std::string const& digest);
}  // namespace bromaida::ida
