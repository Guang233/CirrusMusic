#include "playback_observation_connection.h"
#include "playback_observation_codec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSet>
#include <algorithm>
#include <deque>
#include <limits>

namespace cirrus::infrastructure::ipc
{
using namespace domain;
using Codec = PlaybackObservationCodec;
bool ObservationMailbox::advance()
{
    const auto n = m_fence.load(std::memory_order_relaxed);
    if (n == std::numeric_limits<quint64>::max()) {
        m_exhausted = true;
        return false;
    }
    m_fence.store(n + 1, std::memory_order_release);
    return true;
}
bool ObservationMailbox::publish(PlaybackObservation snapshot,
                                 std::shared_ptr<const PlaybackLyricDocument> document)
{
    QMutexLocker lock(&m_mutex);
    if (m_exhausted || snapshot.binding.instanceId.isNull() || snapshot.binding.viewEpoch.isNull())
        return false;
    if (m_context.instanceId.isNull() && !m_retired)
        m_context = snapshot.binding; // Only the first view can be established implicitly.
    if (m_retired || snapshot.binding.instanceId != m_context.instanceId ||
        snapshot.binding.viewEpoch != m_context.viewEpoch ||
        (m_sequenceInstance == snapshot.binding.instanceId && m_lastSequence &&
         snapshot.sequence <= *m_lastSequence))
        return false;
    m_sequenceInstance = snapshot.binding.instanceId;
    m_lastSequence = snapshot.sequence;
    m_latest =
        Value{std::move(snapshot), std::move(document), m_fence.load(std::memory_order_relaxed)};
    m_wake = true;
    return true;
}
void ObservationMailbox::retire(const ObservationBinding& previous)
{
    QMutexLocker lock(&m_mutex);
    advance();
    if (m_context.instanceId.isNull())
        m_context = previous;
    m_retired = true;
    m_latest.reset();
    m_wake = true;
}
bool ObservationMailbox::beginView(const ObservationBinding& current)
{
    QMutexLocker lock(&m_mutex);
    if (m_exhausted || current.instanceId.isNull() || current.viewEpoch.isNull() ||
        (m_context.instanceId == current.instanceId && m_context.viewEpoch == current.viewEpoch))
        return false;
    if (!advance())
        return false;
    m_context = current;
    m_latest.reset();
    m_retired = false;
    m_wake = true;
    return true;
}
std::optional<ObservationMailbox::Value> ObservationMailbox::take()
{
    QMutexLocker lock(&m_mutex);
    m_wake = false;
    return m_latest;
}
bool ObservationMailbox::wakePending() const
{
    QMutexLocker lock(&m_mutex);
    return m_wake;
}
namespace
{
QString idOf(const DecodedRequest& r)
{
    return std::visit([](const auto& x) { return x.id; }, r);
}
QJsonObject response(const QString& id)
{
    return {{"v", 1}, {"kind", "response"}, {"id", id}, {"ok", true}};
}
QByteArray bytes(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
int topicBit(const QString& topic) { return topic == "snapshot" ? 1 : topic == "lyrics" ? 2 : 4; }
int changedTopics(const QJsonObject& a, const QJsonObject& b)
{
    if (a == b)
        return 0;
    int mask = 1;
    if (a["availability"] != b["availability"] || a["instanceId"] != b["instanceId"] ||
        a["viewEpoch"] != b["viewEpoch"] || a["trackId"] != b["trackId"] ||
        a["lyricsRevision"] != b["lyricsRevision"])
        return 7;
    auto la = a["lyrics"].toObject(), lb = b["lyrics"].toObject();
    auto ta = la.take("tracks").toObject(), tb = lb.take("tracks").toObject();
    const auto ca = la.take("cursorExportStatus"), cb = lb.take("cursorExportStatus");
    if (la != lb)
        mask |= 2;
    if (ca != cb || la["userOffsetMs"] != lb["userOffsetMs"])
        mask |= 4;
    for (const auto& key : Codec::trackNames()) {
        const auto x = ta[key].toObject(), y = tb[key].toObject();
        if (x["summary"] != y["summary"])
            mask |= 2;
        if (x["cursor"] != y["cursor"])
            mask |= 4;
    }
    return mask;
}
} // namespace
class PlaybackObservationConnections::Impl
{
  public:
    struct Frame {
        QByteArray data;
        QString id; // empty only for events
        ObservationBinding binding;
        quint64 revision = 0;
        bool bound = false;
        qsizetype offset = 0;
    };
    struct Client {
        Client() {} // Explicit ctor: don't cache nested aggregate traits before Impl is complete.
        quint64 id = 0, lastDrain = 0;
        IObservationWriteSink* sink = nullptr;
        qint64 born = 0, rateAt = 0, lastQueued = 0;
        int tokens = 16000, topics = 0, dirty = 0;
        bool helloSeen = false, helloDone = false;
        std::optional<qint64> slowSince;
        ObservationRequestFramer framer;
        QSet<QString> ids;
        std::deque<DecodedRequest> requests;
        std::deque<Frame> replies;
        std::optional<Frame> active;
    };
    explicit Impl(std::shared_ptr<ObservationMailbox> input) : source(std::move(input)) {}
    Client* find(quint64 id)
    {
        for (auto& c : clients)
            if (c && c->id == id)
                return &*c;
        return nullptr;
    }
    const Client* find(quint64 id) const
    {
        for (const auto& c : clients)
            if (c && c->id == id)
                return &*c;
        return nullptr;
    }
    qsizetype appBytes(const Client& c) const
    {
        // Retained committed prefixes still occupy the active QByteArray until completion.
        qsizetype n = c.active ? c.active->data.size() : 0;
        for (const auto& f : c.replies)
            n += f.data.size();
        return n;
    }
    void drop(quint64 id, ObservationClose why)
    {
        for (auto& c : clients)
            if (c && c->id == id) {
                auto* sink = c->sink;
                c.reset();
                reason = why;
                sink->closeAndDiscard();
                return;
            }
    }
    void dropAll(ObservationClose why)
    {
        for (auto& c : clients)
            if (c)
                drop(c->id, why);
    }
    bool capacity(Client& c)
    {
        const auto n = c.sink->pendingBytes();
        if (n < 0 || n > ClientBytes || appBytes(c) > ClientBytes - n) {
            drop(c.id, ObservationClose::Backpressure);
            return false;
        }
        const auto drained = c.sink->drainRevision();
        if (drained < c.lastDrain) {
            drop(c.id, ObservationClose::Transport);
            return false;
        }
        if (n < c.lastQueued || drained > c.lastDrain)
            c.slowSince = now;
        c.lastDrain = drained;
        c.lastQueued = n;
        if (n + appBytes(c) == 0)
            c.slowSince.reset();
        else if (!c.slowSince)
            c.slowSince = now;
        if (c.slowSince && now - *c.slowSince >= 5000) {
            drop(c.id, ObservationClose::Timeout);
            return false;
        }
        return true;
    }
    bool enqueue(Client& c, QByteArray body, const QString& id, bool bound)
    {
        const auto queued = c.sink->pendingBytes();
        if (body.isEmpty() || body.size() > Codec::FrameBytes || queued < 0 ||
            queued > ClientBytes || body.size() + 4 > ClientBytes - queued - appBytes(c)) {
            drop(c.id, ObservationClose::Backpressure);
            return false;
        }
        c.replies.push_back(Frame{Codec::frame(body), id,
                                  current ? current->snapshot.binding : ObservationBinding{},
                                  current ? current->snapshot.lyricsRevision : 0, bound, 0});
        return capacity(c);
    }
    void mark(int dirty)
    {
        for (auto& c : clients)
            if (c)
                c->dirty |= dirty & c->topics;
    }
    void updateWire()
    {
        if (!current) {
            wire = {};
            return;
        }
        auto result = Codec::snapshot(current->snapshot, encoding);
        if (auto* value = std::get_if<QJsonObject>(&result)) {
            const auto dirty = changedTopics(wire, *value);
            wire = std::move(*value);
            mark(dirty);
        } else {
            wire = {};
            dropAll(ObservationClose::Protocol);
        }
    }
    void staleFrames()
    {
        if (!current)
            return;
        const auto stale = [&](const Frame& f) {
            return f.bound && (f.binding != current->snapshot.binding ||
                               f.revision != current->snapshot.lyricsRevision);
        };
        for (auto& slot : clients)
            if (slot) {
                auto& c = *slot;
                if (c.active && stale(*c.active)) {
                    if (c.active->offset) {
                        drop(c.id, ObservationClose::StalePartial);
                        continue;
                    }
                    if (c.active->id.isEmpty())
                        c.active.reset();
                    else
                        c.active = Frame{Codec::frame(bytes(Codec::error(
                                             c.active->id, ObservationError::StaleView))),
                                         c.active->id,
                                         {},
                                         0,
                                         false,
                                         0};
                }
                for (auto& f : c.replies)
                    if (stale(f))
                        f = Frame{
                            Codec::frame(bytes(Codec::error(f.id, ObservationError::StaleView))),
                            f.id,
                            {},
                            0,
                            false,
                            0};
            }
    }
    void refresh()
    {
        if (!source)
            return;
        const auto generation = source->fence();
        if (generation != fence) {
            dropAll(ObservationClose::Privacy);
            current.reset();
            encoding = {};
            encoder.reset();
            wire = {};
            fence = generation;
        }
        auto next = source->take();
        if (!next || next->fence != source->fence()) {
            if (current) {
                dropAll(ObservationClose::Privacy);
                current.reset();
                encoding = {};
                encoder.reset();
                wire = {};
            }
            return;
        }
        if (current && current->snapshot.binding == next->snapshot.binding &&
            current->snapshot.sequence == next->snapshot.sequence)
            return;
        const bool documentChanged =
            !current || current->snapshot.binding != next->snapshot.binding ||
            current->snapshot.lyricsRevision != next->snapshot.lyricsRevision ||
            current->snapshot.documentStatus != next->snapshot.documentStatus;
        current = std::move(next);
        fence = current->fence;
        if (documentChanged) {
            encoding = {};
            encoder.reset();
            if (current->snapshot.documentStatus == ObservationContentStatus::Ready &&
                current->document)
                encoder = std::make_unique<PlaybackLyricsEncoder>(current->document);
        }
        staleFrames();
        updateWire();
    }
    bool check(qint64 time)
    {
        if (closed)
            return false;
        if (time < 0 || (lastNow && time < *lastNow)) {
            closed = true;
            dropAll(ObservationClose::Clock);
            current.reset();
            encoding = {};
            encoder.reset();
            wire = {};
            return false;
        }
        now = time;
        lastNow = time;
        refresh();
        for (auto& slot : clients)
            if (slot) {
                auto& c = *slot;
                if ((!c.helloDone && now - c.born >= 5000) || c.framer.feed({}, now).closed) {
                    drop(c.id, ObservationClose::Timeout);
                    continue;
                }
                capacity(c);
            }
        return true;
    }
    void error(Client& c, const QString& id, ObservationError e)
    {
        enqueue(c, bytes(Codec::error(id, e)), id, false);
    }
    void handle(Client& c, DecodedRequest decoded)
    {
        if (const auto* failed = std::get_if<RequestFailure>(&decoded)) {
            error(c, failed->id, failed->code);
            return;
        }
        const auto& r = std::get<ObservationRequest>(decoded);
        if (!current || wire.isEmpty()) {
            error(c, r.id, ObservationError::NotReady);
            return;
        }
        if (r.method == ObservationMethod::Hello) {
            if (c.helloDone) {
                error(c, r.id, ObservationError::InvalidRequest);
                return;
            }
            auto o = response(r.id);
            o["instanceId"] = current->snapshot.binding.instanceId.toString(QUuid::WithoutBraces);
            o["methods"] =
                QJsonArray{"hello", "getSnapshot", "subscribe", "unsubscribe", "getLyricsSlice"};
            QJsonArray names;
            for (const auto& n : Codec::trackNames())
                names.append(n);
            o["tracks"] = names;
            o["limits"] = QJsonObject{{"requestBytes", qint64(Codec::RequestBytes)},
                                      {"frameBytes", qint64(Codec::FrameBytes)},
                                      {"snapshotBytes", qint64(Codec::SnapshotBytes)},
                                      {"documentBytes", qint64(Codec::DocumentBytes)},
                                      {"maxLines", 128},
                                      {"clients", 8},
                                      {"pendingRequests", 4},
                                      {"framesPerSecond", 16},
                                      {"burstFrames", 16},
                                      {"helloMs", 5000},
                                      {"partialFrameMs", 2000},
                                      {"inputBytes", 16384},
                                      {"clientBytes", qint64(ClientBytes)},
                                      {"allClientBytes", qint64(8 * ClientBytes)},
                                      {"slowMs", 5000}};
            c.helloDone = true;
            enqueue(c, bytes(o), r.id, false);
            return;
        }
        if (!c.helloDone) {
            drop(c.id, ObservationClose::Protocol);
            return;
        }
        if (r.method == ObservationMethod::LyricsSlice) {
            auto result = Codec::slice(r, current->snapshot, encoding);
            if (auto* b = std::get_if<QByteArray>(&result))
                enqueue(c, std::move(*b), r.id, true);
            else
                error(c, r.id, std::get<ObservationError>(result));
            return;
        }
        auto o = response(r.id);
        if (r.method == ObservationMethod::Unsubscribe) {
            c.topics = c.dirty = 0;
            if (c.active && c.active->id.isEmpty() && !c.active->offset)
                c.active.reset();
        } else {
            if (r.method == ObservationMethod::Subscribe) {
                c.topics = c.dirty = 0;
                if (c.active && c.active->id.isEmpty() && !c.active->offset)
                    c.active.reset();
                QJsonArray topics;
                for (const auto& t : r.topics) {
                    topics.append(t);
                    c.topics |= topicBit(t);
                }
                o["topics"] = topics;
            }
            o["snapshot"] = wire;
        }
        enqueue(c, bytes(o), r.id, r.method != ObservationMethod::Unsubscribe);
    }
    static constexpr qsizetype ClientBytes = PlaybackObservationConnections::ClientBytes;
    std::array<std::optional<Client>, 8> clients;
    std::shared_ptr<ObservationMailbox> source;
    std::optional<ObservationMailbox::Value> current;
    DocumentEncoding encoding;
    std::unique_ptr<PlaybackLyricsEncoder> encoder;
    QJsonObject wire;
    quint64 fence = 0, nextClient = 0;
    qint64 now = 0;
    std::optional<qint64> lastNow;
    ObservationClose reason = ObservationClose::Closed;
    bool closed = false;
};
PlaybackObservationConnections::PlaybackObservationConnections(
    std::shared_ptr<ObservationMailbox> source)
    : m_impl(std::make_unique<Impl>(std::move(source)))
{
}
PlaybackObservationConnections::~PlaybackObservationConnections() { close(); }
quint64 PlaybackObservationConnections::open(IObservationWriteSink& sink, qint64 nowMs)
{
    auto& p = *m_impl;
    if (!p.check(nowMs) || !p.source || !p.current || p.wire.isEmpty())
        return 0;
    if (p.nextClient == std::numeric_limits<quint64>::max()) {
        close();
        return 0;
    }
    for (auto& c : p.clients)
        if (!c) {
            c.emplace();
            c->id = ++p.nextClient;
            c->sink = &sink;
            c->born = c->rateAt = nowMs;
            if (!p.capacity(*c))
                return 0;
            return c->id;
        }
    return 0;
}
qsizetype PlaybackObservationConnections::receive(quint64 client, QByteArrayView input,
                                                  qint64 nowMs)
{
    auto& p = *m_impl;
    if (!p.check(nowMs))
        return 0;
    auto* c = p.find(client);
    if (!c)
        return 0;
    qsizetype consumed = 0;
    while (consumed < input.size() && c->ids.size() < 4) {
        const auto result = c->framer.feed(input.sliced(consumed), nowMs);
        consumed += result.consumed;
        if (result.closed) {
            p.drop(client, ObservationClose::Protocol);
            break;
        }
        if (!result.body)
            break;
        const auto elapsed = nowMs - c->rateAt;
        c->tokens = elapsed >= 1000 ? 16000 : std::min(16000, c->tokens + int(elapsed) * 16);
        c->rateAt = nowMs;
        if (c->tokens < 1000) {
            p.drop(client, ObservationClose::RateLimit);
            break;
        }
        c->tokens -= 1000;
        auto decoded = Codec::decode(*result.body);
        const auto id = idOf(decoded);
        const auto* request = std::get_if<ObservationRequest>(&decoded);
        if (!Codec::validId(id) || c->ids.contains(id) ||
            (!c->helloSeen && (!request || request->method != ObservationMethod::Hello))) {
            p.drop(client, ObservationClose::Protocol);
            break;
        }
        c->helloSeen = true;
        c->ids.insert(id);
        c->requests.push_back(std::move(decoded));
    }
    return consumed;
}
void PlaybackObservationConnections::pump(qint64 nowMs)
{
    auto& p = *m_impl;
    if (!p.check(nowMs))
        return;
    if (p.encoder) {
        p.encoder->step();
        if (p.encoder->done()) {
            p.encoding = p.encoder->result();
            p.encoder.reset();
            p.updateWire();
        }
    }
    if (p.source && p.source->fence() != p.fence) {
        p.refresh();
        return;
    }
    for (auto& slot : p.clients)
        if (slot && !slot->requests.empty()) {
            auto request = std::move(slot->requests.front());
            slot->requests.pop_front();
            p.handle(*slot, std::move(request));
        }
}
void PlaybackObservationConnections::write(quint64 client, qint64 nowMs)
{
    auto& p = *m_impl;
    if (!p.check(nowMs))
        return;
    auto* c = p.find(client);
    if (!c || !p.capacity(*c))
        return;
    if (!c->active) {
        if (!c->replies.empty()) {
            c->active = std::move(c->replies.front());
            c->replies.pop_front();
        } else if (c->dirty && p.current && !p.wire.isEmpty()) {
            // mark() already intersects topics; subscription replacement resets dirty.
            const auto mask = c->dirty;
            c->dirty = 0;
            if (mask) {
                const bool snapshotChanged = (mask & 1) != 0;
                const bool lyricsChanged = (mask & 2) != 0;
                const QString topic = snapshotChanged ? "snapshot"
                                      : lyricsChanged ? "lyrics"
                                                      : "cursor";
                QJsonObject event{
                    {"v", 1}, {"kind", "event"}, {"topic", topic}, {"snapshot", p.wire}};
                c->active = Impl::Frame{
                    Codec::frame(bytes(event)),         {},   p.current->snapshot.binding,
                    p.current->snapshot.lyricsRevision, true, 0};
                if (!p.capacity(*c))
                    return;
            }
        }
    }
    if (!c->active)
        return;
    // Last private-fence check at NONBLOCKING transport admission. A barrier during
    // write can only discard uncommitted bytes; accepted/in-flight bytes aren't revocable.
    if (p.source->fence() != p.fence) {
        p.refresh();
        return;
    }
    const auto data = c->active->data;
    const auto offset = c->active->offset;
    const auto before = c->sink->pendingBytes();
    const auto room = Impl::ClientBytes - p.appBytes(*c) - before;
    if (room <= 0)
        return; // Wait for actual transport drain, never exceed retained-byte cap.
    const auto view = QByteArrayView(data).sliced(
        offset, std::min({qsizetype(65536), data.size() - offset, qsizetype(room)}));
    const auto count = c->sink->write(view);
    if (count < 0 || count > view.size()) {
        p.drop(client, ObservationClose::Transport);
        return;
    }
    const auto after = c->sink->pendingBytes();
    if (count > 0 && after >= 0 && after < before + count)
        c->slowSince = nowMs;
    c->lastQueued = after;
    c->active->offset += count;
    if (c->active->offset == c->active->data.size()) {
        c->ids.remove(c->active->id);
        c->active.reset();
    }
    p.refresh();
    c = p.find(client);
    if (c)
        p.capacity(*c);
}
void PlaybackObservationConnections::close(quint64 client)
{
    m_impl->drop(client, ObservationClose::Closed);
}
void PlaybackObservationConnections::close()
{
    auto& p = *m_impl;
    p.closed = true;
    p.dropAll(ObservationClose::Closed);
    p.current.reset();
    p.encoding = {};
    p.encoder.reset();
    p.wire = {};
}
bool PlaybackObservationConnections::isOpen(quint64 client) const
{
    return m_impl->find(client) != nullptr;
}
int PlaybackObservationConnections::clientCount() const
{
    return int(std::count_if(m_impl->clients.begin(), m_impl->clients.end(),
                             [](const auto& c) { return c.has_value(); }));
}
int PlaybackObservationConnections::pendingRequests(quint64 client) const
{
    const auto* c = m_impl->find(client);
    return c ? int(c->ids.size()) : 0;
}
qsizetype PlaybackObservationConnections::applicationBytes(quint64 client) const
{
    const auto* c = m_impl->find(client);
    return c ? m_impl->appBytes(*c) : 0;
}
qsizetype PlaybackObservationConnections::inputBytes(quint64 client) const
{
    const auto* c = m_impl->find(client);
    return c ? c->framer.bufferedBytes() + qsizetype(c->ids.size()) * 1024 : 0;
}
ObservationClose PlaybackObservationConnections::lastClose() const { return m_impl->reason; }
bool PlaybackObservationConnections::encodingPending() const { return bool(m_impl->encoder); }
} // namespace cirrus::infrastructure::ipc
