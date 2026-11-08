#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

#include <memory>

class QNetworkReply;

namespace cirrus::ui
{

class Player;

// macOS Now Playing (media keys, Control Centre, the menu bar's Now Playing), driven by
// App.player like the window is (docs/ui/pages/shell.md §7). Not yet verified on macOS.
class NowPlaying : public QObject
{
    Q_OBJECT
  public:
    explicit NowPlaying(Player* player, QObject* parent = nullptr);
    ~NowPlaying() override;

  private:
    struct Native;

    void update();
    void fetchArtwork(const QString& url);

    Player* m_player;
    std::unique_ptr<Native> m_native;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_artworkReply;
};

} // namespace cirrus::ui
