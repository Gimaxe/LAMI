#include "ui/LoginPage.h"

#include <QDesktopServices>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "core/AppConfig.h"

namespace lami {

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(18);
    layout->addStretch();

    m_title = new QLabel("LAMI", this);
    QFont tf = m_title->font(); tf.setPointSize(28); tf.setBold(true);
    m_title->setFont(tf);
    m_title->setAlignment(Qt::AlignCenter);

    auto *subtitle = new QLabel("Launcher Atraxe MInecraft", this);
    subtitle->setAlignment(Qt::AlignCenter);

    m_button = new QPushButton("Se connecter avec Microsoft", this);
    m_button->setMinimumHeight(40);

    m_codeBox = new QLabel(this);
    m_codeBox->setAlignment(Qt::AlignCenter);
    m_codeBox->setTextFormat(Qt::RichText);
    m_codeBox->setOpenExternalLinks(true);
    m_codeBox->setWordWrap(true);
    m_codeBox->setVisible(false);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setVisible(false);

    m_status = new QLabel("Connecte-toi avec ton compte Minecraft pour commencer.", this);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);

    layout->addWidget(m_title);
    layout->addWidget(subtitle);
    layout->addSpacing(10);
    layout->addWidget(m_button);
    layout->addWidget(m_codeBox);
    layout->addWidget(m_progress);
    layout->addWidget(m_status);
    layout->addStretch();

    connect(m_button, &QPushButton::clicked, this, &LoginPage::startLogin);
}

void LoginPage::setBusy(bool busy)
{
    m_button->setEnabled(!busy);
    m_progress->setVisible(busy);
}

void LoginPage::startLogin()
{
    const QString clientId = config::clientId();
    if (clientId.isEmpty()) {
        m_status->setText("<b>Configuration manquante :</b> ~/LAMI/.client_id introuvable.");
        return;
    }

    setBusy(true);
    m_status->setText("Ouverture de la connexion Microsoft…");
    m_codeBox->setVisible(false);

    m_auth = new MicrosoftAuth(clientId, this);

    connect(m_auth, &MicrosoftAuth::deviceCodeReady, this,
            [this](const QString &code, const QString &uri) {
        m_codeBox->setText(QString(
            "Va sur <a href=\"%1\">%1</a><br>et saisis le code :"
            "<br><span style='font-size:22px; font-weight:bold; letter-spacing:3px;'>%2</span>")
                .arg(uri, code));
        m_codeBox->setVisible(true);
        m_status->setText("En attente de validation…");
        QDesktopServices::openUrl(QUrl(uri));  // ouvre la page automatiquement
    });

    connect(m_auth, &MicrosoftAuth::progress, this,
            [this](const QString &s) { m_status->setText(s); });

    connect(m_auth, &MicrosoftAuth::authenticated, this,
            [this](const MinecraftSession &session) {
        setBusy(false);
        m_codeBox->setVisible(false);
        m_status->setText(QString("Connecté : %1").arg(session.name));
        emit loggedIn(session);
    });

    connect(m_auth, &MicrosoftAuth::failed, this, [this](const QString &msg) {
        setBusy(false);
        m_codeBox->setVisible(false);
        m_status->setText("<b>Échec :</b> " + msg.toHtmlEscaped());
        m_auth->deleteLater();
        m_auth = nullptr;
    });

    m_auth->start();
}

} // namespace lami
