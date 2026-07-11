// Coquille webview LAMI : ouvre une fenêtre native (WebView2 sur Windows,
// WebKitGTK sur Linux) affichant l'UI HTML, et démarre le backend WebSocket en
// enfant. L'UI (JS) se connecte au backend via ws://127.0.0.1:<port>.

#include <string>

#include "webview.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <climits>
#include <csignal>
#include <unistd.h>
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
#endif

} // namespace

int main()
{
    const std::string dir = exeDir();
    startBackend(dir);

    webview::webview w(/*debug=*/false, nullptr);
    w.set_title("LAMI — Launcher Atraxe MInecraft");
    w.set_size(1180, 760, WEBVIEW_HINT_NONE);
    w.set_size(900, 600, WEBVIEW_HINT_MIN);
    w.init("window.LAMI_WS_PORT = " + std::to_string(kWsPort) + ";");
    w.navigate(uiUrl(dir));
    w.run();

    stopBackend();
    return 0;
}
