// Dumps the exact JSON shapes the GUI backend exposes, using a real fixture
// configuration with real Git history. Written for frontend development so the
// mock data matches the production contract instead of being guessed.
#include "gsave/gui/gui_controller.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <filesystem>
#include <iostream>

namespace {

[[nodiscard]] QJsonValue to_json(const QVariant& value) {
    return QJsonValue::fromVariant(value);
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    if (argc != 3) {
        std::cerr << "usage: gui_contract_dump CONFIG OUTPUT\n";
        return 2;
    }
    const auto config = std::filesystem::path{argv[1]};
    const auto output = std::filesystem::path{argv[2]};

    gsave::gui::GuiController controller{
        config.parent_path() / L"gsave-core.exe",
        config,
        config.parent_path() / L"packages"};
    if (!controller.initialize()) {
        std::cerr << "controller initialize failed\n";
        return 1;
    }

    QJsonObject root;
    root.insert(QStringLiteral("library"), to_json(controller.library()));
    root.insert(QStringLiteral("games"), to_json(controller.games()));
    root.insert(QStringLiteral("packages"), to_json(controller.packages()));
    root.insert(QStringLiteral("cloudSettings"), to_json(controller.cloudSettings()));

    QJsonObject service;
    service.insert(QStringLiteral("coreRunning"), controller.coreRunning());
    service.insert(QStringLiteral("coreBusy"), controller.coreBusy());
    service.insert(QStringLiteral("autostartEnabled"), controller.autostartEnabled());
    service.insert(QStringLiteral("hasPendingChanges"), controller.hasPendingChanges());
    service.insert(QStringLiteral("corePath"), controller.corePath());
    service.insert(QStringLiteral("configPath"), controller.configPath());
    service.insert(QStringLiteral("packageRoot"), controller.packageRoot());
    service.insert(QStringLiteral("indexUrl"), controller.indexUrl());
    service.insert(QStringLiteral("indexStatus"), controller.indexStatus());
    service.insert(QStringLiteral("indexLoading"), controller.indexLoading());
    root.insert(QStringLiteral("service"), service);

    QJsonArray details;
    for (int index = 0; index < controller.games().size(); ++index) {
        QJsonObject entry;
        entry.insert(QStringLiteral("gameDetail"),
            to_json(controller.gameDetail(index)));
        entry.insert(QStringLiteral("repositoryState"),
            to_json(controller.repositoryState(index, 0)));
        entry.insert(QStringLiteral("branches"),
            to_json(controller.branches(index, 0)));
        controller.refreshHistory(index, 0);
        entry.insert(QStringLiteral("history"), to_json(controller.history()));
        entry.insert(QStringLiteral("historyDiverged"), controller.historyDiverged());
        entry.insert(QStringLiteral("historyStatus"), controller.historyStatus());
        entry.insert(QStringLiteral("repositoriesForGame"),
            to_json(controller.repositoriesForGame(index)));
        details.push_back(entry);
    }
    root.insert(QStringLiteral("perGame"), details);

    QFile file{QString::fromStdWString(output.wstring())};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "cannot write output\n";
        return 1;
    }
    file.write(QJsonDocument{root}.toJson(QJsonDocument::Indented));
    file.close();
    std::cout << "contract written: " << output.string() << '\n';
    return 0;
}
