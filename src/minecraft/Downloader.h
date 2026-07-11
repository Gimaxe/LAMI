#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QObject>
#include <QPair>
#include <QQueue>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

namespace lami {

// Un fichier à télécharger (client.jar, une library, un asset, un mod du repo…).
struct DownloadTask {
    QString url;
    QString dest;          // chemin absolu de destination
    QString expectedHash;  // hash attendu (vide = pas de vérification)
    QCryptographicHash::Algorithm algo = QCryptographicHash::Sha1;  // SHA1 (Mojang) / SHA256 (mods)
    qint64  size = 0;
    // En-têtes HTTP additionnels (ex. Authorization Bearer pour le repo privé).
    QVector<QPair<QByteArray, QByteArray>> headers;
};

// Téléchargeur avec vérification SHA1 et parallélisme limité.
// - saute les fichiers déjà présents ET conformes (reprise / re-sync rapides) ;
// - vérifie le SHA1 après écriture, supprime le fichier si non conforme ;
// - émet la progression puis un bilan (ok / échecs).
// Réutilisable pour Mojang (client/libs/assets) comme pour les mods du repo.
class Downloader : public QObject
{
    Q_OBJECT

public:
    explicit Downloader(int maxParallel = 4, QObject *parent = nullptr);

    void start(const QVector<DownloadTask> &tasks);

    // Hash hex d'un fichier (par blocs). Vide si illisible.
    static QString hashFile(const QString &path, QCryptographicHash::Algorithm algo);
    static QString sha1File(const QString &path);  // raccourci SHA1

signals:
    void progress(int done, int total);
    void progressBytes(qint64 doneBytes, qint64 totalBytes);
    void fileFailed(const QString &dest, const QString &reason);
    void finished(int ok, int failed);

private:
    void pump();
    void startOne(const DownloadTask &task);
    void onOneDone(bool ok, const QString &dest, const QString &reason, qint64 size);
    static bool alreadyValid(const DownloadTask &task);

    QNetworkAccessManager *m_net;
    int m_maxParallel;
    int m_active = 0;
    int m_total  = 0;
    int m_done   = 0;
    int m_ok     = 0;
    int m_failed = 0;
    qint64 m_totalBytes = 0;
    qint64 m_doneBytes  = 0;
    QQueue<DownloadTask> m_queue;
};

} // namespace lami
