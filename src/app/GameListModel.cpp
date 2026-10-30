// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#include "GameListModel.h"

#include <QUrl>

namespace gfxtune
{

using steam::Game;
using steam::Runtime;

int GameListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_games.size());
}

QVariant GameListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Game &game = m_games.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return game.name;
    case AppIdRole:
        return int(game.appId);
    case DetailRole:
        return detailText(game);
    case CoverRole:
        return game.coverPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(game.coverPath);
    case SettingsNoteRole:
        return m_settingsNotes.value(game.appId);
    case RunningRole:
        return m_running.contains(game.appId);
    default:
        return {};
    }
}

QHash<int, QByteArray> GameListModel::roleNames() const
{
    return {
        {AppIdRole, "appId"},
        {NameRole, "name"},
        {DetailRole, "detail"},
        {CoverRole, "cover"},
        {SettingsNoteRole, "settingsNote"},
        {RunningRole, "running"},
    };
}

QString GameListModel::detailText(const Game &game)
{
    switch (game.runtime) {
    case Runtime::Native:
        return QStringLiteral("Native");
    case Runtime::Proton:
        return game.tool.displayName.isEmpty() ? QStringLiteral("Proton") : game.tool.displayName;
    case Runtime::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

void GameListModel::setGames(QList<Game> games)
{
    auto ids = [](const QList<Game> &list) {
        QList<quint32> out;
        out.reserve(list.size());
        for (const Game &g : list) {
            out.append(g.appId);
        }
        return out;
    };
    const QList<quint32> newIds = ids(games);
    const QSet<quint32> newSet(newIds.cbegin(), newIds.cend());

    // Remove games that are gone, bottom up so row numbers stay valid.
    for (int row = int(m_games.size()) - 1; row >= 0; --row) {
        if (!newSet.contains(m_games.at(row).appId)) {
            beginRemoveRows({}, row, row);
            m_games.removeAt(row);
            endRemoveRows();
        }
    }

    // The games left must already be in the new order; otherwise (a rename
    // moved one) start over.
    const QList<quint32> kept = ids(m_games);
    QList<quint32> newOrderOfKept;
    const QSet<quint32> keptSet(kept.cbegin(), kept.cend());
    for (quint32 id : newIds) {
        if (keptSet.contains(id)) {
            newOrderOfKept.append(id);
        }
    }
    if (kept != newOrderOfKept) {
        beginResetModel();
        m_games = std::move(games);
        endResetModel();
        return;
    }

    // Insert new games at their place, updating the rest.
    for (int row = 0; row < games.size(); ++row) {
        if (row < m_games.size() && m_games.at(row).appId == games.at(row).appId) {
            m_games[row] = games.at(row);
            continue;
        }
        beginInsertRows({}, row, row);
        m_games.insert(row, games.at(row));
        endInsertRows();
    }
    if (!m_games.isEmpty()) {
        Q_EMIT dataChanged(index(0), index(int(m_games.size()) - 1));
    }
}

void GameListModel::setSettingsNotes(const QHash<quint32, QString> &notes)
{
    if (notes == m_settingsNotes) {
        return;
    }
    m_settingsNotes = notes;
    changedRows(SettingsNoteRole);
}

void GameListModel::setRunning(const QSet<quint32> &appIds)
{
    if (appIds == m_running) {
        return;
    }
    m_running = appIds;
    changedRows(RunningRole);
}

const Game *GameListModel::find(quint32 appId) const
{
    for (const Game &game : m_games) {
        if (game.appId == appId) {
            return &game;
        }
    }
    return nullptr;
}

void GameListModel::changedRows(int role)
{
    if (!m_games.isEmpty()) {
        Q_EMIT dataChanged(index(0), index(int(m_games.size()) - 1), {role});
    }
}

GameFilterModel::GameFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterRole(GameListModel::NameRole);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
}

void GameFilterModel::setFilterText(const QString &text)
{
    if (text == m_filterText) {
        return;
    }
    m_filterText = text;
    setFilterFixedString(text.trimmed());
    Q_EMIT filterTextChanged();
}

int GameFilterModel::appIdAt(int row) const
{
    const QModelIndex i = index(row, 0);
    return i.isValid() ? i.data(GameListModel::AppIdRole).toInt() : 0;
}

int GameFilterModel::rowOf(int appId) const
{
    for (int row = 0; row < rowCount(); ++row) {
        if (index(row, 0).data(GameListModel::AppIdRole).toInt() == appId) {
            return row;
        }
    }
    return -1;
}

} // namespace gfxtune
