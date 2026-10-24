# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

cmake_minimum_required(VERSION 3.24)
project(gfxtune VERSION 0.1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

option(GFXTUNE_WERROR "Treat compiler warnings as errors" OFF)
option(GFXTUNE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(BUILD_TESTING "Build the tests" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Gui Qml Quick QuickControls2 Widgets)
qt_standard_project_setup(REQUIRES 6.8)
# All QML modules in one folder, so build tools find each other's types.
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
if(GFXTUNE_WERROR)
    add_compile_options(-Werror)
endif()
if(GFXTUNE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(src)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
