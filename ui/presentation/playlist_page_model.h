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
    Q_PROPERTY(QStringList tags READ tags NOTIFY headerChanged)
    Q_PROPERTY(QString updatedText READ updatedText NOTIFY headerChanged) // empty when unknown
    // -1 when unknown.
    Q_PROPERTY(qint64 playCount READ playCount NOTIFY headerChanged)
    Q_PROPERTY(qint64 subscriberCount READ subscriberCount NOTIFY headerChanged)
    Q_PROPERTY(qint64 commentCount READ commentCount NOTIFY headerChanged)
    Q_PROPERTY(bool own READ own NOTIFY headerChanged)
    // Shows the new state at once; reverts and emits writeFailed when the save fails.
    Q_PROPERTY(bool subscribed READ subscribed WRITE setSubscribed NOTIFY subscribedChanged)
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
    [[nodiscard]] QStringList tags() const { return m_info.tags; }
    [[nodiscard]] QString updatedText() const;
    [[nodiscard]] qint64 playCount() const { return m_info.playCount; }
    [[nodiscard]] qint64 subscriberCount() const { return m_info.subscriberCount; }
    [[nodiscard]] qint64 commentCount() const { return m_info.commentCount; }
    [[nodiscard]] bool own() const { return m_info.own; }
    [[nodiscard]] bool subscribed() const { return m_info.subscribed; }
    void setSubscribed(bool subscribed);

  signals:
    void playlistKeyChanged();
    void headerChanged();
    void subscribedChanged();
    // A change the account made here was saved: the library has to reload.
    void libraryChanged();
    // A change could not be saved and was undone; `message` says why, translated.
    void writeFailed(const QString& message);

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    PlaylistInfo m_info;
    SongListModel* m_songs;
    Request m_subscribeRequest;
};

} // namespace cirrus::ui
