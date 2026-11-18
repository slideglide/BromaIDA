#pragma once

#include <asp/fs.hpp>

namespace bromaida {
    /// Every path in BromaIDA is a UTF-8 asp path. Only where an API insists on std::filesystem
    /// (Broma's parser) it is converted with `toStdPath()` / `fromStdPath()` from <asp/fs/std.hpp>.
    using Path = asp::fs::Path;
    using PathView = asp::fs::PathView;

    /// The folder a path lives in; "." for a bare file name.
    [[nodiscard]] inline Path parent_dir(PathView path) {
        if (auto parent = path.parent(); parent && !parent->empty()) return parent->toPath();
        return Path{"."};
    }
}  // namespace bromaida
