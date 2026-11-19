#pragma once

#include <memory>
#include <string>
#include <vector>

#include <asp/detail/Function.hpp>

#include <bromaida/config/settings.hpp>
#include <bromaida/typegen/profile.hpp>
#include <bromaida/util/path.hpp>

#include "ui/ida_ui.hpp"

#include <pro.h>
#include <idp.hpp>
#include <kernwin.hpp>

namespace bromaida::ida {
    class FunctionAction final : public action_handler_t {
    public:
        explicit FunctionAction(asp::MoveOnlyFunction<void()> fn)
            : m_fn(std::move(fn)) {}

        int idaapi activate(action_activation_ctx_t*) override {
            m_fn();
            return 1;
        }
        action_state_t idaapi update(action_update_ctx_t*) override { return AST_ENABLE_ALWAYS; }

    private:
        asp::MoveOnlyFunction<void()> m_fn;
    };

    class Plugin final : public plugmod_t, public event_listener_t {
    public:
        Plugin();
        ~Plugin() override;
        bool idaapi run(size_t arg) override;
        ssize_t idaapi on_event(ssize_t code, va_list va) override;

    private:
        void add_action(std::string name, char const* label, bool in_menu, asp::MoveOnlyFunction<void()> fn);
        void reload_profiles();
        void save_settings();
        [[nodiscard]] bool analysis_is_safe() const;
        void open_main(MainTab tab);
        void import_bindings();
        bool import_bindings(PathView source);
        void export_bindings();
        bool export_bindings(PathView file);
        void export_current_function();
        void about();

        Settings m_settings;
        Path m_settings_file;
        ProfileSet m_profiles;
        std::vector<std::unique_ptr<FunctionAction>> m_handlers;
        std::vector<std::string> m_actions;
        std::vector<std::string> m_menu_actions;
    };
}  // namespace bromaida::ida
