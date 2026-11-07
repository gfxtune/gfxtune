// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include <cstdint>

namespace gfxtune
{

// Byte counts for the limits on what is read from files. 64 bits wide, so
// a product such as 8 * MiB is computed without overflow.
inline constexpr std::int64_t KiB = 1024;
inline constexpr std::int64_t MiB = 1024 * KiB;

} // namespace gfxtune
