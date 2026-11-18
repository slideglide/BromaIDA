#pragma once

#include <bromaida/settings.hpp>

namespace bromaida::ida {
    /// The settings dialog built from IDA's own form markup. Returns true and updates `settings` on confirm.
    [[nodiscard]] bool edit_settings_native(Settings& settings);
}  // namespace bromaida::ida
