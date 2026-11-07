#include "tray.h"

#include "app.h"
#include "app_icon.h"
#include "player.h"

#include <QMenu>
#include <QSystemTrayIcon>

namespace cirrus::ui
{

Tray::Tray(QObject* parent)
    : QObject(parent), m_menu(std::make_unique<QMenu>()), m_icon(new QSystemTrayIcon(this))
{
    Player* player = App::instance()->player();

    m_song = m_menu->addAction(QString());
    m_song->setEnabled(false);
    m_menu->addSeparator();
    m_playPause = m_menu->addAction(QString(), player, &Player::playPause);
    m_previous = m_menu->addAction(tr("上一首"), player, &Player::previous);
    m_next = m_menu->addAction(tr("下一首"), player, &Player::next);
    m_menu->addSeparator();
    m_menu->addAction(tr("显示主窗口"), this, &Tray::showRequested);
    m_menu->addAction(tr("退出 Cirrus"), this, &Tray::quitRequested);

    m_icon->setIcon(appIcon());
    m_icon->setContextMenu(m_menu.get());
    connect(m_icon, &QSystemTrayIcon::activated, this,
            [this, player](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::MiddleClick)
                    player->playPause();
                else if (reason == QSystemTrayIcon::Trigger ||
                         reason == QSystemTrayIcon::DoubleClick)
                    emit showRequested();
            });
    connect(player, &Player::changed, this, &Tray::update);
    connect(player, &Player::currentChanged, this, &Tray::update);
    update();
}

Tray::~Tray() = default;

bool Tray::available() { return QSystemTrayIcon::isSystemTrayAvailable(); }

void Tray::show() { m_icon->show(); }

void Tray::update()
{
    const Player* player = App::instance()->player();
    const SongItem song = player->current();
    const bool hasSong = !song.key.isEmpty();
    const QString title =
        song.artistText.isEmpty() ? song.title : tr("%1 - %2").arg(song.title, song.artistText);
    m_song->setText(hasSong ? title : tr("未在播放"));
    m_icon->setToolTip(hasSong ? title : QStringLiteral("Cirrus"));

    const bool playing =
        player->status() == playback::Playing || player->status() == playback::Loading;
    m_playPause->setText(playing ? tr("暂停") : tr("播放"));
    m_playPause->setEnabled(hasSong);
    m_previous->setEnabled(player->canPrevious());
    m_next->setEnabled(player->canNext());
}

} // namespace cirrus::ui
