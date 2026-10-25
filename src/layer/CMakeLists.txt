# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

# The Vulkan layer that draws the in-game filters. It loads into games, so it
# uses neither Qt nor anything else beyond the C and C++ libraries, and it
# never links the Vulkan loader: the loader hands it every function.

find_package(Vulkan 1.3 REQUIRED COMPONENTS glslangValidator)

# Shaders become SPIR-V arrays in headers, compiled in at build time.
set(shader_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(shader_headers)
foreach(shader fullscreen.vert filters.frag)
    string(REPLACE "." "_" variable ${shader})
    set(header ${shader_dir}/${variable}.h)
    add_custom_command(
        OUTPUT ${header}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_dir}
        COMMAND Vulkan::glslangValidator -V --target-env vulkan1.0 --quiet --vn ${variable}
                -o ${header} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}
        COMMENT "Compiling shader ${shader}"
        VERBATIM)
    list(APPEND shader_headers ${header})
endforeach()

# Everything but the hooks, so tests can drive the filter pass directly.
add_library(gfxtune_filters_vk STATIC
    FilterPass.cpp
    LayerSettings.cpp
    ${shader_headers}
)
target_include_directories(gfxtune_filters_vk
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src
    PRIVATE ${shader_dir})
target_link_libraries(gfxtune_filters_vk PUBLIC Vulkan::Headers)
set_target_properties(gfxtune_filters_vk PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

add_library(VkLayer_gfxtune MODULE Layer.cpp)
target_link_libraries(VkLayer_gfxtune PRIVATE gfxtune_filters_vk)
set_target_properties(VkLayer_gfxtune PROPERTIES
    PREFIX "lib"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(NOT GFXTUNE_SANITIZE)
    # Inside Steam's runtime container the game's C++ library may be older
    # than ours: carry our own, hidden, and refuse to build with anything
    # left unresolved.
    target_link_options(VkLayer_gfxtune PRIVATE
        -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,-z,defs)
endif()
target_link_options(VkLayer_gfxtune PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/layer.map)
set_target_properties(VkLayer_gfxtune PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/layer.map)

# Two manifests from one template: the installed one points at the installed
# library (an absolute path, which Steam's runtime container needs), the
# build one at the build tree, for tests.
set(GFXTUNE_LAYER_LIBRARY ${CMAKE_INSTALL_FULL_LIBDIR}/gfxtune/libVkLayer_gfxtune.so)
configure_file(gfxtune_filters.json.in ${CMAKE_CURRENT_BINARY_DIR}/install/gfxtune_filters.json @ONLY)
set(GFXTUNE_LAYER_LIBRARY "$<TARGET_FILE:VkLayer_gfxtune>")
configure_file(gfxtune_filters.json.in ${CMAKE_CURRENT_BINARY_DIR}/build-manifest.json.in @ONLY)
file(GENERATE
    OUTPUT ${CMAKE_BINARY_DIR}/layer/gfxtune_filters.json
    INPUT ${CMAKE_CURRENT_BINARY_DIR}/build-manifest.json.in)

install(TARGETS VkLayer_gfxtune LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/gfxtune)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/install/gfxtune_filters.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/vulkan/implicit_layer.d)
