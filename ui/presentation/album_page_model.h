#pragma once

#include "loadable_model.h"
#include "types.h"

#include <QVariantList>

namespace cirrus::ui
{

class SongListModel;

// Album details page. Setting `albumKey` loads it. Partial when the source reports
// the track list as incomplete.
class AlbumPageModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(QString albumKey READ albumKey WRITE setAlbumKey NOTIFY albumKeyChanged)
    Q_PROPERTY(QString title READ title NOTIFY headerChanged)
    Q_PROPERTY(QVariantList artists READ artists NOTIFY headerChanged) // [{key, name}]
    Q_PROPERTY(QString artistText READ artistText NOTIFY headerChanged)
    Q_PROPERTY(QString coverKey READ coverKey NOTIFY headerChanged)
    Q_PROPERTY(QString description READ description NOTIFY headerChanged)
    // "2024 · 4 首 · 13 分钟"
    Q_PROPERTY(QString summary READ summary NOTIFY headerChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit AlbumPageModel(QObject* parent = nullptr);

    [[nodiscard]] QString albumKey() const { return m_key; }
    void setAlbumKey(const QString& key);
    [[nodiscard]] QString title() const { return m_page.info.title; }
    [[nodiscard]] QVariantList artists() const;
    [[nodiscard]] QString artistText() const;
    [[nodiscard]] QString coverKey() const { return m_page.info.coverKey; }
    [[nodiscard]] QString description() const { return m_page.info.description; }
    [[nodiscard]] QString summary() const;
    [[nodiscard]] SongListModel* songs() const { return m_songs; }

  signals:
    void albumKeyChanged();
    void headerChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    AlbumPage m_page;
    SongListModel* m_songs;
};

} // namespace cirrus::ui
