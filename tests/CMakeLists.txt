# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

find_package(Qt6 REQUIRED COMPONENTS Test)

add_library(gfxtune_test_support STATIC
    support/Fixtures.cpp
    support/FakeSteam.cpp
    support/FakeSysfs.cpp
)
target_include_directories(gfxtune_test_support PUBLIC .)
target_link_libraries(gfxtune_test_support PUBLIC gfxtune_core Qt6::Test)

# Tests of the parts without a window.
foreach(name textvdf binaryvdf appinfo imports gamefiles steamscan running settings launch vdfedit)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE gfxtune_test_support)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()

# Tests of the objects the window uses.
foreach(name gamelist library gpu)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE gfxtune_test_support gfxtune_app)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()

# The launch helper, run as Steam would run it.
qt_add_executable(tst_run tst_run.cpp)
target_link_libraries(tst_run PRIVATE gfxtune_test_support)
target_compile_definitions(tst_run PRIVATE GFXTUNE_RUN_PATH="$<TARGET_FILE:gfxtune-run>")
add_dependencies(tst_run gfxtune-run)
add_test(NAME run COMMAND tst_run)

# The gfxtune program's text-only options.
qt_add_executable(tst_commands tst_commands.cpp)
target_link_libraries(tst_commands PRIVATE gfxtune_test_support)
target_compile_definitions(tst_commands PRIVATE GFXTUNE_PATH="$<TARGET_FILE:gfxtune>")
add_dependencies(tst_commands gfxtune)
add_test(NAME commands COMMAND tst_commands)

# The in-game filters on a real Vulkan device, and the layer through the loader.
find_package(Vulkan REQUIRED)
qt_add_executable(tst_layer tst_layer.cpp)
target_link_libraries(tst_layer PRIVATE gfxtune_filters_vk Vulkan::Vulkan Qt6::Test)
target_compile_definitions(tst_layer PRIVATE GFXTUNE_LAYER_MANIFEST_DIR="${CMAKE_BINARY_DIR}/layer")
add_dependencies(tst_layer VkLayer_gfxtune)
add_test(NAME layer COMMAND tst_layer)

# The window itself, drawn off screen.
qt_add_executable(tst_window tst_window.cpp)
target_link_libraries(tst_window PRIVATE gfxtune_test_support gfxtune_ui gfxtune_uiplugin Qt6::Widgets)
add_test(NAME window COMMAND tst_window)
