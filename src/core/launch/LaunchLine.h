// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <QSet>
#include <QString>

namespace gfxtune::launch
{

inline constexpr QLatin1StringView HelperName("gfxtune-run");

// Steam launch options with gfxtune-run placed right before %command%.
// Everything the user wrote stays where it was; options written without
// %command% are game arguments, so they keep that meaning after it.
QString withHelper(const QString &launchOptions);

// The launch options without gfxtune-run. A line that held nothing but
// gfxtune-run becomes empty again.
QString withoutHelper(const QString &launchOptions);

bool hasHelper(const QString &launchOptions);

// What the user's own launch options already set, so gfxtune leaves it alone.
struct OwnSettings {
    QSet<QString> environment; // names of variables assigned before %command%
    bool gamescope = false;    // gamescope already wraps the game
};
OwnSettings ownSettings(const QString &launchOptions);

} // namespace gfxtune::launch
