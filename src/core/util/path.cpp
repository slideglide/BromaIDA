#include <bromaida/util/path.hpp>

namespace bromaida {
    Path parent_dir(PathView path) {
        if (auto parent = path.parent(); parent && !parent->empty()) return parent->toPath();
        return Path{"."};
    }
}  // namespace bromaida
