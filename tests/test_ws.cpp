// Test bout-en-bout du transport : démarre le serveur WebSocket puis un vrai
// client QWebSocket (comme le fera le JS), envoie resolveServer, vérifie la
// réponse. Headless (réseau local + repo).

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

#include "bridge/WsServer.h"

using namespace lami;

static QTextStream out(stdout);
static int failures = 0;
static void check(bool ok, const QString &l) { out<<(ok?"  [OK] ":"  [ÉCHEC] ")<<l<<"\n"; if(!ok)++failures; }

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    WsServer server(0);  // port auto
    if (!server.listen()) { out << "listen KO\n"; return 1; }
    const QString url = QString("ws://127.0.0.1:%1").arg(server.port());
    out << "Serveur en écoute : " << url << "\n";

    auto *client = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, &app);

    QObject::connect(client, &QWebSocket::connected, &app, [&]() {
        out << "Client connecté, envoi de resolveServer…\n"; out.flush();
        const QJsonObject req{
            {"id", 7},
            {"method", "resolveServer"},
            {"params", QJsonObject{{"ip", "play.atraxe.fr"}}},
        };
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact)));
    });

    QObject::connect(client, &QWebSocket::textMessageReceived, &app, [&](const QString &text) {
        const QJsonObject msg = QJsonDocument::fromJson(text.toUtf8()).object();
        out << "Reçu : " << text << "\n";
        check(msg.value("id").toInt() == 7, "id préservé à travers le WebSocket");
        check(msg.value("ok").toBool(), "ok = true");
        check(msg.value("result").toObject().value("id").toString() == "exemple",
              "serveur résolu via le WebSocket");

        out << (failures == 0 ? "\nTOUS LES TESTS PASSENT ✅\n"
                              : QString("\n%1 échec(s) ❌\n").arg(failures));
        out.flush();
        app.exit(failures == 0 ? 0 : 1);
    });

    QObject::connect(client, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
                     &app, [&](QAbstractSocket::SocketError) {
        out << "\n❌ erreur socket : " << client->errorString() << "\n"; app.exit(2);
    });

    QTimer::singleShot(15000, &app, [&]{ out << "\nTimeout.\n"; app.exit(3); });

    client->open(QUrl(url));
    return app.exec();
}
