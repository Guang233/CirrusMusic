#pragma once

#include "loadable_model.h"
#include "song_item.h"
#include "types.h"

#include <QVariantList>

namespace cirrus::ui
{

class CollectionListModel;
class SongListModel;

// The song's wiki (docs/ui/pages/song.md §2): tag cards and text sections. Owned by
// SongPageModel, which hands it the song once read (the wiki read wants its title and
// album).
class SongWikiModel : public LoadableModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use SongPageModel.wiki")
    // [{title, values}]
    Q_PROPERTY(QVariantList cards READ cards NOTIFY contentChanged)
    // [{title, description, rows: [{label, text}]}]
    Q_PROPERTY(QVariantList sections READ sections NOTIFY contentChanged)

  public:
    explicit SongWikiModel(QObject* parent = nullptr);

    [[nodiscard]] QVariantList cards() const;
    [[nodiscard]] QVariantList sections() const;
    // Loads it for this song; an empty key clears it.
    void setSong(const SongInfo& song);

  signals:
    void contentChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    SongInfo m_song;
    SongWiki m_wiki;
};

// Songs like this one: one batch from the service (it pages unreliably).
class SimilarSongsModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Use SongPageModel.similarSongs")
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit SimilarSongsModel(QObject* parent = nullptr);

    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    void setSongKey(const QString& key);

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    SongListModel* m_songs;
};

// Playlists like this song (the service's seed is the song).
class SimilarPlaylistsModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Use SongPageModel.similarPlaylists")
    Q_PROPERTY(cirrus::ui::CollectionListModel* items READ items CONSTANT)

  public:
    explicit SimilarPlaylistsModel(QObject* parent = nullptr);

    [[nodiscard]] CollectionListModel* items() const { return m_items; }
    void setSongKey(const QString& key);

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    CollectionListModel* m_items;
};

// Song page (docs/ui/pages/song.md): the song itself, then its wiki and what is like it,
// each section loading and failing on its own. Setting `songKey` loads it all.
class SongPageModel : public LoadableModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString songKey READ songKey WRITE setSongKey NOTIFY songKeyChanged)
    Q_PROPERTY(cirrus::ui::SongItem song READ song NOTIFY songChanged)
    // "2024年5月1日发行", empty when unknown.
    Q_PROPERTY(QString dateText READ dateText NOTIFY songChanged)
    Q_PROPERTY(cirrus::ui::SongWikiModel* wiki READ wiki CONSTANT)
    Q_PROPERTY(cirrus::ui::SimilarSongsModel* similarSongs READ similarSongs CONSTANT)
    Q_PROPERTY(cirrus::ui::SimilarPlaylistsModel* similarPlaylists READ similarPlaylists CONSTANT)

  public:
    explicit SongPageModel(QObject* parent = nullptr);

    [[nodiscard]] QString songKey() const { return m_key; }
    void setSongKey(const QString& key);
    [[nodiscard]] SongItem song() const { return SongItem(m_song); }
    [[nodiscard]] QString dateText() const;
    [[nodiscard]] SongWikiModel* wiki() const { return m_wiki; }
    [[nodiscard]] SimilarSongsModel* similarSongs() const { return m_similarSongs; }
    [[nodiscard]] SimilarPlaylistsModel* similarPlaylists() const { return m_similarPlaylists; }

  signals:
    void songKeyChanged();
    void songChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    SongInfo m_song;
    SongWikiModel* m_wiki;
    SimilarSongsModel* m_similarSongs;
    SimilarPlaylistsModel* m_similarPlaylists;
};

} // namespace cirrus::ui
