#include "ui/HomePage.h"

#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/AppConfig.h"
#include "roles/Permissions.h"

namespace lami {

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(14);

    m_identity = new QLabel(this);
    m_identity->setTextFormat(Qt::RichText);

    auto *title = new QLabel("Rejoindre un serveur", this);
    QFont tf = title->font(); tf.setPointSize(15); tf.setBold(true);
    title->setFont(tf);

    m_address = new QLineEdit(this);
    m_address->setPlaceholderText("Adresse du serveur (ex. play.atraxe.fr)");
    m_address->setMinimumHeight(34);

    m_join = new QPushButton("Rejoindre", this);
    m_join->setMinimumHeight(38);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setVisible(false);

    m_result = new QLabel(this);
    m_result->setTextFormat(Qt::RichText);
    m_result->setWordWrap(true);

    m_hostHint = new QLabel(this);
    m_hostHint->setTextFormat(Qt::RichText);
    m_hostHint->setWordWrap(true);
    m_hostHint->setVisible(false);

    layout->addWidget(m_identity);
    layout->addSpacing(6);
    layout->addWidget(title);
    layout->addWidget(m_address);
    layout->addWidget(m_join);
    layout->addWidget(m_progress);
    layout->addWidget(m_result);
    layout->addStretch();
    layout->addWidget(m_hostHint);

    connect(m_join, &QPushButton::clicked, this, &HomePage::onJoin);
    connect(m_address, &QLineEdit::returnPressed, this, &HomePage::onJoin);
}

void HomePage::setSession(const MinecraftSession &session, Role role)
{
    m_session = session;
    m_role = role;

    m_identity->setText(QString("Connecté : <b>%1</b> &nbsp;·&nbsp; Rôle : <b>%2</b>")
                            .arg(session.name.toHtmlEscaped(), roleToString(role)));

    if (RoleResolver::can(role, Capability::PublishServer)) {
        m_hostHint->setText("★ Vous pouvez publier des serveurs "
                            "(interface de publication à venir).");
        m_hostHint->setVisible(true);
    }

    // Orchestrateur (une fois qu'on a une identité).
    m_mgr = new InstanceManager(config::owner(), config::repo(), config::branch(),
                                config::token(), config::dataRoot(), config::javaPath(), this);

    connect(m_mgr, &InstanceManager::progress, this,
            [this](const QString &s) { m_result->setText(s); });

    connect(m_mgr, &InstanceManager::failed, this, [this](const QString &m) {
        setBusy(false);
        m_result->setText("<b>Erreur :</b> " + m.toHtmlEscaped());
    });

    connect(m_mgr, &InstanceManager::planReady, this, [this](const LaunchPlan &p) {
        setBusy(false);
        qint64 bytes = 0;
        for (const DownloadTask &t : p.downloads) bytes += t.size;
        m_result->setText(QString(
            "<b>%1</b> — Minecraft %2 · %3<br>"
            "%4 fichier(s) à préparer (~%5 Mo)<br>"
            "<i>Téléchargement et lancement : bientôt "
            "(en attente de l'approbation Microsoft de l'app).</i>")
                .arg(p.server.name.toHtmlEscaped(),
                     p.version.id,
                     p.server.loader,
                     QString::number(p.downloads.size()),
                     QString::number(bytes / 1024 / 1024)));
    });
}

void HomePage::setBusy(bool busy)
{
    m_join->setEnabled(!busy);
    m_address->setEnabled(!busy);
    m_progress->setVisible(busy);
}

void HomePage::onJoin()
{
    const QString addr = m_address->text().trimmed();
    if (addr.isEmpty()) {
        m_result->setText("Entre l'adresse d'un serveur.");
        return;
    }
    if (!m_mgr)
        return;
    setBusy(true);
    m_result->setText(QString("Préparation de « %1 »…").arg(addr));
    m_mgr->planByAddress(addr, m_session);
}

} // namespace lami
