// Test du dispatcher du pont UI↔C++ : envoie une requête resolveServer (comme le
// ferait le JS) et vérifie la réponse JSON. Réseau, sans webview → headless.

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

#include "bridge/Bridge.h"

using namespace lami;

static QTextStream out(stdout);
static int failures = 0;
static void check(bool ok, const QString &l) { out<<(ok?"  [OK] ":"  [ÉCHEC] ")<<l<<"\n"; if(!ok)++failures; }

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    auto *bridge = new Bridge(&app);

    QObject::connect(bridge, &Bridge::response, &app, [&](const QJsonObject &msg) {
        out << "Réponse : " << QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)) << "\n";
        check(msg.value("id").toInt() == 42, "id de requête préservé");
        check(msg.value("ok").toBool(), "ok = true");
        const QJsonObject r = msg.value("result").toObject();
        check(r.value("id").toString() == "exemple", "serveur résolu par IP = exemple");
        check(r.value("version").toString() == "1.20.1", "version présente");
        check(r.value("loader").toString().contains("fabric", Qt::CaseInsensitive), "loader lisible");
        check(r.contains("mods") && r.contains("plugins"), "champs UI (mods/plugins) présents");

        out << (failures == 0 ? "\nTOUS LES TESTS PASSENT ✅\n"
                              : QString("\n%1 échec(s) ❌\n").arg(failures));
        out.flush();
        app.exit(failures == 0 ? 0 : 1);
    });

    QTimer::singleShot(15000, &app, [&]{ out << "\nTimeout.\n"; app.exit(2); });

    // Simule ce que le JS enverra via WebSocket.
    const QJsonObject req{
        {"id", 42},
        {"method", "resolveServer"},
        {"params", QJsonObject{{"ip", "play.atraxe.fr"}}},
    };
    out << "Requête : " << QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact)) << "\n";
    bridge->handle(req);

    return app.exec();
}
