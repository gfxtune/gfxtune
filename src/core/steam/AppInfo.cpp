// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "AppInfo.h"

#include "Errors.h"
#include "vdf/BinaryVdf.h"

#include <QFile>
#include <QtEndian>

namespace gfxtune::steam
{

namespace
{

constexpr quint32 MagicV28 = 0x07564428;
constexpr quint32 MagicV29 = 0x07564429;

// appid, size, info state, last updated, access token, SHA-1, change
// number, SHA-1 of the binary data: 4+4+4+4+8+20+4+20 bytes. The size field
// counts everything after itself.
constexpr qsizetype EntryHeaderSize = 68;
constexpr qsizetype BytesBeforeSizeEnd = 8;

constexpr qint32 MaxKeyTableEntries = 10'000'000;

template<typename T>
T readLE(QByteArrayView data, qsizetype pos)
{
    return qFromLittleEndian<T>(data.data() + pos);
}

bool readKeyTable(QByteArrayView data, qint64 offset, QStringList &keys, QString *error)
{
    if (offset < 16 || offset > data.size() - 4) {
        return fail(error, QStringLiteral("key table position is outside the file"));
    }
    const auto count = readLE<qint32>(data, qsizetype(offset));
    if (count < 0 || count > MaxKeyTableEntries) {
        return fail(error, QStringLiteral("key table has an impossible size (%1)").arg(count));
    }
    keys.reserve(count);
    qsizetype pos = qsizetype(offset) + 4;
    for (qint32 i = 0; i < count; ++i) {
        const qsizetype end = data.indexOf('\0', pos);
        if (end < 0) {
            return fail(error, QStringLiteral("key table is cut off"));
        }
        keys.append(QString::fromUtf8(data.sliced(pos, end - pos)));
        pos = end + 1;
    }
    return true;
}

} // namespace

bool readAppInfo(const QString &path, const QSet<quint32> &wanted, AppInfoFile &out, QString *error)
{
    out = AppInfoFile{};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, file.errorString());
    }
    const qint64 size = file.size();
    if (size < 8) {
        return fail(error, QStringLiteral("file is too short"));
    }

    // Map rather than read: the cache can be hundreds of megabytes and only
    // a few entries are needed.
    QByteArray buffer;
    QByteArrayView data;
    if (const uchar *mapped = file.map(0, size)) {
        data = QByteArrayView(reinterpret_cast<const char *>(mapped), qsizetype(size));
    } else {
        buffer = file.readAll();
        data = buffer;
    }

    const auto magic = readLE<quint32>(data, 0);
    qsizetype pos = 8; // magic and universe
    qsizetype limit = data.size();
    QStringList keyTable;

    if (magic == MagicV28) {
        out.version = 28;
    } else if (magic == MagicV29) {
        out.version = 29;
        if (data.size() < 16) {
            return fail(error, QStringLiteral("file is too short"));
        }
        const auto tableOffset = readLE<qint64>(data, pos);
        if (!readKeyTable(data, tableOffset, keyTable, error)) {
            return false;
        }
        pos += 8;
        limit = qsizetype(tableOffset);
    } else {
        return fail(error,
                    QStringLiteral("unsupported format (marker 0x%1); Steam may have changed it").arg(magic, 8, 16, QLatin1Char('0')));
    }

    qsizetype remaining = wanted.size();
    while (remaining > 0 && limit - pos >= 4) {
        const auto appId = readLE<quint32>(data, pos);
        if (appId == 0) {
            break; // end marker
        }
        if (limit - pos < EntryHeaderSize) {
            return fail(error, QStringLiteral("entry for app %1 is cut off").arg(appId));
        }
        const auto entrySize = readLE<quint32>(data, pos + 4);
        const qint64 entryEnd = qint64(pos) + BytesBeforeSizeEnd + entrySize;
        if (entrySize < EntryHeaderSize - BytesBeforeSizeEnd || entryEnd > limit) {
            return fail(error, QStringLiteral("entry for app %1 has an impossible size").arg(appId));
        }

        if (wanted.contains(appId)) {
            const qsizetype payloadStart = pos + EntryHeaderSize;
            const QByteArrayView payload = data.sliced(payloadStart, qsizetype(entryEnd) - payloadStart);
            vdf::Node root;
            QString problem;
            if (vdf::parseBinary(payload, root, &problem, out.version >= 29 ? &keyTable : nullptr)) {
                const vdf::Node *section = root.child(u"appinfo");
                out.apps.insert(appId, section ? *section : root);
                --remaining;
            } else {
                out.warnings.append(QStringLiteral("app %1: %2").arg(appId).arg(problem));
            }
        }
        pos = qsizetype(entryEnd);
    }
    return true;
}

} // namespace gfxtune::steam
