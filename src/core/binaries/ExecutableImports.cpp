// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "ExecutableImports.h"

#include <QFile>
#include <QtEndian>

#include <optional>

namespace gfxtune::binaries
{

namespace
{

// Limits that no real program reaches; they stop damaged files from making
// the scan loop for long.
constexpr int MaxDescriptors = 4096;
constexpr int MaxSections = 96;
constexpr qsizetype MaxNameLength = 512;

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

class Bytes
{
public:
    explicit Bytes(QByteArrayView data)
        : m_data(data)
    {
    }

    qsizetype size() const { return m_data.size(); }

    bool has(qint64 pos, qint64 length) const { return pos >= 0 && length >= 0 && pos <= m_data.size() - length; }

    template<typename T>
    std::optional<T> get(qint64 pos) const
    {
        if (!has(pos, qint64(sizeof(T)))) {
            return std::nullopt;
        }
        return qFromLittleEndian<T>(m_data.data() + pos);
    }

    // A zero-terminated name made of printable ASCII.
    std::optional<QString> name(qint64 pos) const
    {
        if (!has(pos, 1)) {
            return std::nullopt;
        }
        for (qint64 i = pos; i < m_data.size() && i - pos <= MaxNameLength; ++i) {
            const char c = m_data[qsizetype(i)];
            if (c == '\0') {
                if (i == pos) {
                    return std::nullopt;
                }
                return QString::fromLatin1(m_data.sliced(qsizetype(pos), qsizetype(i - pos)));
            }
            if (c < 0x20 || c > 0x7E) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

private:
    QByteArrayView m_data;
};

// ---- Windows PE ----------------------------------------------------------

struct Section {
    quint32 virtualAddress = 0;
    quint32 virtualSize = 0;
    quint32 rawSize = 0;
    quint32 rawOffset = 0;
};

std::optional<qint64> rvaToOffset(const std::vector<Section> &sections, quint64 rva)
{
    for (const Section &s : sections) {
        const quint64 extent = qMax(s.virtualSize, s.rawSize);
        if (rva >= s.virtualAddress && rva < quint64(s.virtualAddress) + extent) {
            const quint64 delta = rva - s.virtualAddress;
            if (delta >= s.rawSize) {
                return std::nullopt; // in memory only, not stored in the file
            }
            return qint64(s.rawOffset) + qint64(delta);
        }
    }
    return std::nullopt;
}

bool parsePe(const Bytes &in, Imports &out, QString *error)
{
    const auto peOffset = in.get<quint32>(0x3C);
    if (!peOffset || in.get<quint32>(*peOffset) != quint32(0x00004550)) { // "PE\0\0"
        return fail(error, QStringLiteral("not a Windows program"));
    }
    const qint64 coff = qint64(*peOffset) + 4;
    const auto sectionCount = in.get<quint16>(coff + 2);
    const auto optionalSize = in.get<quint16>(coff + 16);
    const qint64 optional = coff + 20;
    const auto magic = in.get<quint16>(optional);
    if (!sectionCount || !optionalSize || !magic) {
        return fail(error, QStringLiteral("Windows program header is cut off"));
    }

    quint64 imageBase = 0;
    qint64 directories = 0;
    std::optional<quint32> directoryCount;
    if (*magic == 0x10B) { // 32-bit
        imageBase = in.get<quint32>(optional + 28).value_or(0);
        directoryCount = in.get<quint32>(optional + 92);
        directories = optional + 96;
    } else if (*magic == 0x20B) { // 64-bit
        imageBase = in.get<quint64>(optional + 24).value_or(0);
        directoryCount = in.get<quint32>(optional + 108);
        directories = optional + 112;
    } else {
        return fail(error, QStringLiteral("unknown Windows program type"));
    }
    if (!directoryCount) {
        return fail(error, QStringLiteral("Windows program header is cut off"));
    }

    std::vector<Section> sections;
    const qint64 table = optional + *optionalSize;
    for (int i = 0; i < qMin(int(*sectionCount), MaxSections); ++i) {
        const qint64 at = table + qint64(i) * 40;
        const auto vsize = in.get<quint32>(at + 8);
        const auto vaddr = in.get<quint32>(at + 12);
        const auto rsize = in.get<quint32>(at + 16);
        const auto roff = in.get<quint32>(at + 20);
        if (!vsize || !vaddr || !rsize || !roff) {
            return fail(error, QStringLiteral("section table is cut off"));
        }
        sections.push_back({*vaddr, *vsize, *rsize, *roff});
    }

    out.format = ExecutableFormat::Windows;
    auto addName = [&](quint64 rva) {
        if (const auto offset = rvaToOffset(sections, rva)) {
            if (const auto name = in.name(*offset)) {
                const QString lower = name->toLower();
                if (!out.libraries.contains(lower)) {
                    out.libraries.append(lower);
                }
            }
        }
    };

    // Regular imports: data directory 1, 20-byte descriptors.
    if (*directoryCount > 1) {
        const auto rva = in.get<quint32>(directories + 8);
        if (rva && *rva) {
            if (const auto start = rvaToOffset(sections, *rva)) {
                for (int i = 0; i < MaxDescriptors; ++i) {
                    const qint64 at = *start + qint64(i) * 20;
                    const auto nameRva = in.get<quint32>(at + 12);
                    const auto firstThunk = in.get<quint32>(at + 16);
                    if (!nameRva || !firstThunk || (*nameRva == 0 && *firstThunk == 0)) {
                        break;
                    }
                    addName(*nameRva);
                }
            }
        }
    }

    // Delay-loaded imports: data directory 13, 32-byte descriptors. Many
    // engines delay-load their Direct3D libraries.
    if (*directoryCount > 13) {
        const auto rva = in.get<quint32>(directories + 13 * 8);
        if (rva && *rva) {
            if (const auto start = rvaToOffset(sections, *rva)) {
                for (int i = 0; i < MaxDescriptors; ++i) {
                    const qint64 at = *start + qint64(i) * 32;
                    const auto attributes = in.get<quint32>(at);
                    const auto name = in.get<quint32>(at + 4);
                    if (!attributes || !name || *name == 0) {
                        break;
                    }
                    // Old-style descriptors (attribute bit 0 clear) hold
                    // addresses instead of offsets from the image base.
                    const quint64 nameRva = (*attributes & 1) ? quint64(*name) : quint64(*name) - imageBase;
                    addName(nameRva);
                }
            }
        }
    }
    return true;
}

// ---- Linux ELF -----------------------------------------------------------

struct Segment {
    quint64 offset = 0;
    quint64 vaddr = 0;
    quint64 filesz = 0;
};

bool parseElf(const Bytes &in, Imports &out, QString *error)
{
    const auto elfClass = in.get<quint8>(4);
    const auto encoding = in.get<quint8>(5);
    if (!elfClass || !encoding) {
        return fail(error, QStringLiteral("Linux program header is cut off"));
    }
    if (*encoding != 1) {
        return fail(error, QStringLiteral("big-endian programs are not supported"));
    }
    const bool is64 = *elfClass == 2;
    if (!is64 && *elfClass != 1) {
        return fail(error, QStringLiteral("unknown Linux program type"));
    }

    std::optional<quint64> phoff;
    std::optional<quint16> phentsize, phnum;
    if (is64) {
        phoff = in.get<quint64>(0x20);
        phentsize = in.get<quint16>(0x36);
        phnum = in.get<quint16>(0x38);
    } else {
        if (const auto v = in.get<quint32>(0x1C)) {
            phoff = *v;
        }
        phentsize = in.get<quint16>(0x2A);
        phnum = in.get<quint16>(0x2C);
    }
    if (!phoff || !phentsize || !phnum) {
        return fail(error, QStringLiteral("Linux program header is cut off"));
    }
    const quint16 minimumEntry = is64 ? 56 : 32;
    if (*phnum > 0 && *phentsize < minimumEntry) {
        return fail(error, QStringLiteral("Linux program header table is damaged"));
    }

    std::vector<Segment> loads;
    std::optional<Segment> dynamic;
    for (int i = 0; i < qMin(int(*phnum), MaxSections); ++i) {
        const qint64 at = qint64(*phoff) + qint64(i) * *phentsize;
        const auto type = in.get<quint32>(at);
        Segment s;
        if (is64) {
            const auto off = in.get<quint64>(at + 8);
            const auto vaddr = in.get<quint64>(at + 16);
            const auto filesz = in.get<quint64>(at + 32);
            if (!type || !off || !vaddr || !filesz) {
                return fail(error, QStringLiteral("Linux program header table is cut off"));
            }
            s = {*off, *vaddr, *filesz};
        } else {
            const auto off = in.get<quint32>(at + 4);
            const auto vaddr = in.get<quint32>(at + 8);
            const auto filesz = in.get<quint32>(at + 16);
            if (!type || !off || !vaddr || !filesz) {
                return fail(error, QStringLiteral("Linux program header table is cut off"));
            }
            s = {*off, *vaddr, *filesz};
        }
        if (*type == 1) {
            loads.push_back(s);
        } else if (*type == 2) {
            dynamic = s;
        }
    }

    out.format = ExecutableFormat::Linux;
    if (!dynamic) {
        return true; // statically linked: needs nothing
    }

    const qint64 entrySize = is64 ? 16 : 8;
    std::vector<quint64> needed;
    std::optional<quint64> strtab;
    std::optional<quint64> strsz;
    const qint64 entries = qint64(qMin<quint64>(dynamic->filesz / quint64(entrySize), MaxDescriptors));
    for (qint64 i = 0; i < entries; ++i) {
        const qint64 at = qint64(dynamic->offset) + i * entrySize;
        std::optional<qint64> tag;
        std::optional<quint64> value;
        if (is64) {
            tag = in.get<qint64>(at);
            value = in.get<quint64>(at + 8);
        } else {
            if (const auto t = in.get<qint32>(at)) {
                tag = *t;
            }
            if (const auto v = in.get<quint32>(at + 4)) {
                value = *v;
            }
        }
        if (!tag || !value || *tag == 0) {
            break;
        }
        if (*tag == 1) {
            needed.push_back(*value);
        } else if (*tag == 5) {
            strtab = *value;
        } else if (*tag == 10) {
            strsz = *value;
        }
    }
    if (!strtab) {
        return true;
    }

    // The string table is given as a memory address; find where it lives in
    // the file through the loaded segments.
    std::optional<quint64> tableOffset;
    for (const Segment &s : loads) {
        if (*strtab >= s.vaddr && *strtab < s.vaddr + s.filesz) {
            tableOffset = s.offset + (*strtab - s.vaddr);
            break;
        }
    }
    if (!tableOffset) {
        return true;
    }
    for (quint64 index : needed) {
        if (strsz && index >= *strsz) {
            continue;
        }
        if (const auto name = in.name(qint64(*tableOffset + index))) {
            if (!out.libraries.contains(*name)) {
                out.libraries.append(*name);
            }
        }
    }
    return true;
}

} // namespace

bool parseImports(QByteArrayView data, Imports &out, QString *error)
{
    out = Imports{};
    const Bytes in(data);
    if (data.startsWith("MZ")) {
        return parsePe(in, out, error);
    }
    if (data.startsWith("\x7F"
                        "ELF")) {
        return parseElf(in, out, error);
    }
    return fail(error, QStringLiteral("not a program"));
}

bool readImports(const QString &path, Imports &out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, file.errorString());
    }
    const qint64 size = file.size();
    if (size <= 0) {
        return fail(error, QStringLiteral("empty file"));
    }
    // Map instead of reading: programs can be very large and only a few
    // small tables are looked at.
    if (const uchar *mapped = file.map(0, size)) {
        return parseImports(QByteArrayView(reinterpret_cast<const char *>(mapped), qsizetype(size)), out, error);
    }
    const QByteArray data = file.readAll();
    return parseImports(data, out, error);
}

} // namespace gfxtune::binaries
