#include "minecraft/Downloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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

void Downloader::pump()
{
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

void Downloader::startOne(const DownloadTask &task)
{
    QNetworkRequest req{QUrl(task.url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    for (const auto &h : task.headers)
        req.setRawHeader(h.first, h.second);

    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, task]() {
        reply->deleteLater();
        --m_active;

        if (reply->error() != QNetworkReply::NoError) {
            onOneDone(false, task.dest, reply->errorString(), task.size);
            pump();
            return;
        }

        const QByteArray data = reply->readAll();

        QDir().mkpath(QFileInfo(task.dest).absolutePath());
        QFile out(task.dest);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            onOneDone(false, task.dest, "écriture impossible", task.size);
            pump();
            return;
        }
        out.write(data);
        out.close();

        // Vérification d'intégrité.
        if (!task.expectedHash.isEmpty()) {
            const QString got = QString::fromLatin1(
                QCryptographicHash::hash(data, task.algo).toHex());
            if (got.compare(task.expectedHash, Qt::CaseInsensitive) != 0) {
                QFile::remove(task.dest);  // ne pas garder un fichier corrompu
                onOneDone(false, task.dest, "SHA1 non conforme", task.size);
                pump();
                return;
            }
        }

        onOneDone(true, task.dest, {}, task.size);
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
