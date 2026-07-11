// Backend LAMI : serveur WebSocket local qui expose le pont C++ à l'UI web.
// Lancé par la coquille webview (ou seul, pour le dev). Écoute sur 127.0.0.1.

#include <QCoreApplication>
#include <QTextStream>

#include "bridge/WsServer.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    // Port fixe par défaut (la coquille l'injecte dans le JS) ; surchargeable.
    quint16 port = 8770;
    const QStringList args = app.arguments();
    const int i = args.indexOf("--port");
    if (i >= 0 && i + 1 < args.size())
        port = static_cast<quint16>(args[i + 1].toUShort());

    lami::WsServer server(port);
    if (!server.listen()) {
        out << "ERREUR : impossible d'écouter sur le port " << port << "\n";
        return 1;
    }

    // Ligne lisible par la coquille pour récupérer le port effectif.
    out << "LAMI_BACKEND_READY " << server.port() << "\n";
    out.flush();

    return app.exec();
}
