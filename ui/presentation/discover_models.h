#pragma once

#include "loadable_model.h"
#include "paged_model.h"
#include "types.h"

#include <QVariantList>

namespace cirrus::ui
{

class CollectionListModel;

// Discover's tabs (docs/ui/pages/discover.md §3). Each loads when first shown
// (ensureLoaded); changing a filter loads again in place, the old cards staying until
// the new ones arrive.

// The playlist categories: trending ones for the chip row, all of them by group for
// the dialog, and those with premium playlists.
class PlaylistCategoriesModel : public LoadableModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList hot READ hot NOTIFY categoriesChanged)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY categoriesChanged) // [{name, categories}]
    Q_PROPERTY(QStringList premium READ premium NOTIFY categoriesChanged)

  public:
    explicit PlaylistCategoriesModel(QObject* parent = nullptr);

    [[nodiscard]] QStringList hot() const { return m_categories.hot; }
    [[nodiscard]] QVariantList groups() const;
    [[nodiscard]] QStringList premium() const { return m_categories.premium; }

  signals:
    void categoriesChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    PlaylistCategories m_categories;
};

// Shared by the card tabs: the cards, and loading again when a filter changes.
class DiscoverCardsModel : public PagedModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Base of the discover tabs")
    Q_PROPERTY(cirrus::ui::CollectionListModel* items READ items CONSTANT)

  public:
    explicit DiscoverCardsModel(QObject* parent = nullptr);

    [[nodiscard]] CollectionListModel* items() const { return m_items; }
    Q_INVOKABLE void ensureLoaded();

  protected:
    // A filter changed: loads again unless not shown yet.
    void filterChanged();
    void clear() override;
    Request load() override { return loadFirst(); }
    [[nodiscard]] int loadedCount() const override;
    // Reply adapters for the first page and the next ones.
    Reply<ResultPage<CollectionItem>> firstPage();
    Reply<ResultPage<CollectionItem>> nextPage();

    CollectionListModel* m_items;
};

// Playlists of a category, most played, newest or premium.
class DiscoverPlaylistsModel : public DiscoverCardsModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged) // "" all
    Q_PROPERTY(int order READ order WRITE setOrder NOTIFY orderChanged)                 // Order

  public:
    enum Order { Hot, Newest, Premium };
    Q_ENUM(Order)

    explicit DiscoverPlaylistsModel(QObject* parent = nullptr);

    [[nodiscard]] QString category() const { return m_category; }
    void setCategory(const QString& category);
    [[nodiscard]] int order() const { return m_order; }
    void setOrder(int order);

  signals:
    void categoryChanged();
    void orderChanged();

  protected:
    Request loadPage(PageRequest page) override;

  private:
    QString m_category;
    int m_order = Hot;
    // Premium pages: where the next one starts.
    qint64 m_cursor = 0;
};

// Artists by kind, area and initial ("" popular).
class DiscoverArtistsModel : public DiscoverCardsModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int kind READ kind WRITE setKind NOTIFY filtersChanged)
    Q_PROPERTY(int area READ area WRITE setArea NOTIFY filtersChanged)
    Q_PROPERTY(QString initial READ initial WRITE setInitial NOTIFY filtersChanged)

  public:
    explicit DiscoverArtistsModel(QObject* parent = nullptr);

    [[nodiscard]] int kind() const { return m_kind; }
    void setKind(int kind);
    [[nodiscard]] int area() const { return m_area; }
    void setArea(int area);
    [[nodiscard]] QString initial() const { return m_initial; }
    void setInitial(const QString& initial);

  signals:
    void filtersChanged();

  protected:
    Request loadPage(PageRequest page) override;

  private:
    int m_kind = 0;
    int m_area = 0;
    QString m_initial;
};

// New albums by area: newest (paged), or this week's or month's chart (all at once).
class DiscoverAlbumsModel : public DiscoverCardsModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int area READ area WRITE setArea NOTIFY filtersChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY filtersChanged) // Mode

  public:
    enum Mode { Newest, Week, Month };
    Q_ENUM(Mode)

    explicit DiscoverAlbumsModel(QObject* parent = nullptr);

    [[nodiscard]] int area() const { return m_area; }
    void setArea(int area);
    [[nodiscard]] int mode() const { return m_mode; }
    void setMode(int mode);

  signals:
    void filtersChanged();

  protected:
    Request loadPage(PageRequest page) override;

  private:
    int m_area = 0;
    int m_mode = Newest;
};

} // namespace cirrus::ui
