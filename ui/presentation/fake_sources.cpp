#include "fake_sources.h"

#include "fake_controls.h"
#include "formatter.h"

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <numeric>
#include <random>

namespace cirrus::ui
{

namespace
{
// Embedded by the Cirrus.Samples module.
const QString kSamples = QStringLiteral(":/qt/qml/Cirrus/Samples/");

// Replies after the controlled delay, unless canceled or `context` is gone first.
// The failure to inject is decided when the request starts; requests that pages don't
// show as states (search suggestions, trending keywords) leave it for the next one.
template <class T>
Request replyLater(QObject* context, FakeControls* controls, Reply<T> reply,
                   std::function<Outcome<T>()> produce, bool injectFailures = true)
{
    auto alive = std::make_shared<bool>(true);
    const int failure = injectFailures ? controls->takeFailure() : -1;
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
        // Charts are playlists of their own, made from the sample songs.
        const struct {
            const char* key;
            const char* title;
            const char* update;
            const char* cover;
        } charts[] = {{"ch-soar", QT_TR_NOOP("飙升榜"), QT_TR_NOOP("每天更新"), "crimson"},
                      {"ch-new", QT_TR_NOOP("新歌榜"), QT_TR_NOOP("每天更新"), "ocean"},
                      {"ch-original", QT_TR_NOOP("原创榜"), QT_TR_NOOP("每周四更新"), "forest"},
                      {"ch-hot", QT_TR_NOOP("热歌榜"), QT_TR_NOOP("每周四更新"), "sunset"}};
        std::mt19937 random(7);
        for (const auto& chart : charts) {
            QStringList keys = m_samples->songOrder;
            std::shuffle(keys.begin(), keys.end(), random);
            keys = keys.mid(0, kChartSongs);
            PlaylistPage page;
            page.info = {QString::fromLatin1(chart.key), tr(chart.title), tr(chart.update),
                         tr("Cirrus 示例"), QString::fromLatin1(chart.cover)};
            page.songKeys = keys;
            for (const QString& key : std::as_const(keys))
                page.songs << m_samples->songs[key];
            m_charts << page;
        }
    }

    Request playlist(const QString& key, Reply<PlaylistPage> reply) override
    {
        return replyLater<PlaylistPage>(this, m_controls, std::move(reply), [this, key]() {
            Outcome<PlaylistPage> outcome;
            const auto chart =
                std::find_if(m_charts.cbegin(), m_charts.cend(),
                             [&](const PlaylistPage& c) { return c.info.key == key; });
            const auto found = m_samples->playlists.constFind(key);
            if (chart == m_charts.cend() && found == m_samples->playlists.cend()) {
                outcome.failure = Failure{failure::Rejected, std::nullopt};
                return outcome;
            }
            PlaylistPage page = chart != m_charts.cend() ? *chart : *found;
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

    Request suggestions(const QString& text, Reply<QStringList> reply) override
    {
        return replyLater<QStringList>(
            this, m_controls, std::move(reply),
            [this, text]() {
                QStringList found;
                const auto offer = [&](const QString& candidate) {
                    if (found.size() < kSuggestions && matches(candidate, text) &&
                        !found.contains(candidate))
                        found << candidate;
                };
                for (const QString& key : m_samples->songOrder)
                    offer(m_samples->songs[key].title);
                for (const QString& key : m_samples->artistOrder)
                    offer(m_samples->artists[key].name);
                for (const QString& key : m_samples->albumOrder)
                    offer(m_samples->albums[key].info.title);
                return Outcome<QStringList>{found};
            },
            false);
    }

    Request hotKeywords(Reply<QStringList> reply) override
    {
        return replyLater<QStringList>(
            this, m_controls, std::move(reply),
            [this]() {
                // A fixed mix of songs and artists from the samples.
                QStringList keywords;
                for (const char* key : {"so-001", "so-003", "so-031", "so-014", "so-017", "so-019"})
                    keywords << m_samples->songs[QLatin1StringView(key)].title;
                for (const char* key : {"ar-01", "ar-04"})
                    keywords << m_samples->artists[QLatin1StringView(key)].name;
                return Outcome<QStringList>{keywords};
            },
            false);
    }

    Request searchSongs(const QString& text, PageRequest page,
                        Reply<ResultPage<SongInfo>> reply) override
    {
        return replyLater<ResultPage<SongInfo>>(this, m_controls, std::move(reply), [=, this]() {
            QList<SongInfo> found;
            if (!m_controls->empty()) {
                for (const QString& key : m_samples->songOrder) {
                    const SongInfo& song = m_samples->songs[key];
                    bool hit = matches(song.title, text) || matches(song.albumTitle, text);
                    for (const ArtistRef& artist : song.artists)
                        hit = hit || matches(artist.name, text);
                    if (hit)
                        found << song;
                }
            }
            return Outcome<ResultPage<SongInfo>>{slice(found, page)};
        });
    }

    Request searchCollections(collection::Kind kind, const QString& text, PageRequest page,
                              Reply<ResultPage<CollectionItem>> reply) override
    {
        return replyLater<ResultPage<CollectionItem>>(
            this, m_controls, std::move(reply), [=, this]() {
                QList<CollectionItem> found;
                if (!m_controls->empty()) {
                    switch (kind) {
                    case collection::Album:
                        for (const QString& key : m_samples->albumOrder) {
                            const CollectionItem item = m_samples->albumItem(key);
                            if (matches(item.title, text) || matches(item.byline, text))
                                found << item;
                        }
                        break;
                    case collection::Artist:
                        for (const QString& key : m_samples->artistOrder)
                            if (matches(m_samples->artists[key].name, text))
                                found << m_samples->artistItem(key);
                        break;
                    case collection::Playlist:
                        for (const QString& key : m_samples->playlistOrder) {
                            const PlaylistInfo& info = m_samples->playlists[key].info;
                            if (matches(info.title, text) || matches(info.description, text) ||
                                matches(info.creator, text))
                                found << m_samples->playlistItem(key);
                        }
                        break;
                    }
                }
                return Outcome<ResultPage<CollectionItem>>{slice(found, page)};
            });
    }

    Request feed(Feed which, Reply<QList<FeedSection>> reply) override
    {
        return replyLater<QList<FeedSection>>(this, m_controls, std::move(reply), [=, this]() {
            QList<FeedSection> sections;
            if (m_controls->empty())
                return Outcome<QList<FeedSection>>{sections};
            const auto collections = [](const QString& title, const QList<CollectionItem>& items) {
                return FeedSection{feed::Collections, title, items, {}};
            };
            QList<CollectionItem> playlists;
            for (const QString& key : m_samples->playlistOrder)
                if (!m_samples->playlists[key].songKeys.isEmpty())
                    playlists << m_samples->playlistItem(key);
            if (which == feed::Home) {
                sections << FeedSection{feed::Shortcuts, {}, {}, {}};
                sections << collections(tr("推荐歌单"), playlists);
                FeedSection songs{feed::Songs, tr("猜你喜欢"), {}, {}};
                for (const char* key : {"so-005", "so-017", "so-031", "so-022", "so-009"})
                    songs.songs << m_samples->songs[QLatin1StringView(key)];
                sections << songs;
            } else {
                QList<CollectionItem> albums;
                for (const QString& key : m_samples->albumOrder)
                    albums << m_samples->albumItem(key);
                std::stable_sort(albums.begin(), albums.end(),
                                 [](const CollectionItem& a, const CollectionItem& b) {
                                     return a.year > b.year;
                                 });
                QList<CollectionItem> artists;
                for (const QString& key : m_samples->artistOrder)
                    artists << m_samples->artistItem(key);
                sections << collections(tr("新碟上架"), albums);
                sections << collections(tr("热门歌单"), playlists);
                sections << collections(tr("推荐歌手"), artists);
            }
            return Outcome<QList<FeedSection>>{sections};
        });
    }

    Request dailySongs(Reply<QList<SongInfo>> reply) override
    {
        return replyLater<QList<SongInfo>>(this, m_controls, std::move(reply), [this]() {
            Outcome<QList<SongInfo>> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            QList<SongInfo> songs;
            if (!m_controls->empty()) {
                // A different but stable pick each day.
                QStringList keys = m_samples->songOrder;
                std::mt19937 random(QDate::currentDate().toJulianDay());
                std::shuffle(keys.begin(), keys.end(), random);
                for (const QString& key : keys.mid(0, kDailySongs))
                    songs << m_samples->songs[key];
                // Reasons for some songs, worded like the service's.
                for (qsizetype i = 0; i < songs.size(); i += 2) {
                    SongInfo& song = songs[i];
                    if (song.artists.isEmpty())
                        continue;
                    song.reason = i % 4 == 0
                                      ? tr("根据你喜欢的%1推荐").arg(song.artists.first().name)
                                      : tr("与你最近常听的歌风格相似");
                }
            }
            outcome.value = songs;
            return outcome;
        });
    }

    Request shelf(feed::Shelf which, Reply<QList<CollectionItem>> reply) override
    {
        return replyLater<QList<CollectionItem>>(this, m_controls, std::move(reply), [=, this]() {
            Outcome<QList<CollectionItem>> outcome;
            if (which == feed::DailyPlaylists && !m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            QList<CollectionItem> items;
            if (!m_controls->empty()) {
                if (which == feed::PopularArtists) {
                    for (const QString& key : m_samples->artistOrder)
                        items << m_samples->artistItem(key);
                } else if (which == feed::NewAlbums) {
                    for (const QString& key : m_samples->albumOrder)
                        items << m_samples->albumItem(key);
                    std::stable_sort(items.begin(), items.end(),
                                     [](const CollectionItem& a, const CollectionItem& b) {
                                         return a.year > b.year;
                                     });
                } else {
                    QStringList keys;
                    for (const QString& key : m_samples->playlistOrder)
                        if (!m_samples->playlists[key].songKeys.isEmpty())
                            keys << key;
                    if (which == feed::DailyPlaylists) {
                        // A different but stable order each day.
                        std::mt19937 random(QDate::currentDate().toJulianDay() + 1);
                        std::shuffle(keys.begin(), keys.end(), random);
                    }
                    for (const QString& key : std::as_const(keys))
                        items << m_samples->playlistItem(key);
                }
            }
            outcome.value = items;
            return outcome;
        });
    }

    Request charts(Reply<QList<ChartInfo>> reply) override
    {
        return replyLater<QList<ChartInfo>>(this, m_controls, std::move(reply), [this]() {
            QList<ChartInfo> charts;
            if (!m_controls->empty()) {
                for (const PlaylistPage& page : std::as_const(m_charts)) {
                    ChartInfo chart{page.info.key,
                                    page.info.title,
                                    page.info.description,
                                    page.info.coverKey,
                                    {}};
                    for (const SongInfo& song : page.songs.mid(0, kChartPreviews))
                        chart.previews
                            << ChartPreview{song.title, Formatter::artists(song.artists)};
                    charts << chart;
                }
            }
            return Outcome<QList<ChartInfo>>{charts};
        });
    }

  private:
    static constexpr int kTopSongs = 10;
    static constexpr int kChartSongs = 20;
    static constexpr int kChartPreviews = 3;
    static constexpr int kSuggestions = 8;
    static constexpr int kDailySongs = 15;

    static bool matches(const QString& candidate, const QString& text)
    {
        return candidate.contains(text, Qt::CaseInsensitive);
    }

    template <class T> static ResultPage<T> slice(const QList<T>& all, PageRequest page)
    {
        ResultPage<T> result;
        result.items = all.mid(page.offset, page.limit);
        result.hasMore = page.offset + page.limit < all.size();
        return result;
    }

    FakeControls* m_controls;
    std::shared_ptr<const Samples> m_samples;
    QList<PlaylistPage> m_charts;
};

// The signed-in account's library: a fixed selection of the samples.
class FakeLibrary : public QObject, public LibrarySource
{
  public:
    FakeLibrary(FakeControls* controls, std::shared_ptr<const Samples> samples)
        : m_controls(controls), m_samples(std::move(samples))
    {
    }

    Request library(Reply<LibraryPage> reply) override
    {
        return replyLater<LibraryPage>(this, m_controls, std::move(reply), [this]() {
            Outcome<LibraryPage> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            LibraryPage page;
            if (!m_controls->empty()) {
                const auto songs = [this](std::initializer_list<const char*> keys) {
                    QList<SongInfo> list;
                    for (const char* key : keys)
                        list << m_samples->songs[QLatin1StringView(key)];
                    return list;
                };
                page.likedSongs = songs({"so-001", "so-005", "so-011", "so-017", "so-023", "so-031",
                                         "so-034", "so-008"});
                for (const QString& key : recentKeys())
                    page.recentSongs << m_samples->songs[key];
                for (const char* key : {"pl-01", "pl-05"})
                    page.createdPlaylists << m_samples->playlistItem(QLatin1StringView(key));
                for (const char* key : {"pl-02", "pl-03", "pl-04"})
                    page.savedPlaylists << m_samples->playlistItem(QLatin1StringView(key));
                for (const char* key : {"al-01", "al-03", "al-06", "al-13"})
                    page.albums << m_samples->albumItem(QLatin1StringView(key));
                for (const char* key : {"ar-01", "ar-04", "ar-11"})
                    page.artists << m_samples->artistItem(QLatin1StringView(key));
            }
            outcome.value = page;
            return outcome;
        });
    }

    Request recentSongs(int limit, Reply<QList<SongInfo>> reply) override
    {
        return replyLater<QList<SongInfo>>(this, m_controls, std::move(reply), [=, this]() {
            Outcome<QList<SongInfo>> outcome;
            if (!m_controls->signedIn()) {
                outcome.failure = Failure{failure::SignedOut, std::nullopt};
                return outcome;
            }
            QList<SongInfo> songs;
            if (!m_controls->empty())
                for (const QString& key : recentKeys().mid(0, limit))
                    songs << m_samples->songs[key];
            outcome.value = songs;
            return outcome;
        });
    }

  private:
    static QStringList recentKeys()
    {
        return {"so-031", "so-001", "so-019", "so-026", "so-012", "so-007", "so-022", "so-014"};
    }

    FakeControls* m_controls;
    std::shared_ptr<const Samples> m_samples;
};

// The account follows App.debug.account. QR sign-in plays itself out: scanned after
// 4 s, confirming 2 s later, signed in 1 s after that; an unscanned code expires
// after 60 s. The code is a made-up pattern, not a scannable QR code.
class FakeSession : public QObject, public SessionSource
{
  public:
    explicit FakeSession(FakeControls* controls) : m_controls(controls)
    {
        m_step.setSingleShot(true);
        connect(&m_step, &QTimer::timeout, this, [this]() { advance(); });
        connect(controls, &FakeControls::accountChanged, this, [this]() {
            m_step.stop();
            m_state.qrPhase = account::NoQr;
            m_state.qrImage = {};
            applyAccount();
        });
        applyAccount();
    }

    const SessionSnapshot& snapshot() const override { return m_state; }
    void setChangedHandler(std::function<void()> changed) override
    {
        m_changed = std::move(changed);
    }

    void beginQrLogin(bool) override
    {
        if (m_state.status == account::SignedIn)
            return;
        m_state.status = account::SigningIn;
        newCode();
    }

    void refreshQr() override
    {
        if (m_state.status == account::SigningIn)
            newCode();
    }

    void cancelLogin() override
    {
        if (m_state.status != account::SigningIn)
            return;
        m_step.stop();
        m_state.qrPhase = account::NoQr;
        m_state.qrImage = {};
        applyAccount();
    }

    void signOut() override { m_controls->setAccount(account::Guest); }

  private:
    void applyAccount()
    {
        const bool signedIn = m_controls->account() == account::SignedIn;
        m_state.status = account::Status(m_controls->account());
        m_state.accountKey = signedIn ? QStringLiteral("fake-account") : QString();
        m_state.nickname = signedIn ? QStringLiteral("雾港听众") : QString();
        m_state.avatarKey = signedIn ? QStringLiteral("pastel") : QString();
        notify();
    }

    void newCode()
    {
        m_state.qrPhase = account::WaitingForScan;
        m_state.qrImage = pattern(++m_state.qrSerial);
        m_step.start(kScanMs);
        notify();
    }

    void advance()
    {
        switch (m_state.qrPhase) {
        case account::WaitingForScan:
            m_state.qrPhase = account::WaitingForConfirmation;
            m_step.start(kConfirmMs);
            break;
        case account::WaitingForConfirmation:
            m_state.qrPhase = account::Confirming;
            m_step.start(kSignInMs);
            break;
        case account::Confirming:
            m_state.qrPhase = account::NoQr;
            m_state.qrImage = {};
            // applyAccount() follows through accountChanged.
            m_controls->setAccount(account::SignedIn);
            return;
        default:
            return;
        }
        notify();
    }

    // Finder squares in three corners and seeded noise: looks like a code, scans as
    // nothing.
    static QImage pattern(int seed)
    {
        constexpr int modules = 29;
        QImage image(modules + 2, modules + 2, QImage::Format_Mono);
        image.fill(0);
        QRandomGenerator random{quint32(seed)};
        const auto finder = [&](int fx, int fy, int x, int y) {
            const int dx = x - fx, dy = y - fy;
            if (dx < 0 || dy < 0 || dx > 6 || dy > 6)
                return -1;
            const int ring = std::min({dx, dy, 6 - dx, 6 - dy});
            return ring == 1 ? 0 : 1;
        };
        for (int y = 0; y < modules; ++y) {
            for (int x = 0; x < modules; ++x) {
                int on = -1;
                for (const auto& [fx, fy] : {std::pair{0, 0}, {modules - 7, 0}, {0, modules - 7}})
                    on = std::max(on, finder(fx, fy, x, y));
                if (on < 0)
                    on = random.bounded(2);
                image.setPixel(x + 1, y + 1, on ? 1 : 0);
            }
        }
        // Index 0 is light (the one-module quiet zone from fill(0)), 1 is dark.
        image.setColor(0, qRgb(255, 255, 255));
        image.setColor(1, qRgb(0, 0, 0));
        return image.convertToFormat(QImage::Format_RGB32);
    }

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

    static constexpr int kScanMs = 4000;
    static constexpr int kConfirmMs = 2000;
    static constexpr int kSignInMs = 1000;

    FakeControls* m_controls;
    std::function<void()> m_changed;
    SessionSnapshot m_state;
    QTimer m_step;
    bool m_notifyPending = false;
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
        m_state.mode = playback::Queue;
        m_queue = keys;
        arrange(index);
        start();
    }

    void startFm() override
    {
        m_state.mode = playback::PersonalFm;
        m_queue.clear();
        m_sequence.clear();
        m_pos = 0;
        extendFm();
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
        if (m_state.mode == playback::PersonalFm) {
            ++m_pos;
            extendFm();
            start();
            return;
        }
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
        if (m_sequence.isEmpty() || m_state.mode == playback::PersonalFm)
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
        if (m_state.mode == playback::PersonalFm) {
            next();
            return;
        }
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
        if (m_state.mode == playback::PersonalFm) {
            // FM only goes forward.
            m_state.canNext = true;
            m_state.canPrevious = false;
            return;
        }
        m_state.canNext = m_pos + 1 < m_sequence.size() ||
                          (!m_sequence.isEmpty() && m_state.repeat != playback::RepeatOff);
        m_state.canPrevious = !m_sequence.isEmpty();
    }

    // Personal FM: makes sure a song waits at m_pos, picked at random from the
    // playable samples (not the one just played).
    void extendFm()
    {
        while (m_pos >= m_sequence.size()) {
            QStringList playable;
            for (const QString& key : m_samples->songOrder)
                if (!m_samples->songs[key].unavailable &&
                    (m_queue.isEmpty() || key != m_queue.last()))
                    playable << key;
            m_queue << playable.at(QRandomGenerator::global()->bounded(int(playable.size())));
            m_sequence << int(m_queue.size()) - 1;
        }
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
    sources.session = std::make_unique<FakeSession>(sources.fakeControls.get());
    sources.library = std::make_unique<FakeLibrary>(sources.fakeControls.get(), samples);
    return sources;
}

} // namespace cirrus::ui
