#pragma once

#include <asp/fs.hpp>

namespace bromaida {
    using Path = asp::fs::Path;
    using PathView = asp::fs::PathView;

    /// @param path A file path.
    /// @return The folder it is in, or "." for a bare file name.
    [[nodiscard]] inline Path parent_dir(PathView path) {
        if (auto parent = path.parent(); parent && !parent->empty()) return parent->toPath();
        return Path{"."};
    }
}  // namespace bromaida
