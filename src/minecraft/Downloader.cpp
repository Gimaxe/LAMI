#include "minecraft/Downloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace lami {

Downloader::Downloader(int maxParallel, QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_maxParallel(qMax(1, maxParallel))
{
}

QString Downloader::hashFile(const QString &path, QCryptographicHash::Algorithm algo)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(algo);
    if (!hash.addData(&f))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString Downloader::sha1File(const QString &path)
{
    return hashFile(path, QCryptographicHash::Sha1);
}

bool Downloader::alreadyValid(const DownloadTask &task)
{
    if (!QFileInfo::exists(task.dest))
        return false;
    if (task.expectedHash.isEmpty())
        return true;  // pas de hash à vérifier : présence = suffisant
    return hashFile(task.dest, task.algo).compare(task.expectedHash, Qt::CaseInsensitive) == 0;
}

void Downloader::start(const QVector<DownloadTask> &tasks)
{
    m_queue.clear();
    m_totalBytes = 0;
    for (const DownloadTask &t : tasks) {
        m_queue.enqueue(t);
        m_totalBytes += qMax<qint64>(0, t.size);
    }

    m_total  = tasks.size();
    m_done   = m_ok = m_failed = m_active = 0;
    m_doneBytes = 0;

    if (m_total == 0) {
        emit finished(0, 0);
        return;
    }
    pump();
}

void Downloader::abort()
{
    if (m_aborted)
        return;
    m_aborted = true;
    m_queue.clear();
    // abort() déclenche finished() de chaque reply avec OperationCanceledError ;
    // le handler voit m_aborted et s'arrête sans compter ni écrire.
    const auto replies = m_inFlight;
    for (QNetworkReply *r : replies)
        r->abort();
    m_inFlight.clear();
    emit aborted();
}

void Downloader::pump()
{
    if (m_aborted)
        return;
    while (m_active < m_maxParallel && !m_queue.isEmpty()) {
        const DownloadTask task = m_queue.dequeue();

        if (alreadyValid(task)) {
            // Déjà présent et conforme : compté comme succès sans réseau.
            onOneDone(true, task.dest, {}, task.size);
            continue;
        }
        ++m_active;
        startOne(task);
    }
}

// Remet le fichier en file après une courte pause (300 ms, 600 ms…) tant qu'il
// reste des essais. Le compteur global n'est PAS incrémenté : la tâche n'est ni
// réussie ni définitivement échouée tant qu'on réessaie.
bool Downloader::scheduleRetry(const DownloadTask &task)
{
    if (m_aborted || task.attempts >= kMaxAttempts)
        return false;
    DownloadTask next = task;
    QTimer::singleShot(300 * next.attempts, this, [this, next]() {
        if (m_aborted)
            return;
        m_queue.enqueue(next);
        pump();
    });
    return true;
}

void Downloader::startOne(const DownloadTask &task)
{
    QNetworkRequest req{QUrl(task.url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    for (const auto &h : task.headers)
        req.setRawHeader(h.first, h.second);

    QNetworkReply *reply = m_net->get(req);
    m_inFlight.append(reply);
    // La tâche porte le compteur d'essais : incrémenté ici, avant l'envoi.
    DownloadTask attempt = task;
    attempt.attempts += 1;
    connect(reply, &QNetworkReply::finished, this, [this, reply, attempt]() {
        reply->deleteLater();
        m_inFlight.removeOne(reply);
        --m_active;

        if (m_aborted)
            return;   // annulé : on ne compte plus rien, l'appelant a repris la main

        // Échec réseau (coupure, délai dépassé, 5xx…) : on réessaie avant de
        // déclarer le fichier perdu — c'est ce qui rendait une installation
        // « incomplète » alors qu'un simple second essai suffisait.
        if (reply->error() != QNetworkReply::NoError) {
            if (!scheduleRetry(attempt))
                onOneDone(false, attempt.dest, reply->errorString(), attempt.size);
            pump();
            return;
        }

        const QByteArray data = reply->readAll();

        QDir().mkpath(QFileInfo(attempt.dest).absolutePath());
        QFile out(attempt.dest);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            onOneDone(false, attempt.dest, "écriture impossible", attempt.size);
            pump();
            return;   // problème disque : réessayer n'y changerait rien
        }
        out.write(data);
        out.close();

        // Vérification d'intégrité (transfert tronqué/corrompu → nouvel essai).
        if (!attempt.expectedHash.isEmpty()) {
            const QString got = QString::fromLatin1(
                QCryptographicHash::hash(data, attempt.algo).toHex());
            if (got.compare(attempt.expectedHash, Qt::CaseInsensitive) != 0) {
                QFile::remove(attempt.dest);  // ne pas garder un fichier corrompu
                if (!scheduleRetry(attempt))
                    onOneDone(false, attempt.dest, "empreinte non conforme", attempt.size);
                pump();
                return;
            }
        }

        onOneDone(true, attempt.dest, {}, attempt.size);
        pump();
    });
}

void Downloader::onOneDone(bool ok, const QString &dest, const QString &reason, qint64 size)
{
    ++m_done;
    m_doneBytes += qMax<qint64>(0, size);
    if (ok) {
        ++m_ok;
    } else {
        ++m_failed;
        emit fileFailed(dest, reason);
    }
    emit progress(m_done, m_total);
    emit progressBytes(m_doneBytes, m_totalBytes);

    if (m_done == m_total)
        emit finished(m_ok, m_failed);
}

} // namespace lami
