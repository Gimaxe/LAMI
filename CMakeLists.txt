cmake_minimum_required(VERSION 3.16)

project(LAMI
    VERSION 0.1.0
    DESCRIPTION "Launcher Atraxe MInecraft"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt : génération automatique du code MOC / UIC / RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Compatible Qt6 (préféré) et repli Qt5 si besoin
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network WebSockets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network WebSockets)

# Tous les .cpp du projet (à compléter au fur et à mesure)
set(LAMI_SOURCES
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/LoginPage.cpp
    src/ui/HomePage.cpp
    src/github/Models.cpp
    src/github/GitHubClient.cpp
    src/sync/SyncManager.cpp
    src/roles/Permissions.cpp
    src/auth/MicrosoftAuth.cpp
    src/minecraft/VersionInfo.cpp
    src/minecraft/MojangMeta.cpp
    src/minecraft/Downloader.cpp
    src/minecraft/LaunchBuilder.cpp
    src/minecraft/FabricMeta.cpp
    src/core/InstanceManager.cpp
    src/core/Publisher.cpp
    src/core/ModScanner.cpp
    src/core/ModArchive.cpp
    third_party/miniz/miniz.c
)

set(LAMI_HEADERS
    src/ui/MainWindow.h
    src/ui/LoginPage.h
    src/ui/HomePage.h
    src/github/Models.h
    src/github/GitHubClient.h
    src/sync/SyncManager.h
    src/roles/Permissions.h
    src/auth/MicrosoftAuth.h
    src/minecraft/VersionInfo.h
    src/minecraft/MojangMeta.h
    src/minecraft/Downloader.h
    src/minecraft/LaunchBuilder.h
    src/minecraft/FabricMeta.h
    src/core/InstanceManager.h
    src/core/Publisher.h
    src/core/ModScanner.h
    src/core/ModArchive.h
)

qt_standard_project_setup()

qt_add_executable(lami
    ${LAMI_SOURCES}
    ${LAMI_HEADERS}
)

target_include_directories(lami PRIVATE src third_party/miniz)

target_link_libraries(lami PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

# Sur Windows : pas de console derrière la fenêtre GUI
set_target_properties(lami PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# Sources nécessaires au pont (Bridge) et donc au backend + à ses tests.
set(BRIDGE_CORE_SOURCES
    src/bridge/Bridge.cpp
    src/auth/MicrosoftAuth.cpp
    src/github/Models.cpp
    src/github/GitHubClient.cpp
    src/sync/SyncManager.cpp
    src/roles/Permissions.cpp
    src/core/InstanceManager.cpp
    src/core/Publisher.cpp
    src/core/ModScanner.cpp
    src/core/ModArchive.cpp
    third_party/miniz/miniz.c
    src/minecraft/VersionInfo.cpp
    src/minecraft/MojangMeta.cpp
    src/minecraft/FabricMeta.cpp
    src/minecraft/Downloader.cpp
    src/minecraft/LaunchBuilder.cpp
)

# --- Cible de test console (module github) ---
option(LAMI_BUILD_TESTS "Compiler les tests console" ON)
if(LAMI_BUILD_TESTS)
    qt_add_executable(lami_test_github
        tests/test_github.cpp
        src/github/Models.cpp
        src/github/GitHubClient.cpp
    )
    target_include_directories(lami_test_github PRIVATE src)
    target_link_libraries(lami_test_github PRIVATE Qt${QT_VERSION_MAJOR}::Network)

    qt_add_executable(lami_test_sync
        tests/test_sync.cpp
        src/github/Models.cpp
        src/sync/SyncManager.cpp
    )
    target_include_directories(lami_test_sync PRIVATE src)
    target_link_libraries(lami_test_sync PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    qt_add_executable(lami_test_roles
        tests/test_roles.cpp
        src/github/Models.cpp
        src/roles/Permissions.cpp
    )
    target_include_directories(lami_test_roles PRIVATE src)
    target_link_libraries(lami_test_roles PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    qt_add_executable(lami_test_admin
        tests/test_admin.cpp
        src/github/Models.cpp
        src/github/GitHubClient.cpp
        src/roles/Permissions.cpp
    )
    target_include_directories(lami_test_admin PRIVATE src)
    target_link_libraries(lami_test_admin PRIVATE Qt${QT_VERSION_MAJOR}::Network)

    qt_add_executable(lami_test_auth
        tests/test_auth.cpp
        src/auth/MicrosoftAuth.cpp
    )
    target_include_directories(lami_test_auth PRIVATE src)
    target_link_libraries(lami_test_auth PRIVATE Qt${QT_VERSION_MAJOR}::Network)

    qt_add_executable(lami_test_minecraft
        tests/test_minecraft.cpp
        src/minecraft/VersionInfo.cpp
        src/minecraft/MojangMeta.cpp
    )
    target_include_directories(lami_test_minecraft PRIVATE src)
    target_link_libraries(lami_test_minecraft PRIVATE Qt${QT_VERSION_MAJOR}::Network)

    qt_add_executable(lami_test_download
        tests/test_download.cpp
        src/minecraft/VersionInfo.cpp
        src/minecraft/MojangMeta.cpp
        src/minecraft/Downloader.cpp
    )
    target_include_directories(lami_test_download PRIVATE src)
    target_link_libraries(lami_test_download PRIVATE Qt${QT_VERSION_MAJOR}::Network)

    qt_add_executable(lami_test_launch
        tests/test_launch.cpp
        src/minecraft/VersionInfo.cpp
        src/minecraft/LaunchBuilder.cpp
    )
    target_include_directories(lami_test_launch PRIVATE src)
    target_link_libraries(lami_test_launch PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    qt_add_executable(lami_test_instance
        tests/test_instance.cpp
        src/core/InstanceManager.cpp
        src/github/Models.cpp
        src/github/GitHubClient.cpp
        src/sync/SyncManager.cpp
        src/minecraft/VersionInfo.cpp
        src/minecraft/MojangMeta.cpp
        src/minecraft/Downloader.cpp
        src/minecraft/LaunchBuilder.cpp
        src/minecraft/FabricMeta.cpp
    )
    target_include_directories(lami_test_instance PRIVATE src)
    target_link_libraries(lami_test_instance PRIVATE Qt${QT_VERSION_MAJOR}::Network)

    qt_add_executable(lami_test_fabric
        tests/test_fabric.cpp
        src/minecraft/VersionInfo.cpp
        src/minecraft/FabricMeta.cpp
    )
    target_include_directories(lami_test_fabric PRIVATE src)
    target_link_libraries(lami_test_fabric PRIVATE Qt${QT_VERSION_MAJOR}::Network)

    qt_add_executable(lami_test_publish
        tests/test_publish.cpp
        src/core/Publisher.cpp
        src/core/ModScanner.cpp
        src/core/ModArchive.cpp
        third_party/miniz/miniz.c
        src/sync/SyncManager.cpp
        src/github/Models.cpp
        src/github/GitHubClient.cpp
        src/roles/Permissions.cpp
    )
    target_include_directories(lami_test_publish PRIVATE src third_party/miniz)
    target_link_libraries(lami_test_publish PRIVATE Qt${QT_VERSION_MAJOR}::Network)

    qt_add_executable(lami_test_scan
        tests/test_scan.cpp
        src/core/ModScanner.cpp
        src/sync/SyncManager.cpp
        src/github/Models.cpp
    )
    target_include_directories(lami_test_scan PRIVATE src)
    target_link_libraries(lami_test_scan PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    qt_add_executable(lami_test_archive
        tests/test_archive.cpp
        src/core/ModArchive.cpp
        third_party/miniz/miniz.c
    )
    target_include_directories(lami_test_archive PRIVATE src third_party/miniz)
    target_link_libraries(lami_test_archive PRIVATE Qt${QT_VERSION_MAJOR}::Core)

    qt_add_executable(lami_test_bridge
        tests/test_bridge.cpp
        ${BRIDGE_CORE_SOURCES}
    )
    target_include_directories(lami_test_bridge PRIVATE src third_party/miniz)
    target_link_libraries(lami_test_bridge PRIVATE Qt${QT_VERSION_MAJOR}::Network)

    qt_add_executable(lami_test_ws
        tests/test_ws.cpp
        src/bridge/WsServer.cpp
        ${BRIDGE_CORE_SOURCES}
    )
    target_include_directories(lami_test_ws PRIVATE src third_party/miniz)
    target_link_libraries(lami_test_ws PRIVATE
        Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets)
endif()

# --- Backend WebSocket (exécutable) ---
qt_add_executable(lami_backend
    src/bridge/backend_main.cpp
    src/bridge/WsServer.cpp
    ${BRIDGE_CORE_SOURCES}
)
target_include_directories(lami_backend PRIVATE src third_party/miniz)
target_link_libraries(lami_backend PRIVATE
    Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets)

# Copie le dossier web/ (UI + libs vendorisées) à côté des binaires (file://).
add_custom_command(TARGET lami_backend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/web $<TARGET_FILE_DIR:lami_backend>/web)

# --- Coquille webview (desktop) ---
# Sur Linux : nécessite WebKitGTK (webkit2gtk-4.1). Windows/Mac : à adapter.
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(WEBKIT2GTK QUIET webkit2gtk-4.1)
        if(NOT WEBKIT2GTK_FOUND)
            pkg_check_modules(WEBKIT2GTK QUIET webkit2gtk-4.0)
        endif()
    endif()
    if(WEBKIT2GTK_FOUND)
        add_executable(lami_shell src/shell/shell_main.cpp)
        target_include_directories(lami_shell PRIVATE
            third_party/webview ${WEBKIT2GTK_INCLUDE_DIRS})
        target_link_libraries(lami_shell PRIVATE ${WEBKIT2GTK_LIBRARIES})
        target_compile_options(lami_shell PRIVATE ${WEBKIT2GTK_CFLAGS_OTHER})
        message(STATUS "lami_shell activé (WebKitGTK ${WEBKIT2GTK_VERSION})")
    else()
        message(STATUS "lami_shell désactivé (WebKitGTK absent)")
    endif()
elseif(WIN32)
    # Windows : la webview utilise WebView2 (Edge/Chromium). Le SDK est fourni
    # par le paquet nuget Microsoft.Web.WebView2, dont le chemin est passé via
    # -DWEBVIEW2_DIR=... (voir le workflow CI).
    add_executable(lami_shell WIN32 src/shell/shell_main.cpp)
    # Sous-système GUI (pas de console) mais point d'entrée main() standard.
    if(MSVC)
        target_link_options(lami_shell PRIVATE /ENTRY:mainCRTStartup)
    endif()
    target_include_directories(lami_shell PRIVATE third_party/webview)
    if(DEFINED WEBVIEW2_DIR)
        target_include_directories(lami_shell PRIVATE ${WEBVIEW2_DIR}/build/native/include)
        target_link_libraries(lami_shell PRIVATE
            ${WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib)
        message(STATUS "lami_shell (Windows/WebView2) activé")
    else()
        message(WARNING "WEBVIEW2_DIR non défini : lami_shell Windows peut ne pas lier")
    endif()
    target_link_libraries(lami_shell PRIVATE version shlwapi advapi32 ole32 oleaut32)
endif()
