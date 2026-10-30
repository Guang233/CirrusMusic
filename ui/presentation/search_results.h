#pragma once

#include "paged_model.h"

namespace cirrus::ui
{

class CollectionListModel;
class SongListModel;

// Song results of a search (one tab of SearchModel).
class SongResults : public PagedModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Owned by SearchModel")
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit SongResults(QObject* parent = nullptr);

    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    // New query: back to Idle; ensureLoaded() starts it.
    void setQuery(const QString& query);
    void ensureLoaded();

  protected:
    Request load() override { return loadFirst(); }
    void clear() override;
    Request loadPage(PageRequest page) override;
    [[nodiscard]] int loadedCount() const override;

  private:
    QString m_query;
    SongListModel* m_songs;
};

// Album, artist or playlist results of a search.
class CollectionResults : public PagedModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Owned by SearchModel")
    Q_PROPERTY(int kind READ kind CONSTANT) // Collection.Kind
    Q_PROPERTY(cirrus::ui::CollectionListModel* items READ items CONSTANT)

  public:
    explicit CollectionResults(collection::Kind kind, QObject* parent = nullptr);

    [[nodiscard]] int kind() const { return m_kind; }
    [[nodiscard]] CollectionListModel* items() const { return m_items; }
    void setQuery(const QString& query);
    void ensureLoaded();

  protected:
    Request load() override { return loadFirst(); }
    void clear() override;
    Request loadPage(PageRequest page) override;
    [[nodiscard]] int loadedCount() const override;

  private:
    collection::Kind m_kind;
    QString m_query;
    CollectionListModel* m_items;
};

} // namespace cirrus::ui
