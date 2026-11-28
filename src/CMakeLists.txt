# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

# Everything that needs neither a window nor QML, so tests use it directly.
add_library(gfxtune_core STATIC
    core/Logging.cpp
    core/vdf/KeyValues.cpp
    core/vdf/TextVdf.cpp
    core/vdf/BinaryVdf.cpp
    core/vdf/TextVdfEdit.cpp
    core/launch/LastLaunch.cpp
    core/launch/LaunchLine.cpp
    core/launch/LaunchLines.cpp
    core/launch/LaunchPlan.cpp
    core/binaries/ExecutableImports.cpp
    core/cpu/Cpu.cpp
    core/framegen/LsfgVk.cpp
    core/gpu/AmdGpu.cpp
    core/performance/Performance.cpp
    core/settings/Availability.cpp
    core/settings/GameSettings.cpp
    core/settings/Profile.cpp
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
# core/ for the core's own headers, src/ for filters/, shared with the layer.
target_include_directories(gfxtune_core PUBLIC core ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gfxtune_core PUBLIC Qt6::Core KF6::I18n)

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
        qml/BackToBase.qml
        qml/ChoiceSetting.qml
        qml/Cover.qml
        qml/FilterSlider.qml
        qml/LastLaunch.qml
        qml/LaunchStatus.qml
        qml/Note.qml
        qml/PageHeader.qml
        qml/PerformanceHistory.qml
        qml/SettingRow.qml
        qml/SettingSwitch.qml
        qml/SliderSetting.qml
        qml/SwitchSetting.qml
        qml/ValueComboBox.qml
)
target_link_libraries(gfxtune_ui PUBLIC gfxtune_app gfxtune_appplugin Qt6::Quick Qt6::QuickControls2)

qt_add_executable(gfxtune
    main.cpp
    Commands.cpp
)
target_compile_definitions(gfxtune PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
# The icon inside the program too, for running it before it is installed.
qt_add_resources(gfxtune icon PREFIX / BASE ${PROJECT_SOURCE_DIR}/data/icons FILES ${PROJECT_SOURCE_DIR}/data/icons/gfxtune.svg)
target_link_libraries(gfxtune PRIVATE gfxtune_ui gfxtune_uiplugin Qt6::Widgets KF6::I18nQml)
# Our own QML modules are linked in above; Kirigami's and Qt's are loaded
# from the system at run time. The scan for plugins to link in statically
# only applies to a static Qt.
set_target_properties(gfxtune PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)

# The launch helper. Kept small: it starts with every game it is added to.
qt_add_executable(gfxtune-run run/main.cpp)
target_link_libraries(gfxtune-run PRIVATE gfxtune_core Qt6::Gui)

install(TARGETS gfxtune gfxtune-run RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_subdirectory(layer)
install(FILES ${PROJECT_SOURCE_DIR}/data/gfxtune.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# What software centres such as Discover show about gfxtune.
install(FILES ${PROJECT_SOURCE_DIR}/data/io.github.gfxtune.gfxtune.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
# The icon, drawn on KDE's 48 px grid, and redrawn for 16 px where the gauge's
# segments would blur together.
install(FILES ${PROJECT_SOURCE_DIR}/data/icons/gfxtune.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${PROJECT_SOURCE_DIR}/data/icons/16/gfxtune.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/16x16/apps)
