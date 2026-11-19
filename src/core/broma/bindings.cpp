#include <bromaida/broma/bindings.hpp>

#include <fmt/format.h>

#include <bromaida/util/log.hpp>
#include <bromaida/util/text.hpp>

namespace bromaida {
    static constexpr Log log{"Bindings"};

    static bool is_fmod_class(std::string_view name) { return name.starts_with("FMOD::") || name == "FMOD"; }

    bool is_class_present(Platform platform, std::string_view class_name, std::string_view binary_file) {
        auto const binary = cpp::lower_ascii(binary_file);

        if (platform == Platform::Windows) {
            // taken from Geode's bindings codegen as of GD 2.2082
            bool const is_cocos = class_name.starts_with("cocos2d::") || class_name == "cocos2d" ||
                                  class_name.starts_with("pugi::") || class_name == "pugi" ||
                                  class_name == "DS_Dictionary" || class_name == "ObjectDecoder" ||
                                  class_name == "ObjectDecoderDelegate" || class_name == "CCContentManager";
            bool const is_cocos_ext = class_name.starts_with("cocos2d::extension");
            bool const is_exception =
                class_name == "cocos2d::CCLightning";  // custom RobTop class, lives in GeometryDash.exe

            if (binary.starts_with("libcocos2d")) return is_cocos && !is_cocos_ext && !is_exception;
            if (binary.starts_with("libextensions")) return is_cocos_ext;
            if (binary.starts_with("fmod")) return is_fmod_class(class_name);
            return !is_cocos || is_exception;
        }
        if (is_android(platform) && binary.starts_with("libfmod")) return is_fmod_class(class_name);
        return true;
    }

    bool is_function_present(
        Platform platform, std::string_view name, std::string_view binary_file, std::string_view source_file) {
        auto const binary = cpp::lower_ascii(binary_file);
        bool const is_fmod = name.starts_with("FMOD_");

        if (platform == Platform::Windows) {
            if (binary.starts_with("libcocos2d")) return source_file == "Kazmath.bro";
            if (binary.starts_with("fmod")) return is_fmod;
            return true;
        }
        if (is_android(platform) && binary.starts_with("libfmod")) return is_fmod;
        return true;
    }

    void BindingSet::reindex() {
        m_index.clear();
        for (std::size_t i = 0; i < bindings.size(); ++i)
            m_index.emplace(bindings[i].bind.offset, i);
    }

    void BindingSet::append(Binding binding) {
        m_index.emplace(binding.bind.offset, bindings.size());
        bindings.push_back(std::move(binding));
    }

    void BindingSet::add(Binding binding) {
        auto const address = binding.bind.offset;

        if (auto const found = m_index.find(address); found != m_index.end()) {
            auto const position = found->second;
            auto const where = fmt::format("{} and {}", binding.qualified_name(), bindings[position].short_info());
            if (binding.qualified_name() == bindings[position].qualified_name()) {
                log.warn("Duplicate binding with same qualified name! ({})", where);
                return;
            }
            if (binding.class_name == bindings[position].class_name) {
                log.warn("Duplicate binding within same class! ({})", where);
                return;
            }
            log.warn("Duplicate binding! ({})", where);
            duplicates[address] = {std::move(bindings[position])};
            bindings.erase(bindings.begin() + static_cast<std::ptrdiff_t>(position));
            reindex();
        }

        if (auto const group = duplicates.find(address); group != duplicates.end()) {
            group->second.push_back(std::move(binding));
            return;
        }
        append(std::move(binding));
    }
}  // namespace bromaida
