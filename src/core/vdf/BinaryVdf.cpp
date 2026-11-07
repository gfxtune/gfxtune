// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "BinaryVdf.h"

#include "Errors.h"

#include <QtEndian>

#include <bit>

namespace gfxtune::vdf
{

namespace
{

constexpr int MaxDepth = 128;

enum Type : quint8 {
    TypeSection = 0x00,
    TypeString = 0x01,
    TypeInt32 = 0x02,
    TypeFloat32 = 0x03,
    TypePointer = 0x04,
    TypeWideString = 0x05,
    TypeColor = 0x06,
    TypeUInt64 = 0x07,
    TypeEnd = 0x08,
    TypeInt64 = 0x0A,
    TypeEndAlt = 0x0B,
};

class Reader
{
public:
    explicit Reader(QByteArrayView data)
        : m_data(data)
    {
    }

    bool atEnd() const { return m_pos >= m_data.size(); }
    qsizetype pos() const { return m_pos; }

    bool byte(quint8 &out)
    {
        if (m_pos >= m_data.size()) {
            return false;
        }
        out = quint8(m_data[m_pos++]);
        return true;
    }

    template<typename T>
    bool number(T &out)
    {
        if (m_data.size() - m_pos < qsizetype(sizeof(T))) {
            return false;
        }
        out = qFromLittleEndian<T>(m_data.data() + m_pos);
        m_pos += qsizetype(sizeof(T));
        return true;
    }

    bool string(QString &out)
    {
        const qsizetype end = m_data.indexOf('\0', m_pos);
        if (end < 0) {
            return false;
        }
        out = QString::fromUtf8(m_data.sliced(m_pos, end - m_pos));
        m_pos = end + 1;
        return true;
    }

    // UTF-16LE text ending in a two-byte zero.
    bool wideString(QString &out)
    {
        for (qsizetype i = m_pos; i + 1 < m_data.size(); i += 2) {
            if (m_data[i] == '\0' && m_data[i + 1] == '\0') {
                const qsizetype units = (i - m_pos) / 2;
                out.resize(units);
                for (qsizetype u = 0; u < units; ++u) {
                    out[u] = QChar(qFromLittleEndian<quint16>(m_data.data() + m_pos + 2 * u));
                }
                m_pos = i + 2;
                return true;
            }
        }
        return false;
    }

private:
    QByteArrayView m_data;
    qsizetype m_pos = 0;
};

} // namespace

bool parseBinary(QByteArrayView data, Node &root, QString *error, const QStringList *keyTable)
{
    root = Node{};
    root.isSection = true;

    Reader in(data);
    std::vector<Node *> open{&root};

    while (!in.atEnd()) {
        const qsizetype offset = in.pos();
        quint8 type = 0;
        in.byte(type);

        if (type == TypeEnd || type == TypeEndAlt) {
            if (open.size() == 1) {
                return true; // end of the top level; anything after it is not ours
            }
            open.pop_back();
            continue;
        }

        Node entry;
        if (keyTable) {
            qint32 index = 0;
            if (!in.number(index)) {
                return fail(error, QStringLiteral("data cut off at byte %1").arg(offset));
            }
            if (index < 0 || index >= keyTable->size()) {
                return fail(error, QStringLiteral("key number %1 is not in the key table (byte %2)").arg(index).arg(offset));
            }
            entry.key = keyTable->at(index);
        } else if (!in.string(entry.key)) {
            return fail(error, QStringLiteral("data cut off at byte %1").arg(offset));
        }

        bool ok = true;
        switch (type) {
        case TypeSection:
            if (open.size() > MaxDepth) {
                return fail(error, QStringLiteral("sections nested too deeply at byte %1").arg(offset));
            }
            entry.isSection = true;
            open.back()->children.push_back(std::move(entry));
            open.push_back(&open.back()->children.back());
            continue;
        case TypeString:
            ok = in.string(entry.value);
            break;
        case TypeWideString:
            ok = in.wideString(entry.value);
            break;
        case TypeInt32:
        case TypePointer:
        case TypeColor: {
            qint32 v = 0;
            ok = in.number(v);
            entry.value = QString::number(v);
            break;
        }
        case TypeUInt64: {
            quint64 v = 0;
            ok = in.number(v);
            entry.value = QString::number(v);
            break;
        }
        case TypeInt64: {
            qint64 v = 0;
            ok = in.number(v);
            entry.value = QString::number(v);
            break;
        }
        case TypeFloat32: {
            quint32 bits = 0;
            ok = in.number(bits);
            entry.value = QString::number(double(std::bit_cast<float>(bits)));
            break;
        }
        default:
            return fail(error, QStringLiteral("unknown value type 0x%1 at byte %2").arg(type, 2, 16, QLatin1Char('0')).arg(offset));
        }
        if (!ok) {
            return fail(error, QStringLiteral("data cut off at byte %1").arg(offset));
        }
        open.back()->children.push_back(std::move(entry));
    }

    if (open.size() > 1) {
        return fail(error, QStringLiteral("data ends inside a section"));
    }
    return true;
}

} // namespace gfxtune::vdf
