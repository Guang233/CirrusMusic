#pragma once

#include "loadable_model.h"
#include "types.h"

namespace cirrus::ui
{

class SongListModel;

// Playlist details page. Setting `playlistKey` loads it:
//   PlaylistPageModel { id: model; playlistKey: route.key }
//   SongList { model: model.songs }
class PlaylistPageModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(QString playlistKey READ playlistKey WRITE setPlaylistKey NOTIFY playlistKeyChanged)
    Q_PROPERTY(QString title READ title NOTIFY headerChanged)
    Q_PROPERTY(QString description READ description NOTIFY headerChanged)
    Q_PROPERTY(QString creator READ creator NOTIFY headerChanged)
    Q_PROPERTY(QString coverKey READ coverKey NOTIFY headerChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit PlaylistPageModel(QObject* parent = nullptr);

    [[nodiscard]] QString playlistKey() const { return m_key; }
    void setPlaylistKey(const QString& key);
    [[nodiscard]] QString title() const { return m_info.title; }
    [[nodiscard]] QString description() const { return m_info.description; }
    [[nodiscard]] QString creator() const { return m_info.creator; }
    [[nodiscard]] QString coverKey() const { return m_info.coverKey; }
    [[nodiscard]] SongListModel* songs() const { return m_songs; }

  signals:
    void playlistKeyChanged();
    void headerChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    PlaylistInfo m_info;
    SongListModel* m_songs;
};

} // namespace cirrus::ui
