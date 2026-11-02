#include "library.h"

#include "session.h"
#include "sources.h"

namespace cirrus::ui
{

Library::Library(LibrarySource* source, Session* session, QObject* parent)
    : QObject(parent), m_source(source), m_session(session)
{
    connect(session, &Session::changed, this, [this]() {
        const bool signedIn = m_session->signedIn();
        if (signedIn && (m_liked.isEmpty() || m_likedPlaylistKey.isEmpty()) && !m_load.pending())
            reload();
        if (!signedIn && !m_likedPlaylistKey.isEmpty()) {
            m_likedPlaylistLoad.cancel();
            m_likedPlaylistKey.clear();
            emit likedPlaylistChanged();
        }
        if (!signedIn && !m_liked.isEmpty()) {
            m_load.cancel();
            m_writes.clear();
            m_liked.clear();
            ++m_revision;
            emit likedChanged();
        }
    });
    if (session->signedIn())
        reload();
}

void Library::reload()
{
    if (!m_session->signedIn())
        return;
    m_load = m_source->likedSongKeys([this](Outcome<QStringList> outcome) {
        m_load.release();
        if (!outcome.value)
            return;
        m_liked = QSet<QString>(outcome.value->cbegin(), outcome.value->cend());
        ++m_revision;
        emit likedChanged();
    });
    if (!m_likedPlaylistKey.isEmpty())
        return;
    m_likedPlaylistLoad = m_source->likedPlaylist([this](Outcome<CollectionItem> outcome) {
        m_likedPlaylistLoad.release();
        if (!outcome.value || outcome.value->key == m_likedPlaylistKey)
            return;
        m_likedPlaylistKey = outcome.value->key;
        emit likedPlaylistChanged();
    });
}

void Library::setLikedLocally(const QString& songKey, bool liked)
{
    if (liked == m_liked.contains(songKey))
        return;
    if (liked)
        m_liked.insert(songKey);
    else
        m_liked.remove(songKey);
    ++m_revision;
    emit likedChanged();
}

void Library::setLiked(const QString& songKey, bool liked)
{
    if (songKey.isEmpty() || liked == isLiked(songKey))
        return;
    if (!m_session->signedIn()) {
        emit notice(tr("登录后才能喜欢歌曲"));
        return;
    }
    setLikedLocally(songKey, liked);
    m_writes[songKey] =
        m_source->setSongLiked(songKey, liked, [this, songKey, liked](Outcome<bool> outcome) {
            m_writes[songKey].release();
            if (outcome.value)
                return;
            setLikedLocally(songKey, !liked);
            emit notice(liked ? tr("没能喜欢这首歌，请稍后再试") : tr("没能取消喜欢，请稍后再试"));
        });
}

void Library::addToPlaylist(const QString& playlistKey, const QString& title,
                            const QStringList& songKeys)
{
    if (playlistKey.isEmpty() || songKeys.isEmpty())
        return;
    m_writes[playlistKey] = m_source->addToPlaylist(
        playlistKey, songKeys, [this, playlistKey, title](Outcome<bool> outcome) {
            m_writes[playlistKey].release();
            if (outcome.value) {
                emit playlistChanged(playlistKey);
                emit notice(tr("已收藏到「%1」").arg(title));
            } else {
                emit notice(tr("没能收藏到「%1」，请稍后再试").arg(title));
            }
        });
}

} // namespace cirrus::ui
