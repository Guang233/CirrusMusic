#include "fake_sources.h"

#include "fake_controls.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

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

class FakeCatalog : public QObject, public CatalogSource
{
  public:
    explicit FakeCatalog(FakeControls* controls) : m_controls(controls)
    {
        QFile file(kSamples + QStringLiteral("catalog.json"));
        if (!file.open(QIODevice::ReadOnly))
            qFatal("fake catalog: %s is missing; link Cirrus.Samples", qPrintable(file.fileName()));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

        QHash<QString, QString> artistNames;
        for (const QJsonValue& artist : root.value(QLatin1StringView("artists")).toArray())
            artistNames.insert(artist[QLatin1StringView("id")].toString(),
                               artist[QLatin1StringView("name")].toString());
        QHash<QString, QJsonObject> albums;
        for (const QJsonValue& album : root.value(QLatin1StringView("albums")).toArray())
            albums.insert(album[QLatin1StringView("id")].toString(), album.toObject());

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
            m_songs.insert(info.key, info);
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
                page.songs << m_songs.value(key);
            m_playlists.insert(page.info.key, page);
        }
    }

    Request playlist(const QString& key, Reply<PlaylistPage> reply) override
    {
        return replyLater<PlaylistPage>(this, m_controls, std::move(reply), [this, key]() {
            Outcome<PlaylistPage> outcome;
            const auto found = m_playlists.constFind(key);
            if (found == m_playlists.cend()) {
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

  private:
    FakeControls* m_controls;
    QHash<QString, SongInfo> m_songs;
    QHash<QString, PlaylistPage> m_playlists;
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
} // namespace

Sources makeFakeSources()
{
    Sources sources;
    sources.fakeControls = std::make_unique<FakeControls>();
    sources.catalog = std::make_unique<FakeCatalog>(sources.fakeControls.get());
    sources.images = std::make_unique<FakeImages>();
    return sources;
}

} // namespace cirrus::ui
