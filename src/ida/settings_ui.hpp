#pragma once

#include <bromaida/settings.hpp>

namespace bromaida::ida {

/// The settings dialog built from IDA's own form markup, for the text version of IDA and for a Qt we can't trust.
/// Returns true and updates `settings` when the user confirms.
[[nodiscard]] bool edit_settings_native(Settings& settings);

}  // namespace bromaida::ida
