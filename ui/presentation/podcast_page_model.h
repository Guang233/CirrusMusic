#pragma once

#include "loadable_model.h"
#include "paged_model.h"
#include "types.h"

namespace cirrus::ui
{

class SongListModel;

// A podcast's voices, a page at a time, newest or oldest first
// (docs/ui/pages/search.md §3.4). Owned by PodcastPageModel.
class PodcastVoicesModel : public PagedModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Use PodcastPageModel.voices")
    // Oldest first instead of newest; changing it loads from the start.
    Q_PROPERTY(bool oldestFirst READ oldestFirst WRITE setOldestFirst NOTIFY oldestFirstChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit PodcastVoicesModel(QObject* parent = nullptr);

    [[nodiscard]] bool oldestFirst() const { return m_oldestFirst; }
    void setOldestFirst(bool oldestFirst);
    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    void setPodcastKey(const QString& key);

  signals:
    void oldestFirstChanged();

  protected:
    Request load() override { return loadFirst(); }
    void clear() override;
    Request loadPage(PageRequest page) override;
    [[nodiscard]] int loadedCount() const override;

  private:
    QString m_key;
    bool m_oldestFirst = false;
    SongListModel* m_songs;
};

// Podcast page: the header, saving it, and its voices (loaded alongside). Setting
// `podcastKey` loads it.
class PodcastPageModel : public LoadableModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString podcastKey READ podcastKey WRITE setPodcastKey NOTIFY podcastKeyChanged)
    Q_PROPERTY(QString title READ title NOTIFY headerChanged)
    Q_PROPERTY(QString description READ description NOTIFY headerChanged)
    Q_PROPERTY(QString coverKey READ coverKey NOTIFY headerChanged)
    // "120 期"; empty when unknown.
    Q_PROPERTY(QString countText READ countText NOTIFY headerChanged)
    // Saved by the account. Setting it saves or unsaves (optimistic; reverted with a
    // `notice` on failure).
    Q_PROPERTY(bool subscribed READ subscribed WRITE setSubscribed NOTIFY subscribedChanged)
    Q_PROPERTY(cirrus::ui::PodcastVoicesModel* voices READ voices CONSTANT)

  public:
    explicit PodcastPageModel(QObject* parent = nullptr);

    [[nodiscard]] QString podcastKey() const { return m_key; }
    void setPodcastKey(const QString& key);
    [[nodiscard]] QString title() const { return m_info.title; }
    [[nodiscard]] QString description() const { return m_info.description; }
    [[nodiscard]] QString coverKey() const { return m_info.coverKey; }
    [[nodiscard]] QString countText() const;
    [[nodiscard]] bool subscribed() const { return m_info.subscribed; }
    void setSubscribed(bool subscribed);
    [[nodiscard]] PodcastVoicesModel* voices() const { return m_voices; }

  signals:
    void podcastKeyChanged();
    void headerChanged();
    void subscribedChanged();
    // A short message for the user (a failed save).
    void notice(const QString& message);
    // The account's saved podcasts changed.
    void libraryChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    PodcastInfo m_info;
    PodcastVoicesModel* m_voices;
    Request m_subscribeRequest;
};

} // namespace cirrus::ui
