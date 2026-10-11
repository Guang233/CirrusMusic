#pragma once
#include "domain/playback/playback_observation_types.h"
#include <QByteArrayView>
#include <QJsonObject>
#include <QStringList>
#include <memory>
#include <variant>

namespace cirrus::infrastructure::ipc
{
enum class ObservationError {
    UnsupportedVersion,
    InvalidRequest,
    NotReady,
    StaleView,
    LyricsNotReady,
    ExportLimitExceeded,
    Busy,
    RateLimited
};
enum class ObservationMethod { Hello, Snapshot, Subscribe, Unsubscribe, LyricsSlice };
struct ObservationRequest {
    QString id;
    ObservationMethod method{};
    QStringList topics;
    domain::ObservationBinding binding;
    quint64 lyricsRevision = 0;
    int track = 0;
    qsizetype fromLine = 0, maxLines = 0;
};
struct RequestFailure {
    ObservationError code{};
    QString id; // validated correlation only, never an input echo
};
using DecodedRequest = std::variant<ObservationRequest, RequestFailure>;
class PreparedLyrics final
{
  public:
    [[nodiscard]] qsizetype normalizedBytes() const { return m_bytes; }

  private:
    friend class PlaybackObservationCodec;
    friend class PlaybackLyricsEncoder;
    struct Track {
        QJsonObject summary;
        QList<QByteArray> lines;
        QList<qsizetype> wordCounts;
    };
    domain::ObservationBinding m_binding;
    quint64 m_revision = 0;
    domain::SongId m_song;
    std::array<Track, 6> m_tracks;
    qsizetype m_bytes = 0;
};
struct DocumentEncoding {
    domain::ObservationContentStatus status{};
    std::shared_ptr<const PreparedLyrics> document;
};
// Caller-thread-confined encoding job. One step handles <=128 line/word records;
// a single text field is still bounded by TextUnits. Cancel by destroying the job.
// Partial bytes never escape. Production uses this off the media owner thread.
class PlaybackLyricsEncoder final
{
  public:
    explicit PlaybackLyricsEncoder(std::shared_ptr<const domain::PlaybackLyricDocument> document);
    ~PlaybackLyricsEncoder();
    PlaybackLyricsEncoder(const PlaybackLyricsEncoder&) = delete;
    PlaybackLyricsEncoder& operator=(const PlaybackLyricsEncoder&) = delete;
    void step(int records = 128);
    [[nodiscard]] bool done() const;
    [[nodiscard]] DocumentEncoding result() const;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
class PlaybackObservationCodec final
{
  public:
    static constexpr qsizetype RequestBytes = 4096, SnapshotBytes = 256 * 1024;
    static constexpr qsizetype FrameBytes = 1024 * 1024, DocumentBytes = 8 * 1024 * 1024;
    static constexpr qsizetype TextUnits = 1024 * 1024, CursorEntries = 32768;
    static const std::array<QString, 6>& trackNames();
    static bool validId(const QString& id);
    static DecodedRequest decode(QByteArrayView body);
    // Same <=4KiB UTF8/duplicate/depth/integer grammar gate for private discovery
    // and hello envelopes; NOT a general response/schema or RPC decoder.
    static std::optional<QJsonObject> strictLocalObject(QByteArrayView body);
    static DocumentEncoding prepare(const domain::PlaybackLyricDocument& document);
    static std::variant<QJsonObject, ObservationError>
    snapshot(const domain::PlaybackObservation& value, const DocumentEncoding& encoding = {});
    static std::variant<QByteArray, ObservationError>
    slice(const ObservationRequest& request, const domain::PlaybackObservation& current,
          const DocumentEncoding& encoding);
    static QJsonObject error(const QString& id, ObservationError code);
    static QByteArray frame(QByteArrayView body); // empty on zero or >1MiB; prefix not counted
};
// One bounded partial REQUEST at a time. Stops at one complete frame, caller consumes
// the remainder; no batch vector or input-chunk copy. Deadlines use supplied monotonic
// ms (transport responsibility), fixed from first byte, never extended by slow drip.
class ObservationRequestFramer final
{
  public:
    struct Result {
        qsizetype consumed = 0;
        std::optional<QByteArray> body;
        bool closed = false;
    };
    Result feed(QByteArrayView input, qint64 nowMs);
    void close();
    [[nodiscard]] qsizetype bufferedBytes() const { return m_prefix.size() + m_body.size(); }

  private:
    QByteArray m_prefix, m_body;
    quint32 m_length = 0;
    std::optional<qint64> m_started, m_lastNow;
    bool m_closed = false;
};
} // namespace cirrus::infrastructure::ipc
