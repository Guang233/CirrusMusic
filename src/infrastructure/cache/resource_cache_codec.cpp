#include "resource_cache_codec.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>

namespace cirrus::cache
{
using namespace domain;
bool validCoverUrl(const QUrl& url)
{
    const auto host = url.host();
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           (host == QStringLiteral("p1.music.126.net") ||
            host == QStringLiteral("p2.music.126.net") ||
            host == QStringLiteral("p3.music.126.net") ||
            host == QStringLiteral("p4.music.126.net")) &&
           url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
           (url.port() == -1 || url.port() == 443) && !url.path().isEmpty() &&
           url.toEncoded().size() <= 2048;
}
bool validCoverSize(int size) { return size == 128 || size == 256 || size == 512 || size == 1024; }
QString coverCacheKey(const QUrl& url, int size)
{
    if (!validCoverUrl(url) || !validCoverSize(size))
        return {};
    const auto input = QByteArrayLiteral("cover-v1\n") + url.toEncoded(QUrl::FullyEncoded) + '\n' +
                       QByteArray::number(size);
    return QString::fromLatin1(QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}
namespace
{
constexpr qint64 TimeLimit = 7LL * 24 * 60 * 60 * 1000;
class Codec
{
  public:
    Codec(QByteArray& bytes, bool reading, qsizetype limit)
        : m_bytes(bytes), m_buffer(&bytes), m_stream(&m_buffer), m_reading(reading), m_limit(limit)
    {
        m_buffer.open(reading ? QIODevice::ReadOnly : QIODevice::WriteOnly);
        m_stream.setVersion(QDataStream::Qt_6_0);
        m_stream.setByteOrder(QDataStream::BigEndian);
    }
    bool ok() const
    {
        return m_ok && m_stream.status() == QDataStream::Ok && m_bytes.size() <= m_limit;
    }
    bool complete() const { return ok() && (!m_reading || m_buffer.atEnd()); }
    void check(bool condition) { m_ok = m_ok && condition; }
    void number(qint64& value, qint64 minimum, qint64 maximum)
    {
        if (!ok())
            return;
        if (m_reading)
            m_stream >> value;
        else
            m_stream << value;
        check(value >= minimum && value <= maximum);
    }
    void flag(bool& value)
    {
        qint64 v = value ? 1 : 0;
        number(v, 0, 1);
        value = v != 0;
    }
    void text(QString& value, qsizetype maximum = 4096)
    {
        if (!ok())
            return;
        QByteArray utf8;
        if (!m_reading) {
            check(value.size() <= maximum && value.isValidUtf16() && !value.contains(QChar::Null));
            if (!ok())
                return;
            utf8 = value.toUtf8();
        }
        qint64 length = utf8.size();
        number(length, 0, maximum * 4);
        if (!ok())
            return;
        if (m_reading) {
            check(length <= m_buffer.bytesAvailable());
            if (!ok())
                return;
            utf8.resize(length);
            check(m_stream.readRawData(utf8.data(), length) == length);
            value = QString::fromUtf8(utf8);
            check(value.toUtf8() == utf8 && value.size() <= maximum &&
                  !value.contains(QChar::Null));
        } else {
            check(length <= m_limit - m_bytes.size());
            if (ok())
                check(m_stream.writeRawData(utf8.constData(), length) == length);
        }
    }
    template <typename T, typename F> void optional(std::optional<T>& value, F transfer)
    {
        bool present = value.has_value();
        flag(present);
        if (!ok())
            return;
        if (!present) {
            value.reset();
            return;
        }
        if (m_reading)
            value.emplace();
        transfer(*value);
    }
    template <typename T, typename F> void list(QList<T>& values, qint64 maximum, F transfer)
    {
        qint64 count = values.size();
        number(count, 0, maximum);
        if (!ok())
            return;
        if (m_reading)
            values.clear();
        for (qint64 i = 0; i < count && ok(); ++i) {
            if (m_reading) {
                T value{};
                transfer(value);
                values.append(std::move(value));
            } else
                transfer(values[i]);
        }
    }
    void id(QString& value)
    {
        text(value, 19);
        check(validMusicId(value));
    }
    void song(Song& value)
    {
        id(value.id.value);
        text(value.title);
        list(value.artists, 100, [this](ArtistCredit& artist) {
            optional(artist.id, [this](ArtistId& v) { id(v.value); });
            text(artist.name);
        });
        optional(value.album, [this](AlbumReference& album) {
            optional(album.id, [this](AlbumId& v) { id(v.value); });
            text(album.title);
            auto url = album.cover.toString(QUrl::FullyEncoded);
            text(url, 2048);
            album.cover = QUrl(url, QUrl::StrictMode);
            check(url.isEmpty() || validCoverUrl(album.cover));
        });
        optional(value.durationMs, [this](qint64& v) { number(v, 0, TimeLimit); });
        const auto strings = [this](QStringList& v) {
            list(v, 100, [this](QString& textValue) { text(textValue); });
        };
        optional(value.aliases, strings);
        optional(value.translatedTitles, strings);
        optional(value.publishedAtMs,
                 [this](qint64& v) { number(v, -9007199254740991LL, 9007199254740991LL); });
    }
    void track(LyricTrack& value)
    {
        qint64 availability = static_cast<int>(value.availability),
               timing = static_cast<int>(value.timing);
        number(availability, 0, 3);
        number(timing, 0, 2);
        value.availability = static_cast<LyricAvailability>(availability);
        value.timing = static_cast<LyricTiming>(timing);
        number(value.providerOffsetMs, -TimeLimit, TimeLimit);
        qint64 malformed = value.malformedLines;
        number(malformed, 0, 10000);
        value.malformedLines = static_cast<int>(malformed);
        qsizetype characters = 0, words = 0;
        const auto lyricText = [this, &characters](QString& v) {
            text(v, 1024 * 1024);
            characters += v.size();
            check(characters <= 1024 * 1024);
        };
        list(value.credits, 100, lyricText);
        list(value.lines, 10000, [this, &words, &lyricText](LyricLine& line) {
            optional(line.startMs, [this](qint64& v) { number(v, 0, TimeLimit); });
            optional(line.endMs, [this](qint64& v) { number(v, 0, TimeLimit); });
            check(!line.startMs || !line.endMs || *line.endMs >= *line.startMs);
            lyricText(line.text);
            list(line.words, 30000 - words, [this, &words, &lyricText](LyricWord& word) {
                ++words;
                number(word.startMs, 0, TimeLimit);
                number(word.durationMs, 0, TimeLimit);
                check(word.durationMs <= TimeLimit - word.startMs);
                lyricText(word.text);
            });
        });
    }
    void lyrics(SongLyrics& value)
    {
        id(value.song.value);
        for (auto* versions : {&value.original, &value.translation, &value.romanization}) {
            track(versions->standard);
            track(versions->enhanced);
        }
        optional(value.providerNoLyric, [this](bool& v) { flag(v); });
        optional(value.instrumental, [this](bool& v) { flag(v); });
    }

  private:
    QByteArray& m_bytes;
    QBuffer m_buffer;
    QDataStream m_stream;
    bool m_reading, m_ok = true;
    qsizetype m_limit;
};
} // namespace
QByteArray encodeCacheValue(const CacheValue& value, CacheKind kind)
{
    if (kind == CacheKind::Cover)
        return {}; // cover rows come only from CoverFetcher, after its checks
    QByteArray bytes;
    Codec codec(bytes, false, maximumCachePayload(kind));
    qint64 version = 1, tag = static_cast<int>(kind);
    codec.number(version, 1, 1);
    codec.number(tag, 0, 1);
    if (kind == CacheKind::Song && std::holds_alternative<Song>(value)) {
        auto song = std::get<Song>(value);
        codec.song(song);
    } else if (kind == CacheKind::Lyrics && std::holds_alternative<SongLyrics>(value)) {
        auto lyrics = std::get<SongLyrics>(value);
        codec.lyrics(lyrics);
    } else
        return {};
    return codec.complete() ? bytes : QByteArray{};
}
std::optional<CacheValue> decodeCacheValue(QByteArray bytes, CacheKind kind)
{
    if (bytes.isEmpty() || bytes.size() > maximumCachePayload(kind))
        return std::nullopt;
    if (kind == CacheKind::Cover)
        return CacheValue(std::move(bytes));
    Codec codec(bytes, true, maximumCachePayload(kind));
    qint64 version = 0, tag = -1;
    codec.number(version, 1, 1);
    codec.number(tag, static_cast<int>(kind), static_cast<int>(kind));
    CacheValue result;
    if (kind == CacheKind::Song) {
        Song value;
        codec.song(value);
        result = std::move(value);
    } else if (kind == CacheKind::Lyrics) {
        SongLyrics value;
        codec.lyrics(value);
        result = std::move(value);
    } else
        return std::nullopt;
    return codec.complete() ? std::optional<CacheValue>(std::move(result)) : std::nullopt;
}
} // namespace cirrus::cache
