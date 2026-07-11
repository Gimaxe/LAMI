#pragma once

#include <QObject>
#include <QString>

#include "minecraft/VersionInfo.h"

class QNetworkAccessManager;

namespace lami {

// Résout les métadonnées d'une version de Minecraft depuis les serveurs Mojang :
//   version_manifest_v2.json  ->  JSON de la version  ->  VersionInfo.
// Lecture seule, sans compte → testable en headless.
//
// (Le téléchargement effectif des fichiers et le lancement JVM viendront
//  ensuite, en s'appuyant sur le VersionInfo produit ici.)
class MojangMeta : public QObject
{
    Q_OBJECT

public:
    explicit MojangMeta(QObject *parent = nullptr);

    // Résout la version demandée (ex. "1.20.1"). "" ou "latest" => dernière release.
    void resolve(const QString &mcVersion);

signals:
    void resolved(const lami::VersionInfo &info);
    void errorOccurred(const QString &message);

private:
    void fetchVersionJson(const QString &url, const QString &id);

    QNetworkAccessManager *m_net;
};

} // namespace lami
