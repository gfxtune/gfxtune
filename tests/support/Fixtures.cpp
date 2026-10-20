// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "Fixtures.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

namespace gfxtune::test
{

namespace
{

template<typename T>
void put(QByteArray &out, T v)
{
    char bytes[sizeof(T)];
    qToLittleEndian(v, bytes);
    out.append(bytes, qsizetype(sizeof(T)));
}

template<typename T>
void putAt(QByteArray &out, qsizetype pos, T v)
{
    qToLittleEndian(v, out.data() + pos);
}

QByteArray escaped(const QString &text)
{
    QByteArray out;
    for (const char c : text.toUtf8()) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

void writeTextNode(const vdf::Node &node, int depth, QByteArray &out)
{
    const QByteArray indent(depth, '\t');
    out += indent + '"' + escaped(node.key) + '"';
    if (node.isSection) {
        out += '\n' + indent + "{\n";
        for (const vdf::Node &child : node.children) {
            writeTextNode(child, depth + 1, out);
        }
        out += indent + "}\n";
    } else {
        out += "\t\t\"" + escaped(node.value) + "\"\n";
    }
}

void writeKey(const QString &key, QStringList *keyTable, QByteArray &out)
{
    if (keyTable) {
        qsizetype index = keyTable->indexOf(key);
        if (index < 0) {
            keyTable->append(key);
            index = keyTable->size() - 1;
        }
        put<qint32>(out, qint32(index));
    } else {
        out += key.toUtf8();
        out += '\0';
    }
}

void writeBinaryNode(const vdf::Node &node, QStringList *keyTable, QByteArray &out)
{
    if (node.isSection) {
        out += char(0x00);
        writeKey(node.key, keyTable, out);
        for (const vdf::Node &child : node.children) {
            writeBinaryNode(child, keyTable, out);
        }
        out += char(0x08);
    } else {
        out += char(0x01);
        writeKey(node.key, keyTable, out);
        out += node.value.toUtf8();
        out += '\0';
    }
}

} // namespace

vdf::Node value(const QString &key, const QString &value)
{
    vdf::Node node;
    node.key = key;
    node.value = value;
    return node;
}

vdf::Node section(const QString &key, std::vector<vdf::Node> children)
{
    vdf::Node node;
    node.key = key;
    node.isSection = true;
    node.children = std::move(children);
    return node;
}

QByteArray writeText(const vdf::Node &root)
{
    QByteArray out;
    for (const vdf::Node &child : root.children) {
        writeTextNode(child, 0, out);
    }
    return out;
}

QByteArray writeBinary(const vdf::Node &root, QStringList *keyTable)
{
    QByteArray out;
    for (const vdf::Node &child : root.children) {
        writeBinaryNode(child, keyTable, out);
    }
    out += char(0x08);
    return out;
}

QByteArray writeAppInfo(int version, const QList<QPair<quint32, vdf::Node>> &apps)
{
    QByteArray out;
    put<quint32>(out, version == 29 ? 0x07564429u : 0x07564428u);
    put<quint32>(out, 1); // universe
    const qsizetype tableOffsetAt = out.size();
    if (version == 29) {
        put<qint64>(out, 0); // filled in below
    }

    QStringList keyTable;
    for (const auto &[appId, contents] : apps) {
        vdf::Node root;
        root.isSection = true;
        vdf::Node appinfo = contents;
        appinfo.key = QStringLiteral("appinfo");
        appinfo.isSection = true;
        root.children.push_back(appinfo);
        const QByteArray payload = writeBinary(root, version == 29 ? &keyTable : nullptr);

        put<quint32>(out, appId);
        put<quint32>(out, quint32(60 + payload.size()));
        put<quint32>(out, 2);          // info state
        put<quint32>(out, 1700000000); // last updated
        put<quint64>(out, 0);          // access token
        out += QByteArray(20, '\0');   // SHA-1
        put<quint32>(out, 1);          // change number
        out += QByteArray(20, '\0');   // SHA-1 of the binary data
        out += payload;
    }
    put<quint32>(out, 0); // end marker

    if (version == 29) {
        putAt<qint64>(out, tableOffsetAt, qint64(out.size()));
        put<qint32>(out, qint32(keyTable.size()));
        for (const QString &key : std::as_const(keyTable)) {
            out += key.toUtf8();
            out += '\0';
        }
    }
    return out;
}

QByteArray writePe(bool is64, const QStringList &imports, const QStringList &delayImports, bool oldStyleDelay)
{
    constexpr quint32 SectionRva = 0x1000;
    constexpr qsizetype SectionFileOffset = 0x400;
    const quint64 imageBase = is64 ? 0x140000000ull : 0x400000ull;

    // Section contents: import descriptors, delay descriptors, then names.
    const qsizetype importsSize = (imports.size() + 1) * 20;
    const qsizetype delaySize = delayImports.isEmpty() ? 0 : (delayImports.size() + 1) * 32;
    QByteArray names;
    QList<quint32> importNameRvas;
    QList<quint32> delayNameRvas;
    const quint32 namesRva = SectionRva + quint32(importsSize + delaySize);
    for (const QString &name : imports) {
        importNameRvas.append(namesRva + quint32(names.size()));
        names += name.toLatin1() + '\0';
    }
    for (const QString &name : delayImports) {
        delayNameRvas.append(namesRva + quint32(names.size()));
        names += name.toLatin1() + '\0';
    }

    QByteArray data;
    for (quint32 rva : std::as_const(importNameRvas)) {
        put<quint32>(data, 0);          // original first thunk
        put<quint32>(data, 0);          // time stamp
        put<quint32>(data, 0);          // forwarder chain
        put<quint32>(data, rva);        // name
        put<quint32>(data, SectionRva); // first thunk (any non-zero value)
    }
    data += QByteArray(20, '\0');
    if (!delayImports.isEmpty()) {
        for (quint32 rva : std::as_const(delayNameRvas)) {
            put<quint32>(data, oldStyleDelay ? 0u : 1u);
            put<quint32>(data, oldStyleDelay ? quint32(imageBase + rva) : rva);
            data += QByteArray(24, '\0');
        }
        data += QByteArray(32, '\0');
    }
    data += names;

    QByteArray out(SectionFileOffset, '\0');
    out[0] = 'M';
    out[1] = 'Z';
    putAt<quint32>(out, 0x3C, 0x80);
    putAt<quint32>(out, 0x80, 0x00004550);
    const qsizetype coff = 0x84;
    const quint16 optionalSize = is64 ? 240 : 224;
    putAt<quint16>(out, coff, is64 ? 0x8664 : 0x14C);
    putAt<quint16>(out, coff + 2, 1);
    putAt<quint16>(out, coff + 16, optionalSize);
    const qsizetype optional = coff + 20;
    putAt<quint16>(out, optional, is64 ? 0x20B : 0x10B);
    qsizetype directories = 0;
    if (is64) {
        putAt<quint64>(out, optional + 24, imageBase);
        putAt<quint32>(out, optional + 108, 16);
        directories = optional + 112;
    } else {
        putAt<quint32>(out, optional + 28, quint32(imageBase));
        putAt<quint32>(out, optional + 92, 16);
        directories = optional + 96;
    }
    putAt<quint32>(out, directories + 8, SectionRva);
    putAt<quint32>(out, directories + 12, quint32(importsSize));
    if (!delayImports.isEmpty()) {
        putAt<quint32>(out, directories + 13 * 8, SectionRva + quint32(importsSize));
        putAt<quint32>(out, directories + 13 * 8 + 4, quint32(delaySize));
    }
    const qsizetype sectionHeader = optional + optionalSize;
    out.replace(sectionHeader, 6, ".rdata");
    putAt<quint32>(out, sectionHeader + 8, quint32(data.size()));
    putAt<quint32>(out, sectionHeader + 12, SectionRva);
    putAt<quint32>(out, sectionHeader + 16, quint32(data.size()));
    putAt<quint32>(out, sectionHeader + 20, quint32(SectionFileOffset));
    out += data;
    return out;
}

QByteArray writeElf(bool is64, const QStringList &needed)
{
    constexpr quint64 Base = 0x400000;
    const qsizetype headerSize = is64 ? 64 : 52;
    const qsizetype phEntry = is64 ? 56 : 32;
    const qsizetype stringsAt = headerSize + 2 * phEntry;

    QByteArray strings(1, '\0');
    QList<quint64> offsets;
    for (const QString &name : needed) {
        offsets.append(quint64(strings.size()));
        strings += name.toLatin1() + '\0';
    }
    const qsizetype dynamicAt = stringsAt + strings.size();
    QByteArray dynamic;
    auto entry = [&](qint64 tag, quint64 v) {
        if (is64) {
            put<qint64>(dynamic, tag);
            put<quint64>(dynamic, v);
        } else {
            put<qint32>(dynamic, qint32(tag));
            put<quint32>(dynamic, quint32(v));
        }
    };
    for (quint64 offset : std::as_const(offsets)) {
        entry(1, offset); // DT_NEEDED
    }
    entry(5, Base + quint64(stringsAt)); // DT_STRTAB
    entry(10, quint64(strings.size()));   // DT_STRSZ
    entry(0, 0);                          // DT_NULL
    const qsizetype total = dynamicAt + dynamic.size();

    QByteArray out(stringsAt, '\0');
    out[0] = '\x7F';
    out[1] = 'E';
    out[2] = 'L';
    out[3] = 'F';
    out[4] = is64 ? 2 : 1;
    out[5] = 1; // little-endian
    out[6] = 1;
    putAt<quint16>(out, 16, 3); // shared object / position-independent program
    putAt<quint16>(out, 18, is64 ? 0x3E : 0x03);
    putAt<quint32>(out, 20, 1);
    if (is64) {
        putAt<quint64>(out, 0x20, quint64(headerSize));
        putAt<quint16>(out, 0x34, quint16(headerSize));
        putAt<quint16>(out, 0x36, quint16(phEntry));
        putAt<quint16>(out, 0x38, 2);
    } else {
        putAt<quint32>(out, 0x1C, quint32(headerSize));
        putAt<quint16>(out, 0x28, quint16(headerSize));
        putAt<quint16>(out, 0x2A, quint16(phEntry));
        putAt<quint16>(out, 0x2C, 2);
    }
    auto programHeader = [&](qsizetype at, quint32 type, quint64 offset, quint64 size) {
        if (is64) {
            putAt<quint32>(out, at, type);
            putAt<quint64>(out, at + 8, offset);
            putAt<quint64>(out, at + 16, Base + offset);
            putAt<quint64>(out, at + 32, size);
            putAt<quint64>(out, at + 40, size);
        } else {
            putAt<quint32>(out, at, type);
            putAt<quint32>(out, at + 4, quint32(offset));
            putAt<quint32>(out, at + 8, quint32(Base + offset));
            putAt<quint32>(out, at + 16, quint32(size));
            putAt<quint32>(out, at + 20, quint32(size));
        }
    };
    programHeader(headerSize, 1, 0, quint64(total));                                      // PT_LOAD
    programHeader(headerSize + phEntry, 2, quint64(dynamicAt), quint64(dynamic.size())); // PT_DYNAMIC
    out += strings;
    out += dynamic;
    return out;
}

bool writeFile(const QString &path, const QByteArray &data, bool executable)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) {
        return false;
    }
    file.close();
    if (executable) {
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    }
    return true;
}

QByteArray mutate(const QByteArray &data, std::mt19937 &random)
{
    QByteArray out = data;
    std::uniform_int_distribution<int> kind(0, 3);
    std::uniform_int_distribution<int> byteValue(0, 255);
    const int edits = 1 + int(random() % 8);
    for (int i = 0; i < edits && !out.isEmpty(); ++i) {
        const qsizetype at = qsizetype(random() % quint32(out.size()));
        switch (kind(random)) {
        case 0:
            out[at] = char(byteValue(random));
            break;
        case 1:
            out.truncate(at);
            break;
        case 2:
            out.insert(at, QByteArray(1 + int(random() % 16), char(byteValue(random))));
            break;
        default:
            out[at] = char(0xFF);
            break;
        }
    }
    return out;
}

} // namespace gfxtune::test
