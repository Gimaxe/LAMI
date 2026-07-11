#pragma once

#include <QWidget>

#include "auth/MicrosoftAuth.h"  // MinecraftSession

class QLabel;
class QPushButton;
class QProgressBar;

namespace lami {

// Écran de connexion Microsoft (device code flow) : l'utilisateur clique,
// on lui affiche une URL + un code à saisir, puis la session Minecraft arrive.
class LoginPage : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPage(QWidget *parent = nullptr);

signals:
    void loggedIn(const lami::MinecraftSession &session);

private:
    void startLogin();
    void setBusy(bool busy);

    QLabel       *m_title;
    QLabel       *m_status;
    QLabel       *m_codeBox;
    QPushButton  *m_button;
    QProgressBar *m_progress;
    MicrosoftAuth *m_auth = nullptr;
};

} // namespace lami
