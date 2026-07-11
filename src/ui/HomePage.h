#pragma once

#include <QWidget>

#include "auth/MicrosoftAuth.h"   // MinecraftSession
#include "github/Models.h"       // Role
#include "core/InstanceManager.h" // LaunchPlan

class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;

namespace lami {

// Écran d'accueil : identité + rôle, saisie d'une adresse de serveur, et
// préparation du lancement (plan). Le téléchargement/lancement réel viendra.
class HomePage : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);

    // Appelé après connexion : renseigne l'identité et le rôle.
    void setSession(const MinecraftSession &session, Role role);

private:
    void onJoin();
    void setBusy(bool busy);

    QLabel       *m_identity;
    QLineEdit    *m_address;
    QPushButton  *m_join;
    QProgressBar *m_progress;
    QLabel       *m_result;
    QLabel       *m_hostHint;

    InstanceManager *m_mgr = nullptr;
    MinecraftSession m_session;
    Role m_role = Role::Player;
};

} // namespace lami
