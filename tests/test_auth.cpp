// Test interactif du module auth : lance le device code flow et affiche le
// code à saisir. Nécessite un client_id dans ~/LAMI/.client_id.
// Headless-friendly : tu ouvres l'URL + saisis le code sur n'importe quel appareil.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "auth/MicrosoftAuth.h"

using namespace lami;

static QString readClientId()
{
    QFile f(QDir::homePath() + "/LAMI/.client_id");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const QString clientId = readClientId();
    if (clientId.isEmpty()) {
        out << "ERREUR: ~/LAMI/.client_id introuvable ou vide.\n"
            << "Enregistre l'app Azure, puis :\n"
            << "  echo 'TON_CLIENT_ID' > ~/LAMI/.client_id\n";
        return 1;
    }

    auto *auth = new MicrosoftAuth(clientId, &app);

    QObject::connect(auth, &MicrosoftAuth::deviceCodeReady, &app,
        [&](const QString &code, const QString &uri) {
            out << "\n==============================================\n"
                << "  Ouvre : " << uri << "\n"
                << "  Code   : " << code << "\n"
                << "==============================================\n"
                << "(en attente de validation…)\n";
            out.flush();
        });

    QObject::connect(auth, &MicrosoftAuth::progress, &app, [&](const QString &s) {
        out << "  … " << s << "\n"; out.flush();
    });

    QObject::connect(auth, &MicrosoftAuth::authenticated, &app,
        [&](const MinecraftSession &s) {
            out << "\n✅ Connecté !\n"
                << "  Pseudo : " << s.name << "\n"
                << "  UUID   : " << s.uuid << "\n"
                << "  Token Minecraft : " << s.minecraftToken.left(16) << "… ("
                << s.minecraftToken.size() << " car.)\n";
            out.flush();
            app.quit();
        });

    QObject::connect(auth, &MicrosoftAuth::failed, &app, [&](const QString &msg) {
        out << "\n❌ " << msg << "\n"; out.flush();
        app.exit(1);
    });

    out << "Démarrage de l'authentification Microsoft…\n"; out.flush();
    auth->start();
    return app.exec();
}
