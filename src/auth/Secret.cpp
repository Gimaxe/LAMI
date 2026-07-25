// libsecret (glib/gio) DOIT être inclus AVANT Qt : sinon collision de macros
// ("expected unqualified-id before 'public'"). On teste __linux__ (symbole du
// compilateur), Q_OS_* n'étant pas encore défini. Windows garde son ordre
// habituel (windows.h APRÈS Qt), plus bas, pour ne pas casser le build MSVC.
#if defined(__linux__) && defined(LAMI_HAVE_LIBSECRET)
#  include <libsecret/secret.h>
#endif

#include "auth/Secret.h"

#include <QDir>
#include <QFile>

#include "core/AppConfig.h"

namespace lami {
namespace secret {

namespace {
// Repli non chiffré (dernier recours) : fichier dans le dossier de données.
QString fallbackFile(const QString &name)
{
    return QDir(config::defaultDataRoot()).filePath("." + name + ".plain");
}
bool saveFallback(const QString &name, const QString &value)
{
    QDir().mkpath(config::defaultDataRoot());
    QFile f(fallbackFile(name));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(value.toUtf8());
    f.close();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);  // 600
    return true;
}
QString loadFallback(const QString &name) { return config::readFileTrimmed(fallbackFile(name)); }
void clearFallback(const QString &name) { QFile::remove(fallbackFile(name)); }
} // namespace

#if defined(Q_OS_WIN)
// ----------------------------------------------------------------- Windows DPAPI
#include <windows.h>
#include <dpapi.h>
namespace {
QString blobFile(const QString &name)
{
    return QDir(config::defaultDataRoot()).filePath("." + name + ".dpapi");
}
}
bool save(const QString &name, const QString &value)
{
    const QByteArray v = value.toUtf8();
    DATA_BLOB in{static_cast<DWORD>(v.size()),
                 reinterpret_cast<BYTE *>(const_cast<char *>(v.constData()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"LAMI", nullptr, nullptr, nullptr, 0, &out))
        return saveFallback(name, value);
    const QByteArray enc(reinterpret_cast<char *>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    QDir().mkpath(config::defaultDataRoot());
    QFile f(blobFile(name));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(enc);
    return true;
}
QString load(const QString &name)
{
    QFile f(blobFile(name));
    if (!f.open(QIODevice::ReadOnly)) return loadFallback(name);
    QByteArray enc = f.readAll();
    DATA_BLOB in{static_cast<DWORD>(enc.size()), reinterpret_cast<BYTE *>(enc.data())};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        return {};
    const QString v = QString::fromUtf8(reinterpret_cast<char *>(out.pbData),
                                        static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return v;
}
void clear(const QString &name) { QFile::remove(blobFile(name)); clearFallback(name); }

#elif defined(Q_OS_LINUX) && defined(LAMI_HAVE_LIBSECRET)
// ----------------------------------------------------------------- Linux libsecret
namespace {
const SecretSchema *schema()
{
    // Init partielle : les attributs restants et les champs réservés sont mis à
    // zéro par l'agrégat (motif standard libsecret).
    static const SecretSchema s = {
        "com.lami.Secret", SECRET_SCHEMA_NONE,
        {{"name", SECRET_SCHEMA_ATTRIBUTE_STRING}}};
    return &s;
}
}
bool save(const QString &name, const QString &value)
{
    GError *err = nullptr;
    gboolean ok = secret_password_store_sync(
        schema(), SECRET_COLLECTION_DEFAULT, "LAMI", value.toUtf8().constData(),
        nullptr, &err, "name", name.toUtf8().constData(), nullptr);
    if (err) { g_error_free(err); ok = FALSE; }
    return ok ? true : saveFallback(name, value);
}
QString load(const QString &name)
{
    GError *err = nullptr;
    gchar *pw = secret_password_lookup_sync(schema(), nullptr, &err,
                                            "name", name.toUtf8().constData(), nullptr);
    if (err) { g_error_free(err); return loadFallback(name); }
    if (!pw) return loadFallback(name);
    const QString v = QString::fromUtf8(pw);
    secret_password_free(pw);
    return v;
}
void clear(const QString &name)
{
    GError *err = nullptr;
    secret_password_clear_sync(schema(), nullptr, &err, "name", name.toUtf8().constData(), nullptr);
    if (err) g_error_free(err);
    clearFallback(name);
}

#else
// ----------------------------------------------------------------- Repli (fichier)
bool save(const QString &name, const QString &value) { return saveFallback(name, value); }
QString load(const QString &name) { return loadFallback(name); }
void clear(const QString &name) { clearFallback(name); }
#endif

} // namespace secret
} // namespace lami
