// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "settings/GameSettings.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// Frame generation through lsfg-vk (https://lsfg-vk.dev), a Vulkan layer
// that runs Lossless Scaling's frame generation. The user installs both;
// gfxtune never ships or changes either. It writes a configuration file of
// its own for each game and points lsfg-vk at it when the game starts,
// leaving the user's own lsfg-vk configuration alone.
namespace gfxtune::framegen
{

// Lossless Scaling on Steam, and the branch of it that lsfg-vk needs.
inline constexpr quint32 LosslessScalingAppId = 993090;
inline constexpr char LsfgVkBranch[] = "lsfg-vk";
// lsfg-vk's layer, as its manifest names it.
inline constexpr char LayerName[] = "VK_LAYER_LSFGVK_frame_generation";
// The profile in gfxtune's files, which LSFGVK_PROFILE selects.
inline constexpr char ProfileName[] = "gfxtune";

// The folders the Vulkan loader reads implicit layers from on this system.
QStringList implicitLayerDirs();
// Whether lsfg-vk's layer is installed in one of `dirs`.
bool layerInstalled(const QStringList &dirs);

// gfxtune's lsfg-vk configuration for a game, in a folder of its own:
// lsfg-vk watches the folder of its file for changes.
QString configPath(const QString &baseDir, quint32 appId);
// lsfg-vk's configuration format, version 2: Lossless Scaling's file when
// gfxtune knows it, and one profile with the game's settings.
QByteArray configText(const settings::GameSettings &s, const QString &dll);
// Writes the file unless it already says this, so lsfg-vk only reloads on
// a real change.
bool writeConfig(const QString &path, const QByteArray &text, QString *error = nullptr);

// Where the window keeps the path of Lossless Scaling's frame generation
// file for gfxtune-run, which does not read Steam's files itself.
QString dllNotePath(const QString &baseDir);
QString readDllNote(const QString &baseDir);
bool writeDllNote(const QString &baseDir, const QString &dll, QString *error = nullptr);

} // namespace gfxtune::framegen
