// Test manuel du module github : lit roles.json et servers/exemple.json depuis
// le repo privé Gimaxe/LAMI-db via GitHubClient, en utilisant le token ~/LAMI/.token.
//
// But : prouver bout-en-bout que l'auth (repo privé) + le parsing marchent,
// avant de brancher le module sur l'interface.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include "github/GitHubClient.h"

using namespace lami;

static QString readToken()
{
    QFile f(QDir::homePath() + "/LAMI/.token");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const QString token = readToken();
    if (token.isEmpty()) {
        out << "ERREUR: ~/LAMI/.token introuvable ou vide.\n";
        return 1;
    }

    auto *gh = new GitHubClient("Gimaxe", "LAMI-db", "main", &app);
    gh->setToken(token);

    int pending = 3;   // roles + serveur par id + serveur par adresse
    auto done = [&]() { if (--pending <= 0) app.quit(); };

    QObject::connect(gh, &GitHubClient::rolesFetched, &app, [&](const RoleTable &roles) {
        out << "\n[roles.json] " << roles.size() << " role(s) :\n";
        for (auto it = roles.begin(); it != roles.end(); ++it)
            out << "  - " << it.key() << " -> " << roleToString(it.value()) << "\n";
        out.flush();
        done();
    });

    QObject::connect(gh, &GitHubClient::serverFetched, &app, [&](const ServerInfo &s) {
        out << "\n[servers/exemple.json] valide=" << (s.valid ? "oui" : "non") << "\n";
        out << "  id="   << s.id << "  name=" << s.name << "  addr=" << s.address << "\n";
        out << "  mc="   << s.minecraftVersion << "  loader=" << s.loader
            << " " << s.loaderVersion << "\n";
        out << "  mods (" << s.mods.size() << ") :\n";
        for (const ModEntry &m : s.mods)
            out << "    * " << m.file << "  sha256=" << m.sha256.left(12)
                << "…  size=" << m.size << "\n";
        out.flush();
        done();
    });

    QObject::connect(gh, &GitHubClient::errorOccurred, &app, [&](const QString &msg) {
        out << "\n[ERREUR] " << msg << "\n";
        out.flush();
        done();
    });

    // Filet de sécurité : ne pas rester bloqué si le réseau ne répond pas.
    QTimer::singleShot(15000, &app, [&]{ out << "\nTimeout.\n"; app.exit(2); });

    out << "Lecture du repo privé Gimaxe/LAMI-db…\n";
    out.flush();
    gh->fetchRoles();
    gh->fetchServer("exemple");
    // Résolution par adresse (IP/sous-domaine) via servers/index.json.
    gh->fetchServerByAddress("play.atraxe.fr");

    return app.exec();
}
