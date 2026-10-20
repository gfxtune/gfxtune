# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

# Everything that does not need a window, so tests can use it directly.
add_library(gfxtune_core STATIC
    core/vdf/KeyValues.cpp
    core/vdf/TextVdf.cpp
    core/vdf/BinaryVdf.cpp
    core/binaries/ExecutableImports.cpp
    core/steam/AppInfo.cpp
    core/steam/Game.cpp
    core/steam/GameFiles.cpp
    core/steam/RunningGames.cpp
    core/steam/SteamLibrary.cpp
    core/steam/SteamWatcher.cpp
)
target_include_directories(gfxtune_core PUBLIC core)
target_link_libraries(gfxtune_core PUBLIC Qt6::Core)

qt_add_executable(gfxtune
    main.cpp
    ListGames.cpp
)

qt_add_qml_module(gfxtune
    URI Gfxtune
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_compile_definitions(gfxtune PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(gfxtune PRIVATE gfxtune_core Qt6::Quick Qt6::QuickControls2 Qt6::Widgets)

install(TARGETS gfxtune RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
