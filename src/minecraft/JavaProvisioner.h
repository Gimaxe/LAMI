#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace lami {

// Télécharge automatiquement le bon runtime Java (JRE Mojang) pour une version
// de Minecraft, pour que l'utilisateur n'ait rien à installer. Réutilise le
// Downloader. Un JRE déjà présent n'est pas re-téléchargé.
class JavaProvisioner : public QObject
{
    Q_OBJECT

public:
    explicit JavaProvisioner(QString dataRoot, QObject *parent = nullptr);

    // Provisionne le composant (ex. "java-runtime-gamma") → signal ready(javaPath).
    void provision(const QString &component);

    // Nom d'OS au sens du manifeste Java de Mojang.
    static QString osKey();

signals:
    void ready(const QString &javaPath);
    void progress(const QString &step);
    void errorOccurred(const QString &message);

private:
    QString runtimeDir(const QString &component) const;  // dataRoot/java/<component>
    QString existingJava(const QString &component) const; // "" si absent

    QNetworkAccessManager *m_net;
    QString m_dataRoot;
};

} // namespace lami
