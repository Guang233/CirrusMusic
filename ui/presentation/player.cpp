#include "player.h"

#include "sources.h"

#include <algorithm>

namespace cirrus::ui
{

Player::Player(PlaybackSource* source, QObject* parent) : QObject(parent), m_source(source)
{
    if (!m_source)
        return;
    m_source->setChangedHandler([this]() { update(); });
    update();
}

void Player::update()
{
    m_snapshot = m_source->snapshot();
    SongItem current(m_snapshot.current);
    if (current != m_current) {
        m_current = std::move(current);
        emit currentChanged();
    }
    emit changed();
}

QString Player::failureMessage() const
{
    if (m_snapshot.status != playback::Failed)
        return {};
    if (m_snapshot.current.unavailable)
        return tr("这首歌暂时无法播放");
    switch (m_snapshot.failure ? m_snapshot.failure->kind : failure::Unknown) {
    case failure::Network:
    case failure::Timeout:
        return tr("网络连接失败，无法播放");
    case failure::SignedOut:
        return tr("登录后才能播放");
    case failure::Rejected:
        return tr("这首歌暂时无法播放");
    default:
        return tr("播放失败");
    }
}

void Player::playSongs(const QStringList& keys, int index)
{
    if (m_source && index >= 0 && index < keys.size())
        m_source->playSongs(keys, index);
}

void Player::playVoices(const QStringList& keys, int index)
{
    if (canPlayVoices() && index >= 0 && index < keys.size())
        m_source->playVoices(keys, index);
}

bool Player::canPlayVoices() const { return m_source && m_source->canPlayVoices(); }

void Player::playPause()
{
    if (m_source)
        m_source->playPause();
}

void Player::next()
{
    if (m_source)
        m_source->next();
}

void Player::previous()
{
    if (m_source)
        m_source->previous();
}

void Player::seek(qint64 positionMs)
{
    if (m_source && m_snapshot.seekable)
        m_source->seek(std::max<qint64>(positionMs, 0));
}

void Player::setVolume(double volume)
{
    if (m_source)
        m_source->setVolume(std::clamp(volume, 0.0, 1.0));
}

void Player::setOrder(int order)
{
    if (m_source && (order == playback::Sequential || order == playback::Shuffle))
        m_source->setOrder(playback::Order(order));
}

void Player::setRepeat(int repeat)
{
    if (m_source && repeat >= playback::RepeatOff && repeat <= playback::RepeatOne)
        m_source->setRepeat(playback::Repeat(repeat));
}

void Player::startFm()
{
    if (m_source)
        m_source->startFm();
}

} // namespace cirrus::ui
