#pragma once

#include "request.h"

#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>

namespace cirrus::ui
{

class SongListModel;

// Fetches details for a SongListModel's rows that only have keys (large playlists),
// in batches of kSongDetailsBatch, one request at a time; urgent batches (rows on
// screen) go ahead of the others. A failure drops the queue: the rows are asked for
// again next time. Used by the page models that show such lists.
class SongDetailLoader : public QObject
{
    Q_OBJECT

  public:
    explicit SongDetailLoader(SongListModel* songs, QObject* parent = nullptr);

    // Rows [first, last] and the rest of their batches, ahead of other waiting ones.
    void ensureLoaded(int first, int last);
    void loadAll();
    void clear();

    // Songs asked for and not answered yet.
    [[nodiscard]] int waiting() const { return int(m_pendingKeys.size()); }
    [[nodiscard]] int missingCount() const { return m_missingCount; }

  signals:
    void waitingChanged();
    void missingCountChanged();
    // Details arrived for some rows.
    void filled();

  private:
    void queue(const QStringList& keys, bool urgent);
    void sendNext();
    void updateMissingCount();

    SongListModel* m_songs;
    QList<QStringList> m_queue;
    Request m_request;
    QSet<QString> m_pendingKeys;
    int m_missingCount = 0;
};

} // namespace cirrus::ui
