// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Game.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace gfxtune::steam
{

QString runtimeName(Runtime runtime)
{
    switch (runtime) {
    case Runtime::Native:
        return QStringLiteral("Native");
    case Runtime::Proton:
        return QStringLiteral("Proton");
    case Runtime::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

Fsr4Support readFsr4Support(const QString &toolPath)
{
    if (toolPath.isEmpty()) {
        return Fsr4Support::Unknown;
    }
    // Valve's Proton copies this into every prefix it sets up.
    if (QFileInfo(toolPath + QStringLiteral("/contrib/amdxcffx64.dll")).isFile()) {
        return Fsr4Support::BuiltIn;
    }

    // The launch script is a Python file of a few hundred kilobytes.
    constexpr qint64 MaxScriptSize = 8 * 1024 * 1024;
    QFile script(toolPath + QStringLiteral("/proton"));
    if (script.size() > MaxScriptSize || !script.open(QIODevice::ReadOnly)) {
        return Fsr4Support::Unknown;
    }
    // proton-cachyos switches its FSR 4 option on for every game, then lets
    // PROTON_FSR4_UPGRADE change it; GE-Proton only reads the variable.
    static const QRegularExpression alwaysOn(QStringLiteral("compat_config\\.add\\(\\s*[\"']fsr4[\"']\\s*\\)"));
    bool readsVariable = false;
    while (!script.atEnd()) {
        const QString line = QString::fromUtf8(script.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (alwaysOn.match(line).hasMatch()) {
            return Fsr4Support::BuiltIn;
        }
        readsVariable = readsVariable || line.contains(QLatin1String("PROTON_FSR4_UPGRADE"));
    }
    return readsVariable ? Fsr4Support::OnRequest : Fsr4Support::None;
}

QString apiNames(GraphicsApis apis)
{
    QStringList directX;
    if (apis & GraphicsApi::DirectX8) {
        directX << QStringLiteral("8");
    }
    if (apis & GraphicsApi::DirectX9) {
        directX << QStringLiteral("9");
    }
    if (apis & GraphicsApi::DirectX10) {
        directX << QStringLiteral("10");
    }
    if (apis & GraphicsApi::DirectX11) {
        directX << QStringLiteral("11");
    }
    if (apis & GraphicsApi::DirectX12) {
        directX << QStringLiteral("12");
    }

    QStringList names;
    if (!directX.isEmpty()) {
        names << QStringLiteral("DirectX ") + directX.join(QLatin1Char('/'));
    }
    if (apis & GraphicsApi::Vulkan) {
        names << QStringLiteral("Vulkan");
    }
    if (apis & GraphicsApi::OpenGL) {
        names << QStringLiteral("OpenGL");
    }
    return names.join(QStringLiteral(", "));
}

} // namespace gfxtune::steam
