#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace lami {

// Session Minecraft obtenue après authentification complète.
struct MinecraftSession {
    QString uuid;            // UUID Minecraft (identité pour les rôles)
    QString name;            // pseudo
    QString minecraftToken;  // jeton pour lancer/rejoindre (Authorization: Bearer)
    QString msRefreshToken;  // pour re-login silencieux plus tard
    bool    valid = false;
};

// Authentification Microsoft via DEVICE CODE FLOW (adapté au headless).
// Chaîne : device code -> polling token MS -> Xbox Live -> XSTS ->
//          token Minecraft -> profil (UUID + pseudo).
// Réimplémente ce que faisait minecraft-java-core ; réf. Prism Launcher.
//
// Usage :
//   auto *auth = new MicrosoftAuth("<client_id>", this);
//   connect(auth, &MicrosoftAuth::deviceCodeReady, ...);  // afficher URL+code
//   connect(auth, &MicrosoftAuth::authenticated, ...);    // session prête
//   connect(auth, &MicrosoftAuth::failed, ...);
//   auth->start();
class MicrosoftAuth : public QObject
{
    Q_OBJECT

public:
    explicit MicrosoftAuth(QString clientId, QObject *parent = nullptr);

    // Lance le flux : demande un device code puis attend la validation.
    void start();

signals:
    // À afficher à l'utilisateur : il ouvre verificationUri et saisit userCode.
    void deviceCodeReady(const QString &userCode, const QString &verificationUri);
    // Progression textuelle (étapes Xbox/XSTS/Minecraft/profil).
    void progress(const QString &step);
    // Authentification réussie.
    void authenticated(const lami::MinecraftSession &session);
    // Échec (message lisible).
    void failed(const QString &message);

private:
    // Étapes de la chaîne.
    void requestDeviceCode();
    void pollForToken();
    void authenticateXboxLive(const QString &msAccessToken);
    void authorizeXsts(const QString &xblToken);
    void loginMinecraft(const QString &userHash, const QString &xstsToken);
    void fetchProfile(const QString &minecraftToken);

    // Helpers réseau.
    QNetworkReply *postForm(const QString &url, const QString &body);
    QNetworkReply *postJson(const QString &url, const QByteArray &json);
    void fail(QNetworkReply *reply, const QString &context);

    QNetworkAccessManager *m_net;
    QString m_clientId;

    // État du device code flow.
    QString m_deviceCode;
    QString m_refreshToken;
    int     m_intervalSec = 5;
    qint64  m_expiresAtMs = 0;
};

} // namespace lami
