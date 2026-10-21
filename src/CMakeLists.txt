# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

# Everything that needs neither a window nor QML, so tests use it directly.
add_library(gfxtune_core STATIC
    core/vdf/KeyValues.cpp
    core/vdf/TextVdf.cpp
    core/vdf/BinaryVdf.cpp
    core/binaries/ExecutableImports.cpp
    core/settings/Availability.cpp
    core/settings/GameSettings.cpp
    core/settings/SettingsStore.cpp
    core/steam/AppInfo.cpp
    core/steam/Covers.cpp
    core/steam/Game.cpp
    core/steam/GameFiles.cpp
    core/steam/RunningGames.cpp
    core/steam/SteamLibrary.cpp
    core/steam/SteamWatcher.cpp
)
target_include_directories(gfxtune_core PUBLIC core)
target_link_libraries(gfxtune_core PUBLIC Qt6::Core)

# The objects the window talks to, as the QML module Gfxtune.Backend.
qt_add_library(gfxtune_app STATIC)
qt_add_qml_module(gfxtune_app
    URI Gfxtune.Backend
    VERSION 1.0
    SOURCES
        app/GameLibrary.h app/GameLibrary.cpp
        app/GameListModel.h app/GameListModel.cpp
        app/GameSettingsEditor.h app/GameSettingsEditor.cpp
)
target_include_directories(gfxtune_app PUBLIC app)
target_link_libraries(gfxtune_app PUBLIC gfxtune_core Qt6::Qml Qt6::Concurrent)

# The window, as the QML module Gfxtune.
qt_add_library(gfxtune_ui STATIC)
qt_add_qml_module(gfxtune_ui
    URI Gfxtune
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/GameListPane.qml
        qml/GameSettingsPane.qml
        qml/Cover.qml
        qml/Note.qml
        qml/SettingSwitch.qml
        qml/ValueComboBox.qml
)
target_link_libraries(gfxtune_ui PUBLIC gfxtune_app gfxtune_appplugin Qt6::Quick Qt6::QuickControls2)

qt_add_executable(gfxtune
    main.cpp
    ListGames.cpp
)
target_compile_definitions(gfxtune PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(gfxtune PRIVATE gfxtune_ui gfxtune_uiplugin Qt6::Widgets)

install(TARGETS gfxtune RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
