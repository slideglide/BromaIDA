#pragma once

#include <asp/fs.hpp>

namespace bromaida {
    using Path = asp::fs::Path;
    using PathView = asp::fs::PathView;

    [[nodiscard]] Path parent_dir(PathView path);
}
