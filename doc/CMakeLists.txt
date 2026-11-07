# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 gfxtune contributors

foreach(page gfxtune.1 gfxtune-run.1)
    configure_file(${page} ${CMAKE_CURRENT_BINARY_DIR}/${page} @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${page} DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endforeach()
