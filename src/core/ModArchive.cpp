#include "core/ModArchive.h"

#include <QDir>
#include <QFileInfo>

#include "miniz.h"

namespace lami {
namespace ModArchive {

QStringList extract(const QString &zipPath, const QString &destDir,
                    const QString &ext, QString *error)
{
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
