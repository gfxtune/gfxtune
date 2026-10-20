// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Game.h"

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
