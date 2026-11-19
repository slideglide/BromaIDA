#pragma once

#include <optional>
#include <string>

#include <pro.h>

#include <bromaida/platform/binary_info.hpp>
#include <bromaida/util/path.hpp>

namespace bromaida::ida {
    [[nodiscard]] inline std::string to_std(qstring const& s) { return {s.c_str(), s.length()}; }

    [[nodiscard]] BinaryInfo read_binary_info();

    [[nodiscard]] std::string root_filename();

    [[nodiscard]] bool analysis_finished();

    [[nodiscard]] Path plugin_data_dir();

    [[nodiscard]] bool is_gui();

    [[nodiscard]] std::optional<std::string> select_clang_parser(std::string_view preferred = {});

    void install_log_sink();
}  // namespace bromaida::ida
