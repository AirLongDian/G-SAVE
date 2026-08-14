#include "gsave/gui/cloud_service.hpp"

#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <gtest/gtest.h>

#include <algorithm>

namespace gsave::gui {
namespace {

struct Reply final {
    int status{};
    QByteArray body;
};

class FakeGitService final : public QObject {
public:
    FakeGitService() {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = server_.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    buffers_[socket] += socket->readAll();
                    if (buffers_[socket].size() >= 1
                        && buffers_[socket].front() == '\x16') {
                        buffers_.remove(socket);
                        socket->disconnectFromHost();
                        return;
                    }
                    const auto header_end = buffers_[socket].indexOf("\r\n\r\n");
                    if (header_end < 0) return;
                    const auto first_end = buffers_[socket].indexOf("\r\n");
                    const auto first = buffers_[socket].left(first_end).split(' ');
                    if (first.size() < 2) return;
                    int content_length = 0;
                    for (const auto& line : buffers_[socket].left(header_end).split('\n')) {
                        if (line.toLower().startsWith("content-length:")) {
                            content_length = line.mid(15).trimmed().toInt();
                        }
                    }
                    if (buffers_[socket].size() < header_end + 4 + content_length) return;
                    const auto key = first[0] + ' ' + first[1];
                    requests.push_back(key + "\n" + buffers_[socket].left(header_end));
                    const auto reply = replies.value(key, Reply{404, R"({"message":"not found"})"});
                    const QByteArray reason = reply.status == 200 ? "OK"
                        : reply.status == 201 ? "Created" : "Not Found";
                    socket->write("HTTP/1.1 " + QByteArray::number(reply.status) + ' ' + reason
                        + "\r\nContent-Type: application/json\r\nContent-Length: "
                        + QByteArray::number(reply.body.size())
                        + "\r\nConnection: close\r\n\r\n" + reply.body);
                    socket->disconnectFromHost();
                    buffers_.remove(socket);
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        EXPECT_TRUE(server_.listen(QHostAddress::LocalHost));
    }

    [[nodiscard]] QString url() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort());
    }

    void reply(const QByteArray& method, const QByteArray& path, int status, QByteArray body) {
        replies.insert(method + ' ' + path, Reply{status, std::move(body)});
    }

    QList<QByteArray> requests;

private:
    QTcpServer server_;
    QHash<QTcpSocket*, QByteArray> buffers_;
    QHash<QByteArray, Reply> replies;
};

TEST(CloudService, NormalizesPlayerAddressesAndBuildsStableRepositoryNames) {
    auto normalized = normalize_git_service_url(QStringLiteral("git.example.test/"));
    ASSERT_TRUE(normalized) << normalized.error().message();
    EXPECT_EQ(normalized->toString(), QStringLiteral("https://git.example.test"));
    EXPECT_EQ(automatic_repository_name(QStringLiteral("dark-souls-iii"), 0, 1),
              QStringLiteral("gsave-dark-souls-iii"));
    EXPECT_EQ(automatic_repository_name(QStringLiteral("generic-Game.EXE"), 1, 2),
              QStringLiteral("gsave-generic-game-exe-2"));
    EXPECT_EQ(service_address_from_remote(
                  QStringLiteral("https://git.example.test/alice/gsave-game.git")),
              QStringLiteral("https://git.example.test"));
}

TEST(CloudService, DetectsGiteaAuthenticatesAndCreatesAPrivateRepository) {
    FakeGitService service;
    service.reply("GET", "/api/v1/version", 200, R"({"version":"1.22.0"})");
    service.reply("GET", "/api/v1/user", 200,
                  R"({"login":"player","full_name":"Player One"})");
    service.reply("GET", "/api/v1/repos/player/gsave-game", 404,
                  R"({"message":"not found"})");
    service.reply("POST", "/api/v1/user/repos", 201,
                  R"({"clone_url":"https://git.example/player/gsave-game.git"})");

    auto identity = detect_git_service(service.url(), QStringLiteral("secret-token"));
    ASSERT_TRUE(identity) << identity.error().message();
    EXPECT_EQ(identity->kind, GitServiceKind::gitea);
    EXPECT_EQ(identity->username, QStringLiteral("player"));
    auto repository = ensure_private_repository(
        *identity, QStringLiteral("secret-token"), QStringLiteral("gsave-game"),
        QStringLiteral("Game"));
    ASSERT_TRUE(repository) << repository.error().message();
    EXPECT_TRUE(repository->created);
    EXPECT_EQ(repository->clone_url,
              QStringLiteral("https://git.example/player/gsave-game.git"));
    EXPECT_TRUE(std::ranges::any_of(service.requests, [](const QByteArray& request) {
        return request.contains("Authorization: token secret-token");
    }));
}

TEST(CloudService, DetectsSelfHostedGitLabWithoutAskingThePlayer) {
    FakeGitService service;
    service.reply("GET", "/api/v1/version", 404, R"({"message":"404"})");
    service.reply("GET", "/api/v4/version", 200, R"({"version":"18.2.0"})");
    service.reply("GET", "/api/v4/user", 200,
                  R"({"username":"player","name":"Player Two"})");
    service.reply("GET", "/api/v4/projects/player%2Fgsave-game", 200,
                  R"({"http_url_to_repo":"https://gitlab.example/player/gsave-game.git","visibility":"private"})");

    auto identity = detect_git_service(service.url(), QStringLiteral("gitlab-token"));
    ASSERT_TRUE(identity) << identity.error().message();
    EXPECT_EQ(identity->kind, GitServiceKind::gitlab);
    EXPECT_EQ(identity->username, QStringLiteral("player"));
    auto repository = ensure_private_repository(
        *identity, QStringLiteral("gitlab-token"), QStringLiteral("gsave-game"),
        QStringLiteral("Game"));
    ASSERT_TRUE(repository) << repository.error().message();
    EXPECT_FALSE(repository->created);
    EXPECT_TRUE(std::ranges::any_of(service.requests, [](const QByteArray& request) {
        return request.toLower().contains("private-token: gitlab-token");
    }));
}

TEST(CloudService, FallsBackToHttpWhenHttpsProbeFails) {
    FakeGitService service;
    const auto host = service.url().remove(QStringLiteral("http://"));
    service.reply("GET", "/api/v1/version", 200, R"({"version":"1.22.0"})");
    service.reply("GET", "/api/v1/user", 200,
                  R"({"login":"player","full_name":"Player One"})");
    service.reply("GET", "/api/v1/repos/player/gsave-game", 404,
                  R"({"message":"not found"})");
    service.reply("POST", "/api/v1/user/repos", 201,
                  QStringLiteral(R"({"clone_url":"https://%1/player/gsave-game.git"})")
                      .arg(host).toUtf8());

    const auto address = QStringLiteral("https://%1").arg(host);
    auto identity = detect_git_service(address, QStringLiteral("secret-token"));
    ASSERT_TRUE(identity) << identity.error().message();
    EXPECT_EQ(identity->kind, GitServiceKind::gitea);
    EXPECT_EQ(identity->username, QStringLiteral("player"));
    EXPECT_EQ(identity->service_url.scheme(), QStringLiteral("http"));
    auto repository = ensure_private_repository(
        *identity, QStringLiteral("secret-token"), QStringLiteral("gsave-game"),
        QStringLiteral("Game"));
    ASSERT_TRUE(repository) << repository.error().message();
    EXPECT_TRUE(repository->created);
    EXPECT_EQ(repository->clone_url,
              QStringLiteral("http://%1/player/gsave-game.git").arg(host));
}

TEST(CloudService, RefusesAnExistingPublicRepository) {
    FakeGitService service;
    service.reply("GET", "/api/v1/version", 200, R"({"version":"1.22.0"})");
    service.reply("GET", "/api/v1/user", 200,
                  R"({"login":"player","full_name":"Player One"})");
    service.reply("GET", "/api/v1/repos/player/gsave-game", 200,
                  R"({"clone_url":"https://git.example/player/gsave-game.git","private":false})");

    auto identity = detect_git_service(service.url(), QStringLiteral("secret-token"));
    ASSERT_TRUE(identity) << identity.error().message();
    auto repository = ensure_private_repository(
        *identity, QStringLiteral("secret-token"), QStringLiteral("gsave-game"),
        QStringLiteral("Game"));
    ASSERT_FALSE(repository);
    EXPECT_EQ(repository.error().code, std::errc::permission_denied);
}

}  // namespace
}  // namespace gsave::gui
