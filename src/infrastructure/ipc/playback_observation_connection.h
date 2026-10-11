#pragma once
#include "domain/playback/playback_observation_types.h"
#include <QByteArrayView>
#include <QMutex>
#include <atomic>
#include <memory>

namespace cirrus::infrastructure::ipc
{
// One producer (owner thread), one I/O consumer. Values are coherent historical
// copies. Producer MUST retire synchronously on private session/policy barriers,
// including barriers before the first published sample. No private identity on wire.
// Source documents must be immutable bounded Core projections, not provider payloads.
class ObservationMailbox final
{
  public:
    struct Value {
        domain::PlaybackObservation snapshot;
        std::shared_ptr<const domain::PlaybackLyricDocument> document;
        quint64 fence = 0;
    };
    bool publish(domain::PlaybackObservation snapshot,
                 std::shared_ptr<const domain::PlaybackLyricDocument> document = {});
    void retire(const domain::ObservationBinding& previous);
    // Explicit owner-only private-provenance decision, NOT driven by historical samples.
    // publish cannot change epoch. beginView must only use the newly validated owner view.
    bool beginView(const domain::ObservationBinding& current);
    std::optional<Value> take(); // clears the sole wake; no per-update queued payloads
    [[nodiscard]] bool wakePending() const;
    [[nodiscard]] quint64 fence() const { return m_fence.load(std::memory_order_acquire); }

  private:
    bool advance();
    mutable QMutex m_mutex;
    std::optional<Value> m_latest;
    domain::ObservationBinding m_context;
    std::atomic<quint64> m_fence{1};
    QUuid m_sequenceInstance;
    std::optional<quint64> m_lastSequence;
    bool m_wake = false, m_retired = false, m_exhausted = false;
};

// Narrow fake/native transport seam, NOT a socket or peer-credential verifier.
// Implementations must be nonblocking; callbacks may publish/retire the mailbox,
// but MUST NOT reenter the engine. Sink lives until close/engine destruction.
// pendingBytes includes all bytes already accepted but not drained by the transport.
class IObservationWriteSink
{
  public:
    virtual ~IObservationWriteSink() = default;
    virtual qint64 pendingBytes() const = 0;
    // Optional monotonic proof of actual transport drain (never enqueue). Useful
    // for conservative native debt accounting; default sinks use pending decrease.
    virtual quint64 drainRevision() const { return 0; }
    virtual qint64 write(QByteArrayView bytes) = 0;
    virtual void closeAndDiscard() = 0;
};
enum class ObservationClose {
    Closed,
    Protocol,
    Timeout,
    RateLimit,
    Backpressure,
    Privacy,
    StalePartial,
    Transport,
    Clock
};
// Single I/O-thread logical protocol only: no listener, native APIs, QObject/timer,
// automatic queries, player or authoritative clock. Caller supplies monotonic ms,
// <=16KiB total receive buffers, bounded wakes and nonblocking transport progress.
class PlaybackObservationConnections final
{
  public:
    static constexpr qsizetype ClientBytes = 2 * 1024 * 1024;
    explicit PlaybackObservationConnections(std::shared_ptr<ObservationMailbox> source);
    ~PlaybackObservationConnections();
    PlaybackObservationConnections(const PlaybackObservationConnections&) = delete;
    PlaybackObservationConnections& operator=(const PlaybackObservationConnections&) = delete;
    quint64 open(IObservationWriteSink& sink, qint64 nowMs); // O3 verifies peers BEFORE this
    qsizetype receive(quint64 client, QByteArrayView bytes, qint64 nowMs);
    void pump(qint64 nowMs); // <=1 request/client and <=128 encoder records per turn
    void write(quint64 client, qint64 nowMs); // <=64KiB nonblocking admission per call
    void close(quint64 client);
    void close();
    [[nodiscard]] bool isOpen(quint64 client) const;
    [[nodiscard]] int clientCount() const;
    [[nodiscard]] int pendingRequests(quint64 client) const;
    [[nodiscard]] qsizetype applicationBytes(quint64 client) const;
    // Partial frame + conservative 1KiB per pending typed request/id. Container
    // capacity/transient JSON parse memory are separate from receive buffers.
    [[nodiscard]] qsizetype inputBytes(quint64 client) const;
    [[nodiscard]] ObservationClose lastClose() const;
    [[nodiscard]] bool encodingPending() const;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace cirrus::infrastructure::ipc
