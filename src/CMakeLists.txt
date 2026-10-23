# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

# Everything that needs neither a window nor QML, so tests use it directly.
add_library(gfxtune_core STATIC
    core/vdf/KeyValues.cpp
    core/vdf/TextVdf.cpp
    core/vdf/BinaryVdf.cpp
    core/vdf/TextVdfEdit.cpp
    core/launch/LastLaunch.cpp
    core/launch/LaunchLine.cpp
    core/launch/LaunchLines.cpp
    core/launch/LaunchPlan.cpp
    core/binaries/ExecutableImports.cpp
    core/gpu/AmdGpu.cpp
    core/settings/Availability.cpp
    core/settings/GameSettings.cpp
    core/settings/SettingsStore.cpp
    core/steam/AppInfo.cpp
    core/steam/Covers.cpp
    core/steam/Game.cpp
    core/steam/GameFiles.cpp
    core/steam/RunningGames.cpp
    core/steam/SteamLibrary.cpp
    core/steam/SteamProcess.cpp
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
        app/GpuMonitor.h app/GpuMonitor.cpp
)
target_include_directories(gfxtune_app PUBLIC app)
target_link_libraries(gfxtune_app PUBLIC gfxtune_core Qt6::Gui Qt6::Qml Qt6::Concurrent)

# The window, as the QML module Gfxtune.
qt_add_library(gfxtune_ui STATIC)
qt_add_qml_module(gfxtune_ui
    URI Gfxtune
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/GameListPane.qml
        qml/GameSettingsPane.qml
        qml/GpuPanel.qml
        qml/Cover.qml
        qml/Note.qml
        qml/SettingSwitch.qml
        qml/ValueComboBox.qml
)
target_link_libraries(gfxtune_ui PUBLIC gfxtune_app gfxtune_appplugin Qt6::Quick Qt6::QuickControls2)

qt_add_executable(gfxtune
    main.cpp
    Commands.cpp
)
target_compile_definitions(gfxtune PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(gfxtune PRIVATE gfxtune_ui gfxtune_uiplugin Qt6::Widgets)

# The launch helper. Kept small: it starts with every game it is added to.
qt_add_executable(gfxtune-run run/main.cpp)
target_link_libraries(gfxtune-run PRIVATE gfxtune_core Qt6::Gui)

install(TARGETS gfxtune gfxtune-run RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/gfxtune.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
