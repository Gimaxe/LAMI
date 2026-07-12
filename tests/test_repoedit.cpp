// Vérifie removeFromIndex + deleteFile : publie un serveur temporaire + entrée
// d'index, puis le supprime et confirme que le manifeste ET l'index sont nettoyés.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

#include "github/GitHubClient.h"
#include "github/Models.h"

using namespace lami;
static QTextStream out(stdout);

static QString readToken() {
    QFile f(QDir::homePath() + "/LAMI/.token");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString token = readToken();
    if (token.isEmpty()) { out << "token manquant\n"; return 1; }

    auto *gh = new GitHubClient("Gimaxe", "LAMI-db", "main", &app);
    gh->setToken(token);

    const QString sid = "zz-temp-test";
    ServerInfo s; s.id = sid; s.name = "ZZ Temp"; s.address = "zz.temp.local";
    s.minecraftVersion = "1.20.1"; s.loader = "fabric"; s.valid = true;

    // Étapes : putFile -> upsertAddressIndex -> deleteFile -> removeFromIndex -> vérif.
    int step = 0;
    QObject::connect(gh, &GitHubClient::filePut, &app, [&](const QString &) {
        if (step == 0) { step = 1; out << "[1] manifeste écrit\n"; gh->upsertAddressIndex(s.address, sid, "test"); }
    });
    QObject::connect(gh, &GitHubClient::indexUpdated, &app, [&]() {
        if (step == 1) { step = 2; out << "[2] index ajouté\n"; gh->deleteFile("servers/" + sid + ".json", "test del"); }
        else if (step == 3) {
            out << "[4] index nettoyé → vérification finale…\n";
            gh->fetchServer(sid);   // doit échouer (404)
        }
    });
    QObject::connect(gh, &GitHubClient::fileDeleted, &app, [&](const QString &) {
        if (step == 2) { step = 3; out << "[3] manifeste supprimé\n"; gh->removeFromIndex(sid, "test rmidx"); }
    });
    QObject::connect(gh, &GitHubClient::serverFetched, &app, [&](const ServerInfo &) {
        out << "❌ le serveur existe encore (suppression KO)\n"; app.exit(1);
    });
    QObject::connect(gh, &GitHubClient::errorOccurred, &app, [&](const QString &e) {
        if (step == 3) { out << "✅ serveur bien supprimé (404 attendu) : " << e.left(40) << "\n"; app.exit(0); }
        else { out << "❌ erreur inattendue à l'étape " << step << " : " << e << "\n"; app.exit(2); }
    });
    QObject::connect(gh, &GitHubClient::writeError, &app, [&](const QString &e) {
        out << "❌ writeError étape " << step << " : " << e << "\n"; app.exit(3);
    });

    gh->putFile("servers/" + sid + ".json",
                QJsonDocument(serverToJson(s)).toJson(QJsonDocument::Indented), "test pub");
    QTimer::singleShot(30000, &app, [&]{ out << "timeout\n"; app.exit(9); });
    return app.exec();
}
