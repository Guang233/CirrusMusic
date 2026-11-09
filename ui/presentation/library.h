#pragma once

#include "request.h"

#include <QObject>
#include <QSet>
#include <QtQml/qqmlregistration.h>

#include <map>

namespace cirrus::ui
{

class LibrarySource;
class Session;

// The account's liked songs, and writes that are not about the page they start from
// (App.library; docs/ui/presentation.md §5.1). Writes show at once and are undone
// when they fail; `notice` tells the user either way where it matters.
class Library : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.library")
    // Grows whenever the liked set changes, for bindings that call isLiked().
    Q_PROPERTY(int likedRevision READ likedRevision NOTIFY likedChanged)
    Q_PROPERTY(int likedCount READ likedCount NOTIFY likedChanged)
    // The account's "我喜欢的音乐" playlist; empty until known (or signed out).
    Q_PROPERTY(QString likedPlaylistKey READ likedPlaylistKey NOTIFY likedPlaylistChanged)
    // Others' playlists can be saved (the playlist page shows "收藏" then).
    Q_PROPERTY(bool canSavePlaylists READ canSavePlaylists CONSTANT)

  public:
    Library(LibrarySource* source, Session* session, QObject* parent = nullptr);

    [[nodiscard]] int likedRevision() const { return m_revision; }
    [[nodiscard]] int likedCount() const { return int(m_liked.size()); }
    [[nodiscard]] QString likedPlaylistKey() const { return m_likedPlaylistKey; }
    [[nodiscard]] bool canSavePlaylists() const;
    Q_INVOKABLE bool isLiked(const QString& songKey) const { return m_liked.contains(songKey); }
    Q_INVOKABLE void setLiked(const QString& songKey, bool liked);
    // `title` names the playlist in the notice.
    Q_INVOKABLE void addToPlaylist(const QString& playlistKey, const QString& title,
                                   const QStringList& songKeys);
    // Fetches the liked songs again.
    Q_INVOKABLE void reload();
    // A playlist's songs were changed elsewhere (the order page): emits playlistChanged.
    void notifyPlaylistChanged(const QString& playlistKey) { emit playlistChanged(playlistKey); }

  signals:
    void likedChanged();
    void likedPlaylistChanged();
    // A playlist's songs changed through here: pages showing it reload.
    void playlistChanged(const QString& playlistKey);
    // Something to tell the user, translated.
    void notice(const QString& message);

  private:
    void setLikedLocally(const QString& songKey, bool liked);

    LibrarySource* m_source;
    Session* m_session;
    QSet<QString> m_liked;
    int m_revision = 0;
    Request m_load;
    QString m_likedPlaylistKey;
    Request m_likedPlaylistLoad;
    // One write per song key or playlist key at a time; a newer one replaces it.
    std::map<QString, Request> m_writes;
};

} // namespace cirrus::ui
