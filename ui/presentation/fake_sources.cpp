#include "fake_sources.h"

#include "fake_controls.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <numeric>

namespace cirrus::ui
{

namespace
{
// Embedded by the Cirrus.Samples module.
const QString kSamples = QStringLiteral(":/qt/qml/Cirrus/Samples/");

// Replies after the controlled delay, unless canceled or `context` is gone first.
// The failure to inject is decided when the request starts.
template <class T>
Request replyLater(QObject* context, FakeControls* controls, Reply<T> reply,
                   std::function<Outcome<T>()> produce)
{
    auto alive = std::make_shared<bool>(true);
    const int failure = controls->takeFailure();
    QTimer::singleShot(
        controls->nextDelay(), context,
        [alive, failure, controls, reply = std::move(reply), produce = std::move(produce)]() {
            if (!*alive)
                return;
            *alive = false;
            Outcome<T> outcome;
            if (failure >= 0) {
                outcome.failure = Failure{failure::Kind(failure), std::nullopt};
                if (failure == failure::RateLimited)
                    outcome.failure->retryAfterMs = 3000;
            } else {
                outcome = produce();
                outcome.partial = outcome.partial || controls->partial();
            }
            reply(std::move(outcome));
        });
    return Request([alive]() { *alive = false; });
}

// "covers/sunset.jpg" -> "sunset"; the fake cover key is the sample cover name.
QString coverKey(const QJsonValue& path)
{
    return path.isString() ? QFileInfo(path.toString()).baseName() : QString();
}

QStringList strings(const QJsonValue& array)
{
    QStringList list;
    for (const QJsonValue& value : array.toArray())
        list << value.toString();
    return list;
}

// The sample catalog, read once and shared by the fakes.
struct Samples {
    QHash<QString, SongInfo> songs;
    QHash<QString, PlaylistPage> playlists;
    QHash<QString, AlbumPage> albums;
    QHash<QString, ArtistInfo> artists;
    // In catalog order.
    QStringList songOrder;
    QStringList playlistOrder;
    QStringList albumOrder;
    QStringList artistOrder;
    // By artist key, in catalog order.
    QHash<QString, QStringList> artistSongs;
    QHash<QString, QStringList> artistAlbums;

    [[nodiscard]] CollectionItem playlistItem(const QString& key) const
    {
        const PlaylistPage& page = playlists[key];
        return {page.info.key,
                collection::Playlist,
                page.info.title,
                page.info.creator,
                0,
                int(page.songKeys.size()),
                page.info.coverKey};
    }
    [[nodiscard]] CollectionItem albumItem(const QString& key) const
    {
        const AlbumPage& page = albums[key];
        QStringList names;
        for (const ArtistRef& artist : page.info.artists)
            names << artist.name;
        return {page.info.key,     collection::Album,
                page.info.title,   names.join(QStringLiteral(" / ")),
                page.info.year,    int(page.songKeys.size()),
                page.info.coverKey};
    }
    [[nodiscard]] CollectionItem artistItem(const QString& key) const
    {
        const ArtistInfo& info = artists[key];
        return {info.key, collection::Artist, info.name, {}, 0, info.albumCount, info.avatarKey};
    }
};

std::shared_ptr<const Samples> loadSamples()
{
    QFile file(kSamples + QStringLiteral("catalog.json"));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("fake catalog: %s is missing; link Cirrus.Samples", qPrintable(file.fileName()));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    auto samples = std::make_shared<Samples>();

    QHash<QString, QString> artistNames;
    for (const QJsonValue& artist : root.value(QLatin1StringView("artists")).toArray())
        artistNames.insert(artist[QLatin1StringView("id")].toString(),
                           artist[QLatin1StringView("name")].toString());
    QHash<QString, QJsonObject> albums;
    for (const QJsonValue& album : root.value(QLatin1StringView("albums")).toArray())
        albums.insert(album[QLatin1StringView("id")].toString(), album.toObject());
    for (const QJsonValue& artist : root.value(QLatin1StringView("artists")).toArray()) {
        const QString key = artist[QLatin1StringView("id")].toString();
        samples->artists.insert(key, ArtistInfo{.key = key, .name = artistNames.value(key)});
        samples->artistOrder << key;
    }

    for (const QJsonValue& value : root.value(QLatin1StringView("songs")).toArray()) {
        const QJsonObject song = value.toObject();
        SongInfo info;
        info.key = song.value(QLatin1StringView("id")).toString();
        info.title = song.value(QLatin1StringView("title")).toString();
        for (const QString& id : strings(song.value(QLatin1StringView("artistIds"))))
            info.artists << ArtistRef{id, artistNames.value(id)};
        info.albumKey = song.value(QLatin1StringView("albumId")).toString();
        const QJsonObject album = albums.value(info.albumKey);
        info.albumTitle = album.value(QLatin1StringView("title")).toString();
        info.coverKey = coverKey(album.value(QLatin1StringView("cover")));
        const QJsonValue duration = song.value(QLatin1StringView("durationMs"));
        info.durationMs = duration.isDouble() ? qint64(duration.toDouble()) : -1;
        info.unavailable = !song.value(QLatin1StringView("playable")).toBool(true);
        samples->songs.insert(info.key, info);
        samples->songOrder << info.key;
        for (const ArtistRef& artist : std::as_const(info.artists))
            samples->artistSongs[artist.key] << info.key;
    }

    for (const QJsonValue& value : root.value(QLatin1StringView("albums")).toArray()) {
        const QJsonObject album = value.toObject();
        AlbumPage page;
        page.info.key = album.value(QLatin1StringView("id")).toString();
        page.info.title = album.value(QLatin1StringView("title")).toString();
        for (const QString& id : strings(album.value(QLatin1StringView("artistIds")))) {
            page.info.artists << ArtistRef{id, artistNames.value(id)};
            samples->artistAlbums[id] << page.info.key;
        }
        page.info.year = album.value(QLatin1StringView("year")).toInt();
        page.info.coverKey = coverKey(album.value(QLatin1StringView("cover")));
        page.songKeys = strings(album.value(QLatin1StringView("songIds")));
        for (const QString& key : std::as_const(page.songKeys))
            page.songs << samples->songs.value(key);
        samples->albums.insert(page.info.key, page);
        samples->albumOrder << page.info.key;
    }

    // The samples have no artist pictures or bios: avatars stay empty (placeholder).
    for (ArtistInfo& artist : samples->artists) {
        artist.songCount = int(samples->artistSongs.value(artist.key).size());
        artist.albumCount = int(samples->artistAlbums.value(artist.key).size());
    }

    for (const QJsonValue& value : root.value(QLatin1StringView("playlists")).toArray()) {
        const QJsonObject playlist = value.toObject();
        PlaylistPage page;
        page.info.key = playlist.value(QLatin1StringView("id")).toString();
        page.info.title = playlist.value(QLatin1StringView("title")).toString();
        page.info.description = playlist.value(QLatin1StringView("description")).toString();
        page.info.creator = playlist.value(QLatin1StringView("creator")).toString();
        page.info.coverKey = coverKey(playlist.value(QLatin1StringView("cover")));
        page.songKeys = strings(playlist.value(QLatin1StringView("songIds")));
        for (const QString& key : std::as_const(page.songKeys))
            page.songs << samples->songs.value(key);
        samples->playlists.insert(page.info.key, page);
        samples->playlistOrder << page.info.key;
    }
    return samples;
}

class FakeCatalog : public QObject, public CatalogSource
{
  public:
    FakeCatalog(FakeControls* controls, std::shared_ptr<const Samples> samples)
        : m_controls(controls), m_samples(std::move(samples))
    {
    }

    Request playlist(const QString& key, Reply<PlaylistPage> reply) override
    {
        return replyLater<PlaylistPage>(this, m_controls, std::move(reply), [this, key]() {
            Outcome<PlaylistPage> outcome;
            const auto found = m_samples->playlists.constFind(key);
            if (found == m_samples->playlists.cend()) {
                outcome.failure = Failure{failure::Rejected, std::nullopt};
                return outcome;
            }
            PlaylistPage page = *found;
            if (m_controls->empty()) {
                page.songKeys.clear();
                page.songs.clear();
            }
            outcome.value = page;
            return outcome;
        });
    }

    Request album(const QString& key, Reply<AlbumPage> reply) override
    {
        return replyLater<AlbumPage>(this, m_controls, std::move(reply), [this, key]() {
            Outcome<AlbumPage> outcome;
            const auto found = m_samples->albums.constFind(key);
            if (found == m_samples->albums.cend()) {
                outcome.failure = Failure{failure::Rejected, std::nullopt};
                return outcome;
            }
            AlbumPage page = *found;
            if (m_controls->empty()) {
                page.songKeys.clear();
                page.songs.clear();
            }
            outcome.value = page;
            return outcome;
        });
    }

    Request artist(const QString& key, Reply<ArtistPage> reply) override
    {
        return replyLater<ArtistPage>(this, m_controls, std::move(reply), [this, key]() {
            Outcome<ArtistPage> outcome;
            const auto found = m_samples->artists.constFind(key);
            if (found == m_samples->artists.cend()) {
                outcome.failure = Failure{failure::Rejected, std::nullopt};
                return outcome;
            }
            ArtistPage page;
            page.info = *found;
            if (!m_controls->empty()) {
                for (const QString& song : m_samples->artistSongs.value(key).mid(0, kTopSongs))
                    page.topSongs << m_samples->songs.value(song);
                for (const QString& album : m_samples->artistAlbums.value(key))
                    page.albums << m_samples->albumItem(album);
                // Newest first.
                std::stable_sort(page.albums.begin(), page.albums.end(),
                                 [](const CollectionItem& a, const CollectionItem& b) {
                                     return a.year > b.year;
                                 });
            }
            outcome.value = page;
            return outcome;
        });
    }

  private:
    static constexpr int kTopSongs = 10;

    FakeControls* m_controls;
    std::shared_ptr<const Samples> m_samples;
};

// Sample covers by name. Not subject to the delay and failure knobs, which are for
// page states; covers always arrive on the next event loop turn.
class FakeImages : public QObject, public ImageSource
{
  public:
    Request image(const QString& key, Reply<QByteArray> reply) override
    {
        auto alive = std::make_shared<bool>(true);
        QTimer::singleShot(0, this, [alive, key, reply = std::move(reply)]() {
            if (!*alive)
                return;
            *alive = false;
            Outcome<QByteArray> outcome;
            QFile file(kSamples + QStringLiteral("covers/") + key + QStringLiteral(".jpg"));
            if (file.open(QIODevice::ReadOnly))
                outcome.value = file.readAll();
            else
                outcome.failure = Failure{failure::Rejected, std::nullopt};
            reply(std::move(outcome));
        });
        return Request([alive]() { *alive = false; });
    }
};

// Simulated playback on the monotonic clock (docs/ui/presentation.md §7): loading takes
// the controlled delay, songs end by themselves, the queue follows order and repeat,
// unavailable songs fail and so does everything while offline. Songs of unknown
// duration play for kUnknownLengthMs.
class FakePlayback : public QObject, public PlaybackSource
{
  public:
    FakePlayback(FakeControls* controls, std::shared_ptr<const Samples> samples)
        : m_controls(controls), m_samples(std::move(samples))
    {
        m_load.setSingleShot(true);
        m_end.setSingleShot(true);
        m_end.setTimerType(Qt::PreciseTimer);
        connect(&m_load, &QTimer::timeout, this, [this]() { finishLoading(); });
        connect(&m_end, &QTimer::timeout, this, [this]() { songEnded(); });
    }

    const PlaybackSnapshot& snapshot() const override { return m_state; }
    void setChangedHandler(std::function<void()> changed) override
    {
        m_changed = std::move(changed);
    }

    void playSongs(const QStringList& keys, int index) override
    {
        if (index < 0 || index >= keys.size())
            return;
        m_queue = keys;
        arrange(index);
        start();
    }

    void playPause() override
    {
        switch (m_state.status) {
        case playback::Playing:
            hold(position());
            break;
        case playback::Paused:
            play(m_state.positionMs);
            break;
        case playback::Ended:
            m_pos = 0;
            start();
            break;
        case playback::Failed:
            start();
            break;
        default:
            return;
        }
        notify();
    }

    void next() override
    {
        if (m_sequence.isEmpty())
            return;
        if (m_pos + 1 < m_sequence.size())
            ++m_pos;
        else if (m_state.repeat != playback::RepeatOff)
            m_pos = 0;
        else
            return;
        start();
    }

    void previous() override
    {
        if (m_sequence.isEmpty())
            return;
        const bool started =
            m_state.status == playback::Playing || m_state.status == playback::Paused;
        if (started && position() > kRestartWithinMs) {
            seek(0);
            return;
        }
        if (m_pos > 0)
            --m_pos;
        else if (m_state.repeat != playback::RepeatOff)
            m_pos = int(m_sequence.size()) - 1;
        start();
    }

    void seek(qint64 positionMs) override
    {
        if (!m_state.seekable)
            return;
        positionMs = std::clamp<qint64>(positionMs, 0, length());
        if (m_state.status == playback::Playing)
            play(positionMs);
        else
            hold(positionMs);
        notify();
    }

    void setVolume(double volume) override
    {
        m_state.volume = volume;
        notify();
    }

    void setOrder(playback::Order order) override
    {
        m_state.order = order;
        if (!m_sequence.isEmpty())
            arrange(m_sequence.at(m_pos));
        notify();
    }

    void setRepeat(playback::Repeat repeat) override
    {
        m_state.repeat = repeat;
        updateNavigation();
        notify();
    }

  private:
    static constexpr qint64 kUnknownLengthMs = 200'000;
    static constexpr qint64 kRestartWithinMs = 3'000;

    // Play order over the queue, starting at the queue entry `index`: in queue order,
    // or `index` followed by the rest shuffled.
    void arrange(int index)
    {
        m_sequence.resize(m_queue.size());
        std::iota(m_sequence.begin(), m_sequence.end(), 0);
        if (m_state.order == playback::Shuffle) {
            m_sequence.removeAt(index);
            std::shuffle(m_sequence.begin(), m_sequence.end(), *QRandomGenerator::global());
            m_sequence.prepend(index);
            m_pos = 0;
        } else {
            m_pos = index;
        }
        updateNavigation();
    }

    void start()
    {
        m_end.stop();
        const QString key = m_queue.at(m_sequence.at(m_pos));
        m_state.current = m_samples->songs.value(key, SongInfo{.key = key, .title = key});
        m_state.status = playback::Loading;
        m_state.failure.reset();
        m_state.positionMs = 0;
        m_state.positionAt = playback::now();
        m_state.rate = 0;
        m_state.durationMs = m_state.current.durationMs;
        m_state.seekable = false;
        updateNavigation();
        m_load.start(m_controls->nextDelay());
        notify();
    }

    void finishLoading()
    {
        std::optional<failure::Kind> failure;
        if (m_controls->offline())
            failure = failure::Network;
        else if (m_state.current.unavailable)
            failure = failure::Rejected;
        if (failure) {
            m_state.status = playback::Failed;
            m_state.failure = Failure{*failure, std::nullopt};
        } else {
            m_state.seekable = m_state.durationMs >= 0;
            play(0);
        }
        notify();
    }

    void songEnded()
    {
        if (m_state.repeat == playback::RepeatOne) {
            play(0);
            notify();
        } else if (m_pos + 1 < m_sequence.size() || m_state.repeat == playback::RepeatAll) {
            m_pos = (m_pos + 1) % int(m_sequence.size());
            start();
        } else {
            hold(length());
            m_state.status = playback::Ended;
            notify();
        }
    }

    void play(qint64 from)
    {
        m_state.status = playback::Playing;
        m_state.positionMs = from;
        m_state.positionAt = playback::now();
        m_state.rate = 1;
        m_end.start(int(std::max<qint64>(length() - from, 0)));
    }

    void hold(qint64 at)
    {
        m_end.stop();
        if (m_state.status == playback::Playing)
            m_state.status = playback::Paused;
        m_state.positionMs = at;
        m_state.positionAt = playback::now();
        m_state.rate = 0;
    }

    [[nodiscard]] qint64 length() const
    {
        return m_state.durationMs >= 0 ? m_state.durationMs : kUnknownLengthMs;
    }

    [[nodiscard]] qint64 position() const
    {
        const auto elapsed = qint64(double(playback::now() - m_state.positionAt) * m_state.rate);
        return std::min(m_state.positionMs + elapsed, length());
    }

    void updateNavigation()
    {
        m_state.canNext = m_pos + 1 < m_sequence.size() ||
                          (!m_sequence.isEmpty() && m_state.repeat != playback::RepeatOff);
        m_state.canPrevious = !m_sequence.isEmpty();
    }

    // Reports on the next event loop turn, once for any number of changes.
    void notify()
    {
        if (m_notifyPending)
            return;
        m_notifyPending = true;
        QMetaObject::invokeMethod(
            this,
            [this]() {
                m_notifyPending = false;
                if (m_changed)
                    m_changed();
            },
            Qt::QueuedConnection);
    }

    FakeControls* m_controls;
    std::shared_ptr<const Samples> m_samples;
    std::function<void()> m_changed;
    PlaybackSnapshot m_state;
    QStringList m_queue;
    QList<int> m_sequence; // queue indices in play order
    int m_pos = 0;         // into m_sequence
    QTimer m_load;
    QTimer m_end;
    bool m_notifyPending = false;
};
} // namespace

Sources makeFakeSources()
{
    Sources sources;
    sources.fakeControls = std::make_unique<FakeControls>();
    const std::shared_ptr<const Samples> samples = loadSamples();
    sources.catalog = std::make_unique<FakeCatalog>(sources.fakeControls.get(), samples);
    sources.images = std::make_unique<FakeImages>();
    sources.playback = std::make_unique<FakePlayback>(sources.fakeControls.get(), samples);
    return sources;
}

} // namespace cirrus::ui
