#include "qt_ui.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <ranges>
#include <vector>

#include <asp/detail/Function.hpp>
#include <asp/iter.hpp>
#include <bromaida/version.hpp>

#include <QtWidgets>

QT_USE_NAMESPACE

namespace bromaida::ida::qt {
    static QString to_qt(std::string_view text) {
        return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }

    static QWidget* parent_window() { return QApplication::activeWindow(); }

    static QMessageBox::Icon to_qt(Icon icon) {
        switch (icon) {
            case Icon::Info: return QMessageBox::Information;
            case Icon::Question: return QMessageBox::Question;
            case Icon::Warning: return QMessageBox::Warning;
            case Icon::Error: return QMessageBox::Critical;
            case Icon::None: break;
        }
        return QMessageBox::NoIcon;
    }

    static QLabel* centered_label(QString const& text, QWidget* parent = nullptr) {
        auto* label = new QLabel(text, parent);
        label->setAlignment(Qt::AlignCenter);
        return label;
    }

    bool available() {
        if (QCoreApplication::instance() == nullptr) return false;
        auto const runtime = QString::fromLatin1(qVersion()).split(QLatin1Char('.'));
        return runtime.size() >= 2 && runtime[0].toInt() == QT_VERSION_MAJOR && runtime[1].toInt() == QT_VERSION_MINOR;
    }

    std::size_t ask(
        std::string_view message, std::span<std::string_view const> buttons, Icon icon, std::size_t default_button,
        std::string_view remember_label, bool* remember) {
        QMessageBox box(to_qt(icon), to_qt(kPluginName), to_qt(message), QMessageBox::NoButton, parent_window());
        box.setTextFormat(Qt::PlainText);

        auto added =
            asp::iter::from(buttons)
                .map([&](std::string_view text) { return box.addButton(to_qt(text), QMessageBox::ActionRole); })
                .collect<std::vector<QAbstractButton*>>();
        box.setDefaultButton(static_cast<QPushButton*>(added[std::min(default_button, added.size() - 1)]));
        box.setEscapeButton(added.size() > 1 ? added[1] : added.front());
        QCheckBox* check = nullptr;
        if (!remember_label.empty() && remember != nullptr) {
            check = new QCheckBox(to_qt(remember_label), &box);
            box.setCheckBox(check);
        }
        box.exec();
        if (check != nullptr) *remember = check->isChecked();

        auto const clicked = std::ranges::find(added, box.clickedButton());
        return clicked == added.end() ? 1 : static_cast<std::size_t>(clicked - added.begin());
    }

    void message(Icon icon, std::string_view text) {
        QMessageBox box(to_qt(icon), to_qt(kPluginName), to_qt(text), QMessageBox::Ok, parent_window());
        box.setTextFormat(Qt::PlainText);
        box.exec();
    }

    std::optional<Path> pick_broma_file(std::string_view title) {
        auto const chosen = QFileDialog::getSaveFileName(
            parent_window(), to_qt(title), QStringLiteral("GeometryDash.bro"),
            QStringLiteral("Broma files (*.bro);;All files (*)"), nullptr, QFileDialog::DontConfirmOverwrite);
        if (chosen.isEmpty()) return std::nullopt;
        return Path{chosen.toUtf8().toStdString()};
    }

    std::optional<Path> pick_folder(std::string_view title) {
        auto const chosen =
            QFileDialog::getExistingDirectory(parent_window(), to_qt(title), QString(), QFileDialog::ShowDirsOnly);
        if (chosen.isEmpty()) return std::nullopt;
        return Path{chosen.toUtf8().toStdString()};
    }

    /// The settings widgets; `apply` writes their state into the Settings they were built from.
    struct SettingsPage {
        QWidget* widget = nullptr;
        asp::MoveOnlyFunction<void()> apply;
    };

    static SettingsPage build_settings_page(
        Settings& settings, bool idaclang_available, std::span<ProfileChoice const> profiles, QWidget* parent) {
        auto* page = new QWidget(parent);
        auto* page_layout = new QVBoxLayout(page);
        page_layout->setContentsMargins(0, 0, 0, 0);
        auto* tabs = new QTabWidget(page);
        page_layout->addWidget(tabs);
        auto boxes = std::make_shared<std::vector<std::pair<QCheckBox*, SettingDefinition const*>>>();
        QComboBox* profile_box = nullptr;
        QComboBox* parser_box = nullptr;

        for (auto const group :
             {SettingGroup::General, SettingGroup::Importer, SettingGroup::Exporter, SettingGroup::Profile,
              SettingGroup::Advanced}) {
            auto* tab = new QWidget(tabs);
            auto* inner = new QVBoxLayout(tab);
            tabs->addTab(tab, to_qt(title(group)));

            for (auto const& definition : kSettingDefinitions) {
                if (definition.group != group) continue;

                auto* box = new QCheckBox(to_qt(definition.label), tab);
                auto tooltip = to_qt(definition.tooltip);
                box->setChecked(settings.*definition.member);

                if (definition.member == &Settings::import_types && !idaclang_available) {
                    box->setChecked(false);
                    box->setEnabled(false);
                    tooltip += QStringLiteral("\n\nIDAClang is not available in this IDA.");
                }
                box->setToolTip(tooltip);
                inner->addWidget(box);
                boxes->emplace_back(box, &definition);
            }
            if (group == SettingGroup::Profile) {
                auto* row = new QHBoxLayout;
                row->addWidget(new QLabel(QStringLiteral("Profile:"), tab));
                profile_box = new QComboBox(tab);
                profile_box->setToolTip(QStringLiteral(
                    "The headers and STL layouts used for the type import.\n"
                    "Automatic picks one from the game version in the bindings folder's name (2.2091)."));
                profile_box->addItem(QStringLiteral("Automatic (by game version)"), QString());
                for (auto const& choice : profiles) {
                    profile_box->addItem(to_qt(choice.title.empty() ? choice.name : choice.title), to_qt(choice.name));
                }
                auto index = profile_box->findData(to_qt(settings.profile));
                if (index < 0) {
                    profile_box->addItem(to_qt(settings.profile + " (not found)"), to_qt(settings.profile));
                    index = profile_box->count() - 1;
                }
                profile_box->setCurrentIndex(index);
                row->addWidget(profile_box, 1);
                inner->addLayout(row);

                auto* parser_row = new QHBoxLayout;
                parser_row->addWidget(new QLabel(QStringLiteral("Type parser:"), tab));
                parser_box = new QComboBox(tab);
                parser_box->setToolTip(QStringLiteral(
                    "IDA ships two C++ parsers. Automatic uses old_clang, which copes better with the STL stand-ins.\n"
                    "Try the other if the import crashes or fails."));
                parser_box->addItem(QStringLiteral("Automatic"), QString());
                parser_box->addItem(QStringLiteral("clang (new)"), QStringLiteral("clang"));
                parser_box->addItem(QStringLiteral("old_clang"), QStringLiteral("old_clang"));
                parser_box->setCurrentIndex(std::max(0, parser_box->findData(to_qt(settings.type_parser))));
                parser_row->addWidget(parser_box, 1);
                inner->addLayout(parser_row);
            }
            inner->addStretch();
        }

        auto apply = [&settings, boxes, profile_box, parser_box, idaclang_available] {
            for (auto const& [box, definition] : *boxes)
                settings.*definition->member = box->isChecked();
            if (parser_box) settings.type_parser = parser_box->currentData().toString().toStdString();
            if (profile_box) settings.profile = profile_box->currentData().toString().toStdString();
            if (!idaclang_available) settings.import_types = false;
        };
        return {page, std::move(apply)};
    }

    static QWidget* build_about_page(QWidget* parent) {
        auto* page = new QWidget(parent);
        auto* layout = new QVBoxLayout(page);
        layout->addStretch();
        auto* heading = centered_label(to_qt(fmt::format("{} v{}.", kPluginName, kVersion)), page);
        auto font = heading->font();
        font.setBold(true);
        heading->setFont(font);
        layout->addWidget(heading);
        layout->addWidget(centered_label(to_qt(kPluginDescription), page));
        layout->addSpacing(8);
        layout->addWidget(centered_label(to_qt(fmt::format("Original author: {}", kPluginAuthor)), page));
        layout->addWidget(centered_label(to_qt(fmt::format("Maintained by {}", kPluginMaintainer)), page));
        layout->addWidget(centered_label(QStringLiteral("(+ contributors)"), page));

        auto* github = new QPushButton(QStringLiteral("Open GitHub"), page);
        QObject::connect(
            github, &QPushButton::clicked, page, [] { QDesktopServices::openUrl(QUrl(to_qt(kPluginGithub))); });
        auto* row = new QHBoxLayout;
        row->addStretch();
        row->addWidget(github);
        row->addStretch();
        layout->addLayout(row);
        layout->addStretch();
        return page;
    }

    /// A tab that describes its action. The dialog's own button runs it.
    static QWidget* build_action_page(QString const& text, QString const& platform, bool enabled, QWidget* parent) {
        auto* page = new QWidget(parent);
        auto* layout = new QVBoxLayout(page);
        layout->addStretch();
        auto* description = centered_label(text, page);
        description->setWordWrap(true);
        layout->addWidget(description);
        layout->addSpacing(8);
        auto* status = centered_label(platform, page);
        status->setEnabled(enabled);
        layout->addWidget(status);
        layout->addStretch();
        return page;
    }

    MainResult main_dialog(
        std::string_view platform_name, bool platform_supported, Settings& settings, bool idaclang_available,
        std::span<ProfileChoice const> profiles, MainTab initial_tab) {
        QDialog dialog(parent_window());
        dialog.setWindowTitle(to_qt(fmt::format("{} v{}", kPluginName, kVersion)));
        dialog.setModal(true);
        dialog.setMinimumWidth(420);

        auto* layout = new QVBoxLayout(&dialog);
        auto* tabs = new QTabWidget(&dialog);
        layout->addWidget(tabs);

        auto const platform_text = to_qt(
            platform_supported ? fmt::format("Current Platform: {}", platform_name)
                               : std::string{"This binary is not a platform Broma knows."});
        tabs->addTab(
            build_action_page(
                QStringLiteral(
                    "Reads every .bro file in a folder and applies the function names, member names, "
                    "addresses and (optionally) types to this database."),
                platform_text, platform_supported, tabs),
            QStringLiteral("&Import"));
        tabs->addTab(
            build_action_page(
                QStringLiteral(
                    "Writes the addresses of this database's functions into a .bro file, merging into "
                    "what is already there."),
                platform_text, platform_supported, tabs),
            QStringLiteral("&Export"));
        auto settings_page = build_settings_page(settings, idaclang_available, profiles, tabs);
        tabs->addTab(settings_page.widget, QStringLiteral("&Settings"));
        tabs->addTab(build_about_page(tabs), QStringLiteral("&About"));
        tabs->setCurrentIndex(static_cast<int>(initial_tab));

        auto* buttons = new QDialogButtonBox(&dialog);
        auto* run = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
        auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
        layout->addWidget(buttons);

        auto const update_run = [&, tabs, run] {
            switch (static_cast<MainTab>(tabs->currentIndex())) {
                case MainTab::Import:
                    run->setText(QStringLiteral("Import..."));
                    run->setEnabled(platform_supported);
                    break;
                case MainTab::Export:
                    run->setText(QStringLiteral("Export..."));
                    run->setEnabled(platform_supported);
                    break;
                case MainTab::Settings:
                    run->setText(QStringLiteral("Save"));
                    run->setEnabled(true);
                    break;
                case MainTab::About:
                    run->setText(QStringLiteral("OK"));
                    run->setEnabled(true);
                    break;
            }
        };
        QObject::connect(tabs, &QTabWidget::currentChanged, &dialog, update_run);
        update_run();
        run->setDefault(true);

        QObject::connect(run, &QPushButton::clicked, &dialog, &QDialog::accept);
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted) return {};

        settings_page.apply();
        MainResult result{.settings_changed = true};
        switch (static_cast<MainTab>(tabs->currentIndex())) {
            case MainTab::Import: result.action = MainAction::Import; break;
            case MainTab::Export: result.action = MainAction::Export; break;
            default: break;
        }
        return result;
    }

    void copyable_text(std::string_view title, std::string_view label, std::string_view text) {
        QDialog dialog(parent_window());
        dialog.setWindowTitle(to_qt(title));
        dialog.setMinimumWidth(520);

        auto* layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(to_qt(label), &dialog));

        auto* line = new QLineEdit(to_qt(text), &dialog);
        line->setReadOnly(true);
        line->setCursorPosition(0);
        auto monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        line->setFont(monospace);
        layout->addWidget(line);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        auto* copy = buttons->addButton(QStringLiteral("&Copy"), QDialogButtonBox::ActionRole);
        layout->addWidget(buttons);
        QObject::connect(
            copy, &QPushButton::clicked, &dialog, [line] { QGuiApplication::clipboard()->setText(line->text()); });
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        line->selectAll();
        dialog.exec();
    }
}  // namespace bromaida::ida::qt
