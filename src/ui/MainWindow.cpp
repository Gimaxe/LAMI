#include "ui/MainWindow.h"

#include <QStackedWidget>

#include "core/AppConfig.h"
#include "github/GitHubClient.h"
#include "roles/Permissions.h"
#include "ui/HomePage.h"
#include "ui/LoginPage.h"

namespace lami {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("LAMI");
    resize(560, 460);

    m_stack = new QStackedWidget(this);
    m_login = new LoginPage(this);
    m_home  = new HomePage(this);
    m_stack->addWidget(m_login);
    m_stack->addWidget(m_home);
    setCentralWidget(m_stack);

    m_gh = new GitHubClient(config::owner(), config::repo(), config::branch(), this);
    if (!config::token().isEmpty())
        m_gh->setToken(config::token());

    connect(m_login, &LoginPage::loggedIn, this, &MainWindow::onLoggedIn);
}

void MainWindow::onLoggedIn(const MinecraftSession &session)
{
    m_session = session;

    // Résout le rôle depuis roles.json, puis affiche l'accueil.
    // En cas d'erreur réseau, on retombe sur Joueur pour rester utilisable.
    auto showHome = [this, session](Role role) {
        m_home->setSession(session, role);
        m_stack->setCurrentWidget(m_home);
    };

    connect(m_gh, &GitHubClient::rolesFetched, this,
            [this, session, showHome](const RoleTable &roles) {
        showHome(RoleResolver::roleFor(session.uuid, roles));
    });
    connect(m_gh, &GitHubClient::errorOccurred, this,
            [showHome](const QString &) { showHome(Role::Player); });

    m_gh->fetchRoles();
}

} // namespace lami
