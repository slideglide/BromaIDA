#include "qt_ui.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <vector>

#include <bromaida/version.hpp>

// Qt before the IDA SDK: the SDK's headers poison libc names that Qt's headers use.
#include <QtWidgets>

// IDA's Qt lives in a namespace (see QT_NAMESPACE in CMake); this brings it into scope.
QT_USE_NAMESPACE

namespace bromaida::ida::qt {

static QString to_qt(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// IDA's main window, so dialogs open on top of it and not behind.
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
    std::string_view message, std::span<std::string_view const> buttons, Icon icon, std::size_t default_button) {
    QMessageBox box(to_qt(icon), to_qt(kPluginName), to_qt(message), QMessageBox::NoButton, parent_window());
    box.setTextFormat(Qt::PlainText);

    std::vector<QAbstractButton*> added;
    for (auto const [index, text] : buttons | std::views::enumerate) {
        added.push_back(box.addButton(to_qt(text), QMessageBox::ActionRole));
    }
    box.setDefaultButton(static_cast<QPushButton*>(added[std::min(default_button, added.size() - 1)]));
    box.setEscapeButton(added.size() > 1 ? added[1] : added.front());
    box.exec();

    auto const clicked = std::ranges::find(added, box.clickedButton());
    return clicked == added.end() ? 1 : static_cast<std::size_t>(clicked - added.begin());
}

void message(Icon icon, std::string_view text) {
    QMessageBox box(to_qt(icon), to_qt(kPluginName), to_qt(text), QMessageBox::Ok, parent_window());
    box.setTextFormat(Qt::PlainText);
    box.exec();
}

std::optional<Path> pick_broma_file(std::string_view title) {
    // the exporter merges into an existing file, so replacing it is not something to ask about
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

MainAction main_dialog(
    std::string_view platform_name, bool platform_supported, std::function<void()> const& open_settings) {
    QDialog dialog(parent_window());
    dialog.setWindowTitle(to_qt(kPluginName));
    dialog.setModal(true);
    dialog.setMinimumWidth(360);

    auto* layout = new QVBoxLayout(&dialog);

    auto* title = centered_label(to_qt(fmt::format("{} v{}.", kPluginName, kVersion)), &dialog);
    auto font = title->font();
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);
    layout->addWidget(centered_label(to_qt(kPluginDescription), &dialog));
    layout->addSpacing(8);
    layout->addWidget(centered_label(
        to_qt(
            platform_supported ? fmt::format("Current Platform: {}", platform_name)
                               : std::string{"This binary is not a platform Broma knows."}),
        &dialog));
    layout->addSpacing(8);
    layout->addWidget(new QLabel(QStringLiteral("Select Mode:"), &dialog));

    auto* import_button = new QPushButton(QStringLiteral("&Import"), &dialog);
    auto* export_button = new QPushButton(QStringLiteral("&Export"), &dialog);
    import_button->setEnabled(platform_supported);
    export_button->setEnabled(platform_supported);
    auto* mode_row = new QHBoxLayout;
    mode_row->addWidget(import_button);
    mode_row->addWidget(export_button);
    layout->addLayout(mode_row);

    auto* settings_button = new QPushButton(QStringLiteral("&Settings"), &dialog);
    layout->addWidget(settings_button);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    MainAction result = MainAction::Cancel;
    QObject::connect(import_button, &QPushButton::clicked, &dialog, [&] {
        result = MainAction::Import;
        dialog.accept();
    });
    QObject::connect(export_button, &QPushButton::clicked, &dialog, [&] {
        result = MainAction::Export;
        dialog.accept();
    });
    // settings open on top of this window and return to it
    QObject::connect(settings_button, &QPushButton::clicked, &dialog, [&] { open_settings(); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
    return result;
}

bool settings_dialog(Settings& settings, bool idaclang_available, std::span<ProfileChoice const> profiles) {
    QDialog dialog(parent_window());
    dialog.setWindowTitle(to_qt(kPluginName));
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    std::vector<std::pair<QCheckBox*, SettingDefinition const*>> boxes;
    QComboBox* profile_box = nullptr;
    QComboBox* parser_box = nullptr;

    for (auto const group : {SettingGroup::General, SettingGroup::TypeImport}) {
        auto* frame = new QGroupBox(to_qt(title(group)), &dialog);
        auto* inner = new QVBoxLayout(frame);

        for (auto const& definition : kSettingDefinitions) {
            if (definition.group != group) continue;

            auto* box = new QCheckBox(to_qt(definition.label), frame);
            auto tooltip = to_qt(definition.tooltip);
            box->setChecked(settings.*definition.member);

            // importing types needs IDAClang: without it the option can't be turned on
            if (definition.member == &Settings::import_types && !idaclang_available) {
                box->setChecked(false);
                box->setEnabled(false);
                tooltip += QStringLiteral("\n\nIDAClang is not available in this IDA.");
            }
            box->setToolTip(tooltip);
            inner->addWidget(box);
            boxes.emplace_back(box, &definition);
        }
        // which profile (SDK headers, STL flavors) the type import is built from
        if (group == SettingGroup::TypeImport) {
            auto* row = new QHBoxLayout;
            row->addWidget(new QLabel(QStringLiteral("Profile:"), frame));
            profile_box = new QComboBox(frame);
            profile_box->setToolTip(QStringLiteral(
                "The headers and STL layouts used for the type import.\n"
                "Automatic picks one from the game version in the bindings folder's name (2.2091)."));
            profile_box->addItem(QStringLiteral("Automatic (by game version)"), QString());
            for (auto const& choice : profiles) {
                profile_box->addItem(to_qt(choice.title.empty() ? choice.name : choice.title), to_qt(choice.name));
            }
            auto index = profile_box->findData(to_qt(settings.profile));
            if (index < 0) {
                // a profile that has since been removed: keep it visible instead of silently dropping it
                profile_box->addItem(to_qt(settings.profile + " (not found)"), to_qt(settings.profile));
                index = profile_box->count() - 1;
            }
            profile_box->setCurrentIndex(index);
            row->addWidget(profile_box, 1);
            inner->addLayout(row);

            // which of IDA's two C++ parsers does the type import
            auto* parser_row = new QHBoxLayout;
            parser_row->addWidget(new QLabel(QStringLiteral("Type parser:"), frame));
            parser_box = new QComboBox(frame);
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
        layout->addWidget(frame);
    }

    layout->addSpacing(8);
    layout->addWidget(centered_label(to_qt(fmt::format("{} v{}.", kPluginName, kVersion)), &dialog));
    layout->addWidget(centered_label(to_qt(fmt::format("Original author: {}", kPluginAuthor)), &dialog));
    layout->addWidget(centered_label(to_qt(fmt::format("Maintained by {}", kPluginMaintainer)), &dialog));
    layout->addWidget(centered_label(QStringLiteral("(+ contributors)"), &dialog));

    auto* github = new QPushButton(QStringLiteral("Open GitHub"), &dialog);
    QObject::connect(
        github, &QPushButton::clicked, &dialog, [] { QDesktopServices::openUrl(QUrl(to_qt(kPluginGithub))); });
    auto* github_row = new QHBoxLayout;
    github_row->addStretch();
    github_row->addWidget(github);
    github_row->addStretch();
    layout->addLayout(github_row);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // nothing is written back until OK, so Cancel throws every change away
    if (dialog.exec() != QDialog::Accepted) return false;
    for (auto const& [box, definition] : boxes)
        settings.*definition->member = box->isChecked();
    if (parser_box) settings.type_parser = parser_box->currentData().toString().toStdString();
    if (profile_box) settings.profile = profile_box->currentData().toString().toStdString();
    if (!idaclang_available) settings.import_types = false;
    return true;
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
