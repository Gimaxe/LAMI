// Coquille webview LAMI : ouvre une fenêtre native (WebView2 sur Windows,
// WebKitGTK sur Linux) affichant l'UI HTML, et démarre le backend WebSocket en
// enfant. L'UI (JS) se connecte au backend via ws://127.0.0.1:<port>.

#include <cstdio>
#include <string>

#include "webview.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <climits>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include <gtk/gtk.h>   // icône de fenêtre (GtkWindow)
#endif

namespace {

constexpr int kWsPort = 8770;

// Dossier de l'exécutable courant (pour trouver le backend et le HTML à côté).
std::string exeDir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring w(buf, n);
    const auto slash = w.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        w = w.substr(0, slash);
    // UTF-16 → UTF-8
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len > 0 ? len - 1 : 0, '\0');
    if (len > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
#else
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return ".";
    buf[n] = '\0';
    std::string path(buf);
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
#endif
}

// URL file:// vers le HTML de l'UI (à côté de l'exécutable, dans web/).
std::string uiUrl(const std::string &dir)
{
#ifdef _WIN32
    std::string d = dir;
    for (char &c : d) if (c == '\\') c = '/';  // Windows : / dans les URLs
    return "file:///" + d + "/web/atraxe-ui.html";
#else
    return "file://" + dir + "/web/atraxe-ui.html";
#endif
}

#ifdef _WIN32
PROCESS_INFORMATION g_backend{};

void startBackend(const std::string &dir)
{
    std::string cmd = "\"" + dir + "\\lami_backend.exe\" --port " + std::to_string(kWsPort);
    STARTUPINFOA si{}; si.cb = sizeof(si);
    CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                   CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &g_backend);
}

void stopBackend()
{
    if (g_backend.hProcess) {
        TerminateProcess(g_backend.hProcess, 0);
        CloseHandle(g_backend.hProcess);
        CloseHandle(g_backend.hThread);
    }
}
#else
pid_t g_backend = 0;

void startBackend(const std::string &dir)
{
    g_backend = fork();
    if (g_backend == 0) {
        const std::string bin = dir + "/lami_backend";
        execl(bin.c_str(), "lami_backend", "--port", std::to_string(kWsPort).c_str(),
              static_cast<char *>(nullptr));
        _exit(127);
    }
}

void stopBackend()
{
    if (g_backend > 0)
        kill(g_backend, SIGTERM);
}

// Le moteur (lami_backend) est lié à Qt 6, contrairement à cette coquille :
// sans ces bibliothèques, la fenêtre s'ouvre mais rien ne fonctionne. On
// détecte le cas pour l'expliquer, au lieu du laconique « Backend non connecté ».
bool backendAlive()
{
    if (g_backend <= 0)
        return false;
    int status = 0;
    return waitpid(g_backend, &status, WNOHANG) == 0;
}

// Diagnostic lisible : bibliothèques manquantes rapportées par ldd.
std::string backendDiagnostic(const std::string &dir)
{
    std::string missing;
    const std::string cmd = "ldd '" + dir + "/lami_backend' 2>/dev/null | grep 'not found'";
    if (FILE *p = popen(cmd.c_str(), "r")) {
        char line[512];
        while (fgets(line, sizeof(line), p)) {
            std::string s(line);
            const auto cut = s.find(" =>");
            if (cut != std::string::npos) s = s.substr(0, cut);
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
            if (!s.empty()) missing += "<li>" + s + "</li>";
        }
        pclose(p);
    }
    return missing;
}
#endif

} // namespace

int main()
{
#ifndef _WIN32
    // Identité de l'application, AVANT toute initialisation GTK : c'est elle qui
    // devient l'app_id sous Wayland et le WM_CLASS sous X11. Le fichier
    // lami.desktop porte le même nom (StartupWMClass=lami), ce qui permet au
    // bureau d'afficher la bonne icône dans la barre des tâches.
    g_set_prgname("lami");
    g_set_application_name("LAMI");
#endif

    const std::string dir = exeDir();
    startBackend(dir);

    webview::webview w(/*debug=*/false, nullptr);
    w.set_title("LAMI");
    w.set_size(1180, 760, WEBVIEW_HINT_NONE);
    w.set_size(900, 600, WEBVIEW_HINT_MIN);

    // Icône de la fenêtre.
#ifdef _WIN32
    // La ressource .rc donne l'icône de l'.exe (Explorateur), mais la FENÊTRE
    // WebView2 ne l'hérite pas : on la pose explicitement (barre des tâches + titre).
    // On charge depuis le fichier .ico (fiable), avec repli sur la ressource.
    if (HWND hwnd = static_cast<HWND>(w.window())) {
        const std::string icoPath = dir + "\\web\\assets\\lami-icon.ico";
        const int len = MultiByteToWideChar(CP_UTF8, 0, icoPath.c_str(), -1, nullptr, 0);
        std::wstring wico(len > 0 ? len - 1 : 0, L'\0');
        if (len > 0)
            MultiByteToWideChar(CP_UTF8, 0, icoPath.c_str(), -1, wico.data(), len);

        auto setIcon = [&](int size, WPARAM which) {
            HICON hi = static_cast<HICON>(LoadImageW(nullptr, wico.c_str(), IMAGE_ICON,
                                                     size, size, LR_LOADFROMFILE));
            if (!hi)  // repli : ressource embarquée (id 1)
                hi = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                                   MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                   size, size, LR_SHARED));
            if (hi)
                SendMessageW(hwnd, WM_SETICON, which, reinterpret_cast<LPARAM>(hi));
        };
        setIcon(GetSystemMetrics(SM_CXICON), ICON_BIG);
        setIcon(GetSystemMetrics(SM_CXSMICON), ICON_SMALL);
    }
#else
    // Linux : l'icône de la barre des tâches vient du thème d'icônes, retrouvée
    // à partir de l'identité de l'application — PAS d'un fichier PNG.
    // gtk_window_set_icon_from_file() est ignoré sous Wayland : le compositeur
    // associe la fenêtre à son .desktop via l'app_id (Wayland) / WM_CLASS (X11),
    // d'où l'icône générique en roue dentée quand il ne trouve rien.
    // g_set_prgname() est posé avant, dans main().
    gtk_window_set_default_icon_name("lami");
    if (GtkWidget *win = static_cast<GtkWidget *>(w.window())) {
        gtk_window_set_icon_name(GTK_WINDOW(win), "lami");
        // Repli si le thème ne connaît pas encore « lami » (exécution portable,
        // sans installation) : on charge le PNG livré avec l'app.
        if (!gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "lami"))
            gtk_window_set_icon_from_file(GTK_WINDOW(win),
                                          (dir + "/web/assets/lami-icon.png").c_str(), nullptr);
    }
#endif

    // Contrôle de la fenêtre depuis l'UI (réglage « Fermeture du launcher ») :
    //   window.lamiWindowControl('["close"]' | '["hide"]' | '["show"]')
    // - close : quitte le launcher (le jeu, processus indépendant, survit) ;
    // - hide  : masque la fenêtre pendant que le jeu tourne ;
    // - show  : la réaffiche (à la fermeture du jeu).
    w.bind("lamiWindowControl", [&](const std::string &req) -> std::string {
        const bool isClose = req.find("close") != std::string::npos;
        const bool isHide  = req.find("hide")  != std::string::npos;
        const bool isShow  = req.find("show")  != std::string::npos;
        if (isClose) {
            w.dispatch([&w]() { w.terminate(); });
            return "true";
        }
#ifdef _WIN32
        if (HWND hwnd = static_cast<HWND>(w.window())) {
            if (isHide) ShowWindow(hwnd, SW_HIDE);
            else if (isShow) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
        }
#else
        if (GtkWidget *win = static_cast<GtkWidget *>(w.window())) {
            if (isHide) gtk_widget_hide(win);
            else if (isShow) { gtk_widget_show(win); gtk_window_present(GTK_WINDOW(win)); }
        }
#endif
        return "true";
    });

    w.init("window.LAMI_WS_PORT = " + std::to_string(kWsPort) + ";");

#ifndef _WIN32
    // Le moteur meurt aussitôt s'il manque Qt 6 : on affiche une page qui dit
    // quoi installer, plutôt que de laisser l'utilisateur face à une interface
    // inerte qui répond « Backend non connecté ».
    usleep(700 * 1000);
    if (!backendAlive()) {
        const std::string missing = backendDiagnostic(dir);
        const std::string page =
            "data:text/html;charset=utf-8,<html><body style='background:#0a0a0a;color:#e5e5e5;"
            "font-family:system-ui,sans-serif;padding:48px;line-height:1.6'>"
            "<h1 style='color:%2306b6d4'>Le moteur de LAMI n'a pas démarré</h1>"
            "<p>La fenêtre fonctionne, mais le composant qui télécharge et lance "
            "Minecraft n'a pas pu se lancer : il lui manque des bibliothèques "
            "<b>Qt&nbsp;6</b>.</p>"
            + (missing.empty() ? "" : "<p>Bibliothèques manquantes :</p><ul>" + missing + "</ul>")
            + "<p>Installe-les puis relance LAMI :</p>"
            "<pre style='background:%23171717;padding:16px;border-radius:12px;overflow:auto'>"
            "sudo apt install -y libqt6core6 libqt6network6 libqt6websockets6</pre>"
            "<p style='color:%23a3a3a3;font-size:14px'>Selon la distribution&nbsp;: "
            "<code>qt6-base-dev qt6-websockets-dev</code> (Debian/Ubuntu), "
            "<code>qt6-qtbase qt6-qtwebsockets</code> (Fedora), "
            "<code>qt6-base qt6-websockets</code> (Arch).</p>"
            "</body></html>";
        w.navigate(page);
        w.run();
        stopBackend();
        return 1;
    }
#endif

    w.navigate(uiUrl(dir));
    w.run();

    stopBackend();
    return 0;
}
