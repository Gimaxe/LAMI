#pragma once

#include <QMainWindow>

#include "auth/MicrosoftAuth.h"  // MinecraftSession

class QStackedWidget;

namespace lami {

class GitHubClient;
class LoginPage;
class HomePage;

// Fenêtre principale : empile l'écran de connexion puis l'accueil.
// Après connexion, résout le rôle (roles.json) avant d'afficher l'accueil.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void onLoggedIn(const MinecraftSession &session);

    QStackedWidget *m_stack;
    LoginPage      *m_login;
    HomePage       *m_home;
    GitHubClient   *m_gh;
    MinecraftSession m_session;
};

} // namespace lami
