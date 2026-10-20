# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

find_package(Qt6 REQUIRED COMPONENTS Test)

add_library(gfxtune_test_support STATIC support/Fixtures.cpp)
target_include_directories(gfxtune_test_support PUBLIC .)
target_link_libraries(gfxtune_test_support PUBLIC gfxtune_core Qt6::Test)

foreach(name textvdf binaryvdf appinfo imports gamefiles steamscan running)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE gfxtune_test_support)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()
