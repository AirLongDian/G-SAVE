#include "gsave/gui/gui_controller.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QMessageBox>

#include <filesystem>

namespace {

[[nodiscard]] std::filesystem::path default_config_path(
    const std::filesystem::path& executable) {
    const auto local_app_data = qEnvironmentVariable("LOCALAPPDATA");
    if (!local_app_data.isEmpty()) {
        return std::filesystem::path{local_app_data.toStdWString()}
            / L"G-SAVE" / L"config.toml";
    }
    return executable.parent_path() / L"config.toml";
}

}  // namespace

int main(int argc, char* argv[]) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("G-SAVE"));
    QCoreApplication::setOrganizationName(QStringLiteral("G-SAVE"));
    application.setWindowIcon(QIcon{QStringLiteral(":/save.png")});

    const auto executable = std::filesystem::path{
        QCoreApplication::applicationFilePath().toStdWString()};
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption config_option{
        QStringList{QStringLiteral("c"), QStringLiteral("config")},
        QStringLiteral("Use this G-SAVE configuration file."),
        QStringLiteral("path")};
    parser.addOption(config_option);
    parser.process(application);
    const auto config = parser.isSet(config_option)
        ? std::filesystem::path{parser.value(config_option).toStdWString()}
        : default_config_path(executable);

    gsave::gui::GuiController controller{
        executable.parent_path() / L"gsave-core.exe",
        config,
        executable.parent_path() / L"packages"};
    if (!controller.initialize()) return 1;

    QQmlApplicationEngine engine;
    QStringList qml_warnings;
    QObject::connect(
        &engine, &QQmlApplicationEngine::warnings,
        &application,
        [&](const QList<QQmlError>& warnings) {
            for (const auto& warning : warnings) qml_warnings.push_back(warning.toString());
        });
    engine.rootContext()->setContextProperty(QStringLiteral("Backend"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/GSave/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        QMessageBox::critical(
            nullptr, QStringLiteral("G-SAVE 界面无法启动"),
            QStringLiteral("无法载入内置 QML 界面。\n\n%1")
                .arg(qml_warnings.join(QLatin1Char('\n'))));
        return 1;
    }
    return application.exec();
}
