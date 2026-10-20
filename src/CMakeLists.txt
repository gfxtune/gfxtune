# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

qt_add_executable(gfxtune main.cpp)

qt_add_qml_module(gfxtune
    URI Gfxtune
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_compile_definitions(gfxtune PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(gfxtune PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Widgets)

install(TARGETS gfxtune RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
