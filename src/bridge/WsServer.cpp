#include "bridge/WsServer.h"

#include <QJsonDocument>
#include <QWebSocket>
#include <QWebSocketServer>

#include "bridge/Bridge.h"

namespace lami {

WsServer::WsServer(quint16 port, QObject *parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(QStringLiteral("LAMI"),
                                    QWebSocketServer::NonSecureMode, this))
    , m_bridge(new Bridge(this))
    , m_port(port)
{
    connect(m_server, &QWebSocketServer::newConnection, this, &WsServer::onNewConnection);

    // Réponses et événements du pont → renvoyés à l'UI.
    connect(m_bridge, &Bridge::response, this, &WsServer::broadcast);
    connect(m_bridge, &Bridge::event, this, &WsServer::broadcast);
}

bool WsServer::listen()
{
    // Écoute UNIQUEMENT en local (jamais exposé au réseau).
    if (!m_server->listen(QHostAddress::LocalHost, m_port))
        return false;
    m_port = m_server->serverPort();
    return true;
}

quint16 WsServer::port() const
{
    return m_port;
}

void WsServer::onNewConnection()
{
    QWebSocket *client = m_server->nextPendingConnection();
    m_clients << client;

    connect(client, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isObject())
            m_bridge->handle(doc.object());
    });
    connect(client, &QWebSocket::disconnected, this, [this, client]() {
        m_clients.removeAll(client);
        client->deleteLater();
    });
}

void WsServer::broadcast(const QJsonObject &message)
{
    const QString text = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
    for (QWebSocket *c : m_clients)
        c->sendTextMessage(text);
}

} // namespace lami
