#pragma once

#include <QString>

// Stockage sécurisé d'un secret local (ex. refresh token Microsoft), chiffré par
// l'OS et lié à la session utilisateur :
//   - Windows : DPAPI (CryptProtectData) — indéchiffrable par un autre compte /
//     sur une autre machine.
//   - Linux   : libsecret (trousseau GNOME/KWallet) ; repli fichier si absent.
//   - Autres  : repli fichier (non chiffré) — mieux que rien, documenté.
namespace lami {
namespace secret {

// Chiffre et enregistre `value` sous `name`. Retourne false en cas d'échec total.
bool save(const QString &name, const QString &value);
// Déchiffre et renvoie la valeur (chaîne vide si absente/illisible).
QString load(const QString &name);
// Efface le secret.
void clear(const QString &name);

} // namespace secret
} // namespace lami
