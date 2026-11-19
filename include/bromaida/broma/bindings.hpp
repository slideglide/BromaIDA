#pragma once

#include <cstdint>
#include <map>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <bromaida/platform/platform.hpp>
#include <bromaida/broma/signature.hpp>

namespace bromaida {

    [[nodiscard]] bool is_class_present(Platform platform, std::string_view class_name, std::string_view binary_file);

    [[nodiscard]] bool is_function_present(
        Platform platform, std::string_view name, std::string_view binary_file, std::string_view source_file);

    class BindingSet {
    public:
        std::vector<Binding> bindings;
        std::map<std::uint64_t, std::vector<Binding>> duplicates;

        void add(Binding binding);

        void append(Binding binding);

    private:
        void reindex();

        std::unordered_map<std::uint64_t, std::size_t> m_index;
    };
}
