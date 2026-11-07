// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QLoggingCategory>

// What gfxtune reports to the system log (journalctl, or Steam's console
// for gfxtune-run). Warnings show by default; the rest with, for example,
// QT_LOGGING_RULES="gfxtune.*.debug=true".
Q_DECLARE_LOGGING_CATEGORY(lcSteam)    // gfxtune.steam: reading Steam's files
Q_DECLARE_LOGGING_CATEGORY(lcLaunch)   // gfxtune.launch: launch options, gfxtune-run
Q_DECLARE_LOGGING_CATEGORY(lcSettings) // gfxtune.settings: gfxtune's own files
