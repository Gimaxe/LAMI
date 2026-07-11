#include "auth/MicrosoftAuth.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <utility>

namespace lami {

namespace {
// Endpoints (comptes Microsoft personnels => tenant "consumers").
const char *kDeviceCodeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
const char *kTokenUrl      = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
const char *kXblUrl        = "https://user.auth.xboxlive.com/user/authenticate";
const char *kXstsUrl       = "https://xsts.auth.xboxlive.com/xsts/authorize";
const char *kMcLoginUrl    = "https://api.minecraftservices.com/authentication/login_with_xbox";
const char *kMcProfileUrl  = "https://api.minecraftservices.com/minecraft/profile";
// Scope Xbox + refresh token.
const char *kScope         = "XboxLive.signin offline_access";

QJsonObject jsonOf(QNetworkReply *reply)
{
    return QJsonDocument::fromJson(reply->readAll()).object();
}
} // namespace

MicrosoftAuth::MicrosoftAuth(QString clientId, QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_clientId(std::move(clientId))
{
}

QNetworkReply *MicrosoftAuth::postForm(const QString &url, const QString &body)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    return m_net->post(req, body.toUtf8());
}

QNetworkReply *MicrosoftAuth::postJson(const QString &url, const QByteArray &json)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    return m_net->post(req, json);
}

void MicrosoftAuth::fail(QNetworkReply *reply, const QString &context)
{
    const QString detail = reply ? reply->errorString() : QString();
    emit failed(context + (detail.isEmpty() ? QString() : (" : " + detail)));
}

void MicrosoftAuth::start()
{
    if (m_clientId.isEmpty()) {
        emit failed("client_id manquant : enregistre d'abord l'application Azure.");
        return;
    }
    requestDeviceCode();
}

// --- 1) Device code ---------------------------------------------------------
void MicrosoftAuth::requestDeviceCode()
{
    QUrlQuery q;
    q.addQueryItem("client_id", m_clientId);
    q.addQueryItem("scope", kScope);

    QNetworkReply *reply = postForm(kDeviceCodeUrl, q.toString(QUrl::FullyEncoded));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonObject o = jsonOf(reply);
        if (reply->error() != QNetworkReply::NoError && !o.contains("device_code")) {
            fail(reply, "Échec de la demande de device code");
            return;
        }

        m_deviceCode  = o.value("device_code").toString();
        m_intervalSec = o.value("interval").toInt(5);
        const int expiresIn = o.value("expires_in").toInt(900);
        m_expiresAtMs = QDateTime::currentMSecsSinceEpoch() + qint64(expiresIn) * 1000;

        const QString userCode = o.value("user_code").toString();
        const QString uri = o.value("verification_uri").toString(
            "https://www.microsoft.com/link");

        emit deviceCodeReady(userCode, uri);
        QTimer::singleShot(m_intervalSec * 1000, this, [this]() { pollForToken(); });
    });
}

// --- 2) Polling du token MS -------------------------------------------------
void MicrosoftAuth::pollForToken()
{
    if (QDateTime::currentMSecsSinceEpoch() > m_expiresAtMs) {
        emit failed("Délai expiré : le code n'a pas été validé à temps.");
        return;
    }

    QUrlQuery q;
    q.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    q.addQueryItem("client_id", m_clientId);
    q.addQueryItem("device_code", m_deviceCode);

    QNetworkReply *reply = postForm(kTokenUrl, q.toString(QUrl::FullyEncoded));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonObject o = jsonOf(reply);

        if (o.contains("access_token")) {
            m_refreshToken = o.value("refresh_token").toString();
            emit progress("Compte Microsoft validé, connexion à Xbox Live…");
            authenticateXboxLive(o.value("access_token").toString());
            return;
        }

        const QString err = o.value("error").toString();
        if (err == "authorization_pending") {
            QTimer::singleShot(m_intervalSec * 1000, this, [this]() { pollForToken(); });
        } else if (err == "slow_down") {
            m_intervalSec += 5;
            QTimer::singleShot(m_intervalSec * 1000, this, [this]() { pollForToken(); });
        } else if (err == "authorization_declined") {
            emit failed("Connexion refusée par l'utilisateur.");
        } else if (err == "expired_token") {
            emit failed("Code expiré, relance la connexion.");
        } else {
            emit failed("Erreur d'authentification Microsoft : "
                        + (err.isEmpty() ? reply->errorString() : err));
        }
    });
}

// --- 3) Xbox Live -----------------------------------------------------------
void MicrosoftAuth::authenticateXboxLive(const QString &msAccessToken)
{
    QJsonObject props{
        {"AuthMethod", "RPS"},
        {"SiteName", "user.auth.xboxlive.com"},
        {"RpsTicket", "d=" + msAccessToken},
    };
    QJsonObject body{
        {"Properties", props},
        {"RelyingParty", "http://auth.xboxlive.com"},
        {"TokenType", "JWT"},
    };

    QNetworkReply *reply = postJson(kXblUrl, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonObject o = jsonOf(reply);
        const QString token = o.value("Token").toString();
        if (reply->error() != QNetworkReply::NoError || token.isEmpty()) {
            fail(reply, "Échec de l'authentification Xbox Live");
            return;
        }
        emit progress("Xbox Live OK, autorisation XSTS…");
        authorizeXsts(token);
    });
}

// --- 4) XSTS ----------------------------------------------------------------
void MicrosoftAuth::authorizeXsts(const QString &xblToken)
{
    QJsonObject props{
        {"SandboxId", "RETAIL"},
        {"UserTokens", QJsonArray{xblToken}},
    };
    QJsonObject body{
        {"Properties", props},
        {"RelyingParty", "rp://api.minecraftservices.com/"},
        {"TokenType", "JWT"},
    };

    QNetworkReply *reply = postJson(kXstsUrl, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonObject o = jsonOf(reply);

        if (reply->error() != QNetworkReply::NoError || !o.contains("Token")) {
            // XSTS renvoie des codes XErr parlants (ex. pas de compte Xbox).
            const QString xerr = QString::number(o.value("XErr").toDouble(0), 'f', 0);
            QString hint;
            if (xerr == "2148916233")
                hint = " (ce compte Microsoft n'a pas de profil Xbox — crée-en un sur xbox.com)";
            else if (xerr == "2148916238")
                hint = " (compte enfant : doit être ajouté à une famille)";
            emit failed("Échec XSTS" + hint);
            return;
        }

        const QString token = o.value("Token").toString();
        const QString uhs = o.value("DisplayClaims").toObject()
                                 .value("xui").toArray().at(0).toObject()
                                 .value("uhs").toString();
        emit progress("XSTS OK, connexion à Minecraft…");
        loginMinecraft(uhs, token);
    });
}

// --- 5) Token Minecraft -----------------------------------------------------
void MicrosoftAuth::loginMinecraft(const QString &userHash, const QString &xstsToken)
{
    QJsonObject body{
        {"identityToken", QString("XBL3.0 x=%1;%2").arg(userHash, xstsToken)},
    };

    QNetworkReply *reply = postJson(kMcLoginUrl, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(raw).object();
        const QString token = o.value("access_token").toString();
        if (reply->error() != QNetworkReply::NoError || token.isEmpty()) {
            const int status = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit failed(QString("Échec login Minecraft [HTTP %1] : %2")
                            .arg(status)
                            .arg(QString::fromUtf8(raw.left(400))));
            return;
        }
        emit progress("Minecraft OK, récupération du profil…");
        fetchProfile(token);
    });
}

// --- 6) Profil (UUID + pseudo) ---------------------------------------------
void MicrosoftAuth::fetchProfile(const QString &minecraftToken)
{
    QNetworkRequest req{QUrl(kMcProfileUrl)};
    req.setRawHeader("Authorization", QByteArray("Bearer ") + minecraftToken.toUtf8());
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");

    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, minecraftToken]() {
        reply->deleteLater();
        const QJsonObject o = jsonOf(reply);
        const QString id = o.value("id").toString();
        if (reply->error() != QNetworkReply::NoError || id.isEmpty()) {
            // 404 ici = compte sans Minecraft (Java Edition) acheté.
            emit failed("Profil Minecraft introuvable "
                        "(ce compte possède-t-il bien Minecraft Java ?)");
            return;
        }

        MinecraftSession s;
        s.uuid = id;
        s.name = o.value("name").toString();
        s.minecraftToken = minecraftToken;
        s.msRefreshToken = m_refreshToken;
        s.valid = true;
        emit authenticated(s);
    });
}

} // namespace lami
