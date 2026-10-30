#pragma once

#include "loadable_model.h"
#include "types.h"

namespace cirrus::ui
{

class CollectionListModel;
class SongListModel;

// Artist page: header, popular songs and albums. Setting `artistKey` loads it.
class ArtistPageModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(QString artistKey READ artistKey WRITE setArtistKey NOTIFY artistKeyChanged)
    Q_PROPERTY(QString name READ name NOTIFY headerChanged)
    Q_PROPERTY(QString description READ description NOTIFY headerChanged)
    Q_PROPERTY(QString avatarKey READ avatarKey NOTIFY headerChanged)
    // "12 首歌曲 · 3 张专辑"
    Q_PROPERTY(QString summary READ summary NOTIFY headerChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* topSongs READ topSongs CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionListModel* albums READ albums CONSTANT)

  public:
    explicit ArtistPageModel(QObject* parent = nullptr);

    [[nodiscard]] QString artistKey() const { return m_key; }
    void setArtistKey(const QString& key);
    [[nodiscard]] QString name() const { return m_info.name; }
    [[nodiscard]] QString description() const { return m_info.description; }
    [[nodiscard]] QString avatarKey() const { return m_info.avatarKey; }
    [[nodiscard]] QString summary() const;
    [[nodiscard]] SongListModel* topSongs() const { return m_topSongs; }
    [[nodiscard]] CollectionListModel* albums() const { return m_albums; }

  signals:
    void artistKeyChanged();
    void headerChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    ArtistInfo m_info;
    SongListModel* m_topSongs;
    CollectionListModel* m_albums;
};

} // namespace cirrus::ui
