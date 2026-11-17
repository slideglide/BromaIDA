#pragma once

#include <optional>
#include <string>

namespace bromaida::ida {

/// Data tied to the open database, kept in a netnode so it travels with the IDB.
/// The layout matches what the Python plugin wrote.
struct IdbState {
    std::optional<std::string> types_digest;
    std::optional<std::string> types_platform;

    [[nodiscard]] static IdbState load();
    void save() const;
    static void clear();
};

}  // namespace bromaida::ida
