#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>

class QWebSocketServer;
class QWebSocket;

namespace lami {

class Bridge;

// Serveur WebSocket local (127.0.0.1) qui relaie les messages entre l'UI (JS
// dans la webview) et le Bridge C++. Le JS envoie des requêtes JSON, reçoit les
// réponses et les événements poussés. Testable en headless (client QWebSocket).
class WsServer : public QObject
{
    Q_OBJECT

public:
    explicit WsServer(quint16 port = 0, QObject *parent = nullptr);

    bool listen();          // démarre l'écoute ; false si échec
    quint16 port() const;   // port effectif (utile si port=0 → auto)

private:
    void onNewConnection();
    void broadcast(const QJsonObject &message);

    QWebSocketServer *m_server;
    QList<QWebSocket *> m_clients;
    Bridge *m_bridge;
    quint16 m_port;
};

} // namespace lami
