// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

#pragma once

#include "steam/Game.h"

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QtQmlIntegration>

namespace gfxtune
{

// The installed games, in the order the scan sorted them.
class GameListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

public:
    enum Role {
        AppIdRole = Qt::UserRole + 1,
        NameRole,
        DetailRole,
        CoverRole,
        CustomizedRole,
        RunningRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Updates in place where it can, so the view keeps its selection and
    // scroll position when a scan only changes details or adds a game.
    void setGames(QList<steam::Game> games);
    void setCustomized(const QSet<quint32> &appIds);
    void setRunning(const QSet<quint32> &appIds);

    const steam::Game *find(quint32 appId) const;
    const QList<steam::Game> &games() const { return m_games; }

    static QString detailText(const steam::Game &game);

private:
    void changedRows(int role);

    QList<steam::Game> m_games;
    QSet<quint32> m_customized;
    QSet<quint32> m_running;
};

// The list as the view shows it: filtered by the search text.
class GameFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

public:
    explicit GameFilterModel(QObject *parent = nullptr);

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &text);

    Q_INVOKABLE int appIdAt(int row) const;
    Q_INVOKABLE int rowOf(int appId) const;

Q_SIGNALS:
    void filterTextChanged();

private:
    QString m_filterText;
};

} // namespace gfxtune
