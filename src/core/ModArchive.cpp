#include "core/ModArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "miniz.h"

namespace lami {
namespace ModArchive {

namespace {
// Les 4 premiers octets d'un ZIP ("PK\x03\x04") et la signature 7z.
bool isSevenZip(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = f.read(6);
    // 7z : 37 7A BC AF 27 1C
    static const char sig[6] = {'\x37', '\x7A', '\xBC', '\xAF', '\x27', '\x1C'};
    return head.size() == 6 && memcmp(head.constData(), sig, 6) == 0;
}

// Localise un binaire 7-Zip (PATH, puis emplacements d'installation Windows).
QString find7z()
{
    for (const QString &n : {QStringLiteral("7z"), QStringLiteral("7za"), QStringLiteral("7zr")}) {
        const QString p = QStandardPaths::findExecutable(n);
        if (!p.isEmpty()) return p;
    }
#if defined(Q_OS_WIN)
    for (const QString &c : {QStringLiteral("C:/Program Files/7-Zip/7z.exe"),
                             QStringLiteral("C:/Program Files (x86)/7-Zip/7z.exe")})
        if (QFile::exists(c)) return c;
#endif
    return {};
}

// Extraction .7z via le binaire 7-Zip du système (aplatie comme le ZIP).
QStringList extract7z(const QString &archivePath, const QString &destDir,
                      const QString &ext, QString *error)
{
    auto fail = [&](const QString &m) -> QStringList { if (error) *error = m; return {}; };
    const QString bin = find7z();
    if (bin.isEmpty())
        return fail(QStringLiteral("Archive .7z : 7-Zip introuvable sur le système. "
                                   "Installe 7-Zip ou fournis un .zip."));
    QDir().mkpath(destDir);

    QProcess proc;
    // « e » = extraction à plat (ignore l'arborescence, comme notre ZIP).
    QStringList args{"e", "-y", "-o" + QDir::toNativeSeparators(destDir),
                     QDir::toNativeSeparators(archivePath)};
    proc.start(bin, args);
    if (!proc.waitForFinished(120000) || proc.exitCode() != 0)
        return fail(QStringLiteral("Extraction .7z échouée : %1")
                        .arg(QString::fromUtf8(proc.readAll()).right(300)));

    // Collecte les fichiers extraits (filtre par extension si demandé).
    QStringList out;
    const QStringList nameFilters = ext.isEmpty() ? QStringList() : QStringList{"*" + ext};
    for (const QFileInfo &fi : QDir(destDir).entryInfoList(nameFilters, QDir::Files))
        out << fi.fileName();
    if (out.isEmpty())
        return fail(QStringLiteral("Archive .7z vide (aucun fichier%1).")
                        .arg(ext.isEmpty() ? QString() : " " + ext));
    return out;
}
} // namespace

QStringList extract(const QString &zipPath, const QString &destDir,
                    const QString &ext, QString *error)
{
    // Détection par signature : .7z délégué au binaire 7-Zip, sinon ZIP via miniz.
    if (isSevenZip(zipPath))
        return extract7z(zipPath, destDir, ext, error);

    QStringList extracted;
    auto fail = [&](const QString &msg) -> QStringList {
        if (error) *error = msg;
        return {};
    };

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zipPath.toUtf8().constData(), 0))
        return fail(QStringLiteral("Impossible d'ouvrir le zip : %1").arg(zipPath));

    QDir().mkpath(destDir);

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st))
            continue;
        if (mz_zip_reader_is_file_a_directory(&zip, i))
            continue;

        // Aplati au nom de fichier ; filtre par extension si demandé.
        const QString base = QFileInfo(QString::fromUtf8(st.m_filename)).fileName();
        if (base.isEmpty())
            continue;
        if (!ext.isEmpty() && !base.endsWith(ext, Qt::CaseInsensitive))
            continue;

        const QString dest = QDir(destDir).filePath(base);
        if (!mz_zip_reader_extract_to_file(&zip, i, dest.toUtf8().constData(), 0)) {
            mz_zip_reader_end(&zip);
            return fail(QStringLiteral("Échec d'extraction de %1").arg(base));
        }
        extracted << base;
    }

    mz_zip_reader_end(&zip);
    return extracted;
}

QStringList extractJars(const QString &zipPath, const QString &destDir, QString *error)
{
    return extract(zipPath, destDir, ".jar", error);
}

} // namespace ModArchive
} // namespace lami
