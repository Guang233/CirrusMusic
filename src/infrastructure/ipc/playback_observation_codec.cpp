#include "playback_observation_codec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

namespace cirrus::infrastructure::ipc
{
using namespace domain;
namespace
{
constexpr qint64 MaxTime = ObservedPlayback::MaximumMs;
template <class E, size_t N> QString name(E e, const std::array<const char*, N>& names)
{
    const auto n = int(e);
    return n >= 0 && n < int(N) ? QString::fromLatin1(names[size_t(n)]) : QString{};
}
QString availability(LyricAvailability x)
{
    return name(x, std::array{"missing", "empty", "present", "malformed"});
}
QString timing(LyricTiming x) { return name(x, std::array{"plainText", "lines", "words"}); }
QString content(ObservationContentStatus x)
{
    return name(x, std::array{"notReady", "ready", "limitExceeded", "invalidData"});
}
QString uuid(QUuid x) { return x.toString(QUuid::WithoutBraces); }
bool bindingValid(ObservationBinding b)
{
    return !b.instanceId.isNull() && !b.viewEpoch.isNull() && !b.trackId.isNull();
}
template <class T> QJsonValue optional(const std::optional<T>& x)
{
    return x ? QJsonValue(*x) : QJsonValue{};
}
bool text(const QString& s) { return s.isValidUtf16() && !s.contains(QChar::Null); }
bool time(std::optional<qint64> x) { return !x || (*x >= 0 && *x <= MaxTime); }
std::array<const LyricTrack*, 6> tracks(const SongLyrics& l)
{
    return {&l.original.standard,    &l.original.enhanced,     &l.translation.standard,
            &l.translation.enhanced, &l.romanization.standard, &l.romanization.enhanced};
}
std::array<const LyricCursor*, 6> cursors(const LyricCursors& c)
{
    return {&c.original.standard,    &c.original.enhanced,     &c.translation.standard,
            &c.translation.enhanced, &c.romanization.standard, &c.romanization.enhanced};
}
QJsonObject summary(const ObservationLyricSummary& t)
{
    return {{"availability", availability(t.availability)},
            {"timing", timing(t.timing)},
            {"providerOffsetMs", t.providerOffsetMs},
            {"malformedLines", qint64(t.malformedLines)},
            {"lineCount", qint64(t.lineCount)}};
}
bool validSummary(const ObservationLyricSummary& s)
{
    return !availability(s.availability).isEmpty() && !timing(s.timing).isEmpty() &&
           s.providerOffsetMs >= -MaxTime && s.providerOffsetMs <= MaxTime && s.lineCount >= 0 &&
           s.lineCount <= 10000 && s.malformedLines >= 0 && s.malformedLines <= 2147483647 &&
           (s.availability == LyricAvailability::Present ? s.lineCount > 0 : s.lineCount == 0);
}
// Bounded string writer. Computes exact escaped UTF-8 size BEFORE allocation/conversion.
// No unbounded QJsonObject tree for line/word text, no text truncation on overflow.
class Writer final
{
  public:
    explicit Writer(qsizetype cap) : m_cap(cap) {}
    void raw(QByteArrayView s)
    {
        if (!m_ok || s.size() > m_cap - m_out.size())
            m_ok = false;
        else
            m_out.append(s.data(), s.size());
    }
    void number(qint64 n) { raw(QByteArray::number(n)); }
    void nullable(std::optional<qint64> n)
    {
        if (n)
            number(*n);
        else
            raw("null");
    }
    void quote(const QString& s)
    {
        if (!m_ok)
            return;
        if (!text(s)) {
            m_ok = false;
            return;
        }
        qsizetype cost = 2;
        for (qsizetype i = 0; i < s.size(); ++i) {
            const auto n = s[i].unicode();
            cost += n == '"' || n == '\\' ? 2
                    : n < 32   ? (n == 8 || n == 9 || n == 10 || n == 12 || n == 13 ? 2 : 6)
                    : n < 128  ? 1
                    : n < 2048 ? 2
                    : QChar::isHighSurrogate(n) ? 4
                                                : 3;
            if (cost > m_cap - m_out.size()) {
                m_ok = false;
                return;
            }
            if (QChar::isHighSurrogate(n))
                ++i;
        }
        raw("\"");
        const auto bytes = s.toUtf8();
        static constexpr char hex[] = "0123456789abcdef";
        for (const auto c : bytes) {
            const auto n = quint8(c);
            if (c == '"' || c == '\\') {
                m_out.append('\\');
                m_out.append(c);
            } else if (n < 32) {
                if (n == 8)
                    m_out.append("\\b");
                else if (n == 9)
                    m_out.append("\\t");
                else if (n == 10)
                    m_out.append("\\n");
                else if (n == 12)
                    m_out.append("\\f");
                else if (n == 13)
                    m_out.append("\\r");
                else {
                    m_out.append("\\u00");
                    m_out.append(hex[n >> 4]);
                    m_out.append(hex[n & 15]);
                }
            } else
                m_out.append(c);
        }
        raw("\"");
    }
    [[nodiscard]] std::optional<QByteArray> finish()
    {
        return m_ok ? std::optional(std::move(m_out)) : std::nullopt;
    }

  private:
    QByteArray m_out;
    qsizetype m_cap;
    bool m_ok = true;
};
} // namespace
class PlaybackLyricsEncoder::Impl
{
  public:
    explicit Impl(std::shared_ptr<const PlaybackLyricDocument> input) : source(std::move(input))
    {
        if (!source || !bindingValid(source->binding) || !validMusicId(source->lyrics.song.value)) {
            fail(ObservationContentStatus::InvalidData);
            return;
        }
        out = std::make_shared<PreparedLyrics>();
        out->m_binding = source->binding;
        out->m_revision = source->revision;
        out->m_song = source->lyrics.song;
        const auto ts = tracks(source->lyrics);
        for (size_t i = 0; i < 6; ++i) {
            const auto& t = *ts[i];
            if (t.lines.size() > 10000) {
                fail(ObservationContentStatus::LimitExceeded);
                return;
            }
            const ObservationLyricSummary info{t.availability, t.timing, t.providerOffsetMs,
                                               t.lines.size(), t.malformedLines};
            if (!validSummary(info)) {
                fail(ObservationContentStatus::InvalidData);
                return;
            }
            out->m_tracks[i].summary = summary(info);
            if (!add((i ? 1 : 0) + PlaybackObservationCodec::trackNames()[i].size() + 3 +
                     QJsonDocument(out->m_tracks[i].summary).toJson(QJsonDocument::Compact).size() +
                     11)) {
                fail(ObservationContentStatus::LimitExceeded);
                return;
            }
        }
    }
    void fail(ObservationContentStatus status)
    {
        value = {status, {}};
        finished = true;
        writer.reset();
        out.reset();
        source.reset();
    }
    bool add(qsizetype n)
    {
        if (n > PlaybackObservationCodec::DocumentBytes - total)
            return false;
        total += n;
        return true;
    }
    bool addText(const QString& s)
    {
        if (s.size() > PlaybackObservationCodec::TextUnits - units)
            return false;
        units += s.size();
        return true;
    }
    void step(int records)
    {
        if (finished)
            return;
        records = std::clamp(records, 1, 128);
        const auto ts = tracks(source->lyrics);
        while (records > 0 && track < 6) {
            const auto& t = *ts[track];
            if (line == t.lines.size()) {
                ++track;
                line = word = words = 0;
                continue;
            }
            const auto& l = t.lines[line];
            --records;
            if (!writer) {
                if (l.words.size() > 30000 - words || !addText(l.text)) {
                    fail(ObservationContentStatus::LimitExceeded);
                    return;
                }
                if (!time(l.startMs) || !time(l.endMs) ||
                    (l.startMs && l.endMs && *l.endMs < *l.startMs) || !text(l.text)) {
                    fail(ObservationContentStatus::InvalidData);
                    return;
                }
                words += l.words.size();
                writer.emplace(PlaybackObservationCodec::DocumentBytes - total);
                writer->raw("{\"index\":");
                writer->number(line);
                writer->raw(",\"startMs\":");
                writer->nullable(l.startMs);
                writer->raw(",\"endMs\":");
                writer->nullable(l.endMs);
                writer->raw(",\"text\":");
                writer->quote(l.text);
                writer->raw(",\"words\":[");
            } else if (word < l.words.size()) {
                const auto& w = l.words[word];
                if (!addText(w.text)) {
                    fail(ObservationContentStatus::LimitExceeded);
                    return;
                }
                if (w.startMs < 0 || w.startMs > MaxTime || w.durationMs < 0 ||
                    w.durationMs > MaxTime - w.startMs || !text(w.text)) {
                    fail(ObservationContentStatus::InvalidData);
                    return;
                }
                if (word)
                    writer->raw(",");
                writer->raw("{\"index\":");
                writer->number(word);
                writer->raw(",\"startMs\":");
                writer->number(w.startMs);
                writer->raw(",\"durationMs\":");
                writer->number(w.durationMs);
                writer->raw(",\"text\":");
                writer->quote(w.text);
                writer->raw("}");
                ++word;
            } else {
                writer->raw("]}");
                auto bytes = writer->finish();
                if (!bytes || !add(bytes->size() + (line ? 1 : 0))) {
                    fail(ObservationContentStatus::LimitExceeded);
                    return;
                }
                out->m_tracks[track].lines.append(std::move(*bytes));
                out->m_tracks[track].wordCounts.append(l.words.size());
                writer.reset();
                word = 0;
                ++line;
            }
        }
        if (track == 6) {
            out->m_bytes = total;
            value = {ObservationContentStatus::Ready, std::move(out)};
            finished = true;
            source.reset();
        }
    }
    std::shared_ptr<const PlaybackLyricDocument> source;
    std::shared_ptr<PreparedLyrics> out;
    DocumentEncoding value;
    std::optional<Writer> writer;
    qsizetype total = QByteArrayView("{\"tracks\":{}}").size(), units = 0, line = 0, word = 0,
              words = 0;
    size_t track = 0;
    bool finished = false;
};
PlaybackLyricsEncoder::PlaybackLyricsEncoder(std::shared_ptr<const PlaybackLyricDocument> document)
    : m_impl(std::make_unique<Impl>(std::move(document)))
{
}
PlaybackLyricsEncoder::~PlaybackLyricsEncoder() = default;
void PlaybackLyricsEncoder::step(int records) { m_impl->step(records); }
bool PlaybackLyricsEncoder::done() const { return m_impl->finished; }
DocumentEncoding PlaybackLyricsEncoder::result() const { return m_impl->value; }
DocumentEncoding PlaybackObservationCodec::prepare(const PlaybackLyricDocument& doc)
{
    PlaybackLyricsEncoder encoder(std::make_shared<const PlaybackLyricDocument>(doc));
    while (!encoder.done())
        encoder.step();
    return encoder.result();
}
std::variant<QJsonObject, ObservationError>
PlaybackObservationCodec::snapshot(const PlaybackObservation& input,
                                   const DocumentEncoding& encoding)
{
    PlaybackObservation p = input;
    if (p.binding.instanceId.isNull() || p.binding.viewEpoch.isNull() || p.sampleMonoMs < 0 ||
        (p.status != ObservationStatus::Ready && p.status != ObservationStatus::Unavailable))
        return ObservationError::InvalidRequest;
    if (p.status == ObservationStatus::Unavailable) {
        PlaybackObservation blank;
        blank.binding = {p.binding.instanceId, p.binding.viewEpoch, {}};
        blank.sequence = p.sequence;
        blank.sampleMonoMs = p.sampleMonoMs;
        p = blank;
    }
    const auto state = name(p.state, std::array{"empty", "resolving", "loading", "ready", "playing",
                                                "paused", "ended", "failed"});
    const auto intent = name(p.intent, std::array{"play", "pause", "stop"});
    const auto mode = name(p.mode, std::array{"queue", "personalFm"});
    const auto lyricsState = name(p.lyricsState, std::array{"none", "loading", "ready", "failed"});
    if (state.isEmpty() || intent.isEmpty() || mode.isEmpty() || lyricsState.isEmpty() ||
        (p.song && !validMusicId(p.song->value)) || (p.entry && p.entry->isNull()) ||
        !time(p.positionMs) || !time(p.durationMs) || !time(p.songPositionMs) ||
        (p.durationMs && *p.durationMs == 0) ||
        (p.positionMs && p.durationMs && *p.positionMs > *p.durationMs) ||
        (p.positionObservedMonoMs &&
         (*p.positionObservedMonoMs < 0 || *p.positionObservedMonoMs > p.sampleMonoMs)) ||
        (p.volumeLinear &&
         (!std::isfinite(*p.volumeLinear) || *p.volumeLinear < 0 || *p.volumeLinear > 1)) ||
        p.userOffsetMs < -120000 || p.userOffsetMs > 120000 ||
        (p.order && name(*p.order, std::array{"sequential", "shuffle"}).isEmpty()) ||
        (p.repeat && name(*p.repeat, std::array{"off", "all", "one"}).isEmpty()))
        return ObservationError::InvalidRequest;
    if (p.preview && (p.preview->startMs < 0 || p.preview->endMs <= p.preview->startMs ||
                      p.preview->endMs > MaxTime ||
                      (p.durationMs && *p.durationMs > p.preview->endMs - p.preview->startMs) ||
                      (p.positionMs && *p.positionMs > p.preview->endMs - p.preview->startMs)))
        return ObservationError::InvalidRequest;
    if (p.mode == ListeningMode::PersonalFm && (p.order || p.repeat || p.entry))
        return ObservationError::InvalidRequest;
    if (p.positionMs && p.songPositionMs &&
        (*p.positionMs > MaxTime - (p.preview ? p.preview->startMs : 0) ||
         *p.songPositionMs != *p.positionMs + (p.preview ? p.preview->startMs : 0)))
        return ObservationError::InvalidRequest;
    if (p.binding.trackId.isNull() &&
        (p.song || p.entry || p.positionMs || p.durationMs || p.songPositionMs || p.preview ||
         p.metadata || p.lyricTracks || p.cursors || p.failure || p.state != PlaybackState::Empty))
        return ObservationError::InvalidRequest;
    QJsonObject root{
        {"instanceId", uuid(p.binding.instanceId)},
        {"viewEpoch", uuid(p.binding.viewEpoch)},
        {"trackId",
         p.binding.trackId.isNull() ? QJsonValue{} : QJsonValue(uuid(p.binding.trackId))},
        {"sequence", QString::number(p.sequence)},
        {"lyricsRevision", QString::number(p.lyricsRevision)},
        {"availability", p.status == ObservationStatus::Ready ? "ready" : "unavailable"},
        {"playback", QJsonObject{{"state", state}, {"intent", intent}}},
        {"track", p.binding.trackId.isNull()
                      ? QJsonValue{}
                      : QJsonValue(QJsonObject{
                            {"providerSongId", p.song ? QJsonValue(p.song->value) : QJsonValue{}},
                            {"entryId", p.entry ? QJsonValue(uuid(*p.entry)) : QJsonValue{}}})},
        {"timing",
         QJsonObject{{"positionMs", optional(p.positionMs)},
                     {"durationMs", optional(p.durationMs)},
                     {"songPositionMs", optional(p.songPositionMs)},
                     {"preview", p.preview ? QJsonValue(QJsonObject{{"startMs", p.preview->startMs},
                                                                    {"endMs", p.preview->endMs}})
                                           : QJsonValue{}}}},
        {"clock", QJsonObject{{"sampleMonoMs", QString::number(p.sampleMonoMs)},
                              {"positionObservedMonoMs",
                               p.positionObservedMonoMs
                                   ? QJsonValue(QString::number(*p.positionObservedMonoMs))
                                   : QJsonValue{}}}},
        {"settings",
         QJsonObject{
             {"volumeLinear", p.volumeLinear ? QJsonValue(double(*p.volumeLinear)) : QJsonValue{}},
             {"mode", mode},
             {"order", p.order ? QJsonValue(name(*p.order, std::array{"sequential", "shuffle"}))
                               : QJsonValue{}},
             {"repeat", p.repeat ? QJsonValue(name(*p.repeat, std::array{"off", "all", "one"}))
                                 : QJsonValue{}},
             {"fmWaiting", p.fmWaiting},
             {"fmBlocked", p.fmBlocked}}},
        {"capabilities", QJsonObject{{"canPlay", p.capabilities.play},
                                     {"canPause", p.capabilities.pause},
                                     {"canStop", p.capabilities.stop},
                                     {"canNext", p.capabilities.next},
                                     {"canPrevious", p.capabilities.previous},
                                     {"canSeek", p.capabilities.seek},
                                     {"canSetVolume", p.capabilities.volume},
                                     {"canSetModes", p.capabilities.modes}}},
        {"failure", QJsonValue{}},
        {"metadata", QJsonValue{}}};
    if (p.failure) {
        const auto code =
            name(*p.failure, std::array{"InvalidRequest", "StaleContext", "Canceled", "Unavailable",
                                        "ProtocolKeyMissing", "Network", "Timeout", "RateLimited",
                                        "InvalidResponse", "UnsafeSource", "Expired", "TooLarge",
                                        "Storage", "UnsupportedFormat", "Decode", "NoOutput"});
        if (code.isEmpty())
            return ObservationError::InvalidRequest;
        root["failure"] = QJsonObject{{"code", code}};
    }
    auto metadataStatus = content(p.metadataStatus);
    if (metadataStatus == "notReady")
        metadataStatus = "unknown";
    if (metadataStatus.isEmpty())
        metadataStatus = "invalidData";
    if (p.metadataStatus == ObservationContentStatus::Ready) {
        qsizetype units = 0;
        bool invalid = !p.metadata;
        bool large = p.metadata && p.metadata->artists.size() > 100;
        const auto check = [&](const QString& s) {
            if (s.size() > 4096 || s.size() > 16384 - units)
                large = true;
            else {
                units += s.size();
                invalid |= !text(s);
            }
        };
        if (p.metadata && !large) {
            check(p.metadata->title);
            for (const auto& s : p.metadata->artists)
                check(s);
            if (p.metadata->album)
                check(*p.metadata->album);
        }
        if (invalid)
            metadataStatus = "invalidData";
        else if (large)
            metadataStatus = "limitExceeded";
        else {
            QJsonArray artists;
            for (const auto& s : p.metadata->artists)
                artists.append(s);
            root["metadata"] = QJsonObject{{"title", p.metadata->title},
                                           {"artists", artists},
                                           {"album", optional(p.metadata->album)}};
        }
    }
    root["metadataExportStatus"] = metadataStatus;
    QString docStatus = content(p.documentStatus);
    if (docStatus.isEmpty())
        docStatus = "invalidData";
    const auto prepared = encoding.document.get();
    const bool matching = prepared && prepared->m_binding == p.binding &&
                          prepared->m_revision == p.lyricsRevision &&
                          (!p.song || prepared->m_song == *p.song);
    if (p.documentStatus == ObservationContentStatus::Ready) {
        if (encoding.status == ObservationContentStatus::NotReady)
            docStatus = "preparing";
        else if (encoding.status != ObservationContentStatus::Ready) {
            docStatus = content(encoding.status);
            if (docStatus.isEmpty())
                docStatus = "invalidData";
        } else
            docStatus = matching ? "ready" : "invalidData";
    }
    QString cursorStatus = content(p.cursorStatus);
    if (cursorStatus.isEmpty())
        cursorStatus = "invalidData";
    QJsonObject wireTracks;
    const auto resetTracks = [&] {
        for (const auto& key : trackNames())
            wireTracks[key] = QJsonObject{{"summary", QJsonValue{}}, {"cursor", QJsonValue{}}};
    };
    resetTracks();
    bool summaryValid = p.lyricTracks.has_value();
    if (p.lyricTracks)
        for (size_t i = 0; i < 6; ++i) {
            const auto& s = (*p.lyricTracks)[i];
            summaryValid &= validSummary(s);
            if (matching)
                summaryValid &= summary(s) == prepared->m_tracks[i].summary;
        }
    if ((p.lyricTracks || p.documentStatus == ObservationContentStatus::Ready) && !summaryValid) {
        docStatus = "invalidData";
        cursorStatus = "invalidData";
    }
    if (summaryValid)
        for (size_t i = 0; i < 6; ++i)
            wireTracks[trackNames()[i]] =
                QJsonObject{{"summary", summary((*p.lyricTracks)[i])}, {"cursor", QJsonValue{}}};
    if (p.cursorStatus == ObservationContentStatus::Ready) {
        if (docStatus == "invalidData" || !summaryValid)
            cursorStatus = "invalidData";
        else if (!matching || docStatus != "ready")
            cursorStatus = "notReady";
        else if (!p.cursors)
            cursorStatus = "invalidData";
        else {
            const auto cs = cursors(*p.cursors);
            qsizetype count = 0;
            bool invalid = false, large = false;
            for (size_t i = 0; i < 6; ++i) {
                const auto& c = *cs[i];
                const auto& t = prepared->m_tracks[i];
                if (c.lines.size() > CursorEntries - count) {
                    large = true;
                    break;
                }
                count += c.lines.size();
                if (c.words.size() > CursorEntries - count) {
                    large = true;
                    break;
                }
                count += c.words.size();
                QSet<qsizetype> active;
                QJsonArray lines, words;
                for (const auto index : c.lines) {
                    if (index < 0 || index >= t.lines.size()) {
                        invalid = true;
                        break;
                    }
                    active.insert(index);
                    lines.append(qint64(index));
                }
                if (invalid)
                    break;
                for (const auto& word : c.words) {
                    if (!active.contains(word.line) || word.word < 0 ||
                        word.word >= t.wordCounts[word.line]) {
                        invalid = true;
                        break;
                    }
                    words.append(
                        QJsonObject{{"line", qint64(word.line)}, {"word", qint64(word.word)}});
                }
                if (invalid)
                    break;
                auto track = wireTracks[trackNames()[i]].toObject();
                track["cursor"] = QJsonObject{{"lines", lines}, {"words", words}};
                wireTracks[trackNames()[i]] = track;
            }
            if (large || invalid) {
                cursorStatus = large ? "limitExceeded" : "invalidData";
                for (const auto& key : trackNames()) {
                    auto t = wireTracks[key].toObject();
                    t["cursor"] = QJsonValue{};
                    wireTracks[key] = t;
                }
            }
        }
    }
    QJsonObject lyrics{{"state", lyricsState},
                       {"documentExportStatus", docStatus},
                       {"cursorExportStatus", cursorStatus},
                       {"userOffsetMs", p.userOffsetMs},
                       {"providerNoLyric", optional(p.providerNoLyric)},
                       {"instrumental", optional(p.instrumental)},
                       {"tracks", wireTracks}};
    root["lyrics"] = lyrics;
    // Arrays are already structurally bounded. Degrade the entire cursor, never cut indexes.
    if (QJsonDocument(root).toJson(QJsonDocument::Compact).size() > SnapshotBytes) {
        for (const auto& key : trackNames()) {
            auto t = wireTracks[key].toObject();
            t["cursor"] = QJsonValue{};
            wireTracks[key] = t;
        }
        lyrics["tracks"] = wireTracks;
        lyrics["cursorExportStatus"] = "limitExceeded";
        root["lyrics"] = lyrics;
    }
    if (QJsonDocument(root).toJson(QJsonDocument::Compact).size() > SnapshotBytes)
        return ObservationError::ExportLimitExceeded;
    return root;
}
std::variant<QByteArray, ObservationError>
PlaybackObservationCodec::slice(const ObservationRequest& r, const PlaybackObservation& current,
                                const DocumentEncoding& encoding)
{
    if (!validId(r.id) || r.method != ObservationMethod::LyricsSlice || r.track < 0 ||
        r.track >= 6 || r.fromLine < 0 || r.maxLines < 1 || r.maxLines > 128)
        return ObservationError::InvalidRequest;
    if (current.status != ObservationStatus::Ready)
        return ObservationError::NotReady;
    if (r.binding != current.binding || r.lyricsRevision != current.lyricsRevision)
        return ObservationError::StaleView;
    if (current.documentStatus == ObservationContentStatus::LimitExceeded ||
        encoding.status == ObservationContentStatus::LimitExceeded)
        return ObservationError::ExportLimitExceeded;
    if (current.documentStatus != ObservationContentStatus::Ready ||
        encoding.status != ObservationContentStatus::Ready || !encoding.document)
        return ObservationError::LyricsNotReady;
    const auto& doc = *encoding.document;
    if (doc.m_binding != r.binding || doc.m_revision != r.lyricsRevision ||
        (current.song && doc.m_song != *current.song))
        return ObservationError::StaleView;
    if (!current.lyricTracks)
        return ObservationError::LyricsNotReady;
    for (size_t i = 0; i < 6; ++i)
        if (!validSummary((*current.lyricTracks)[i]) ||
            summary((*current.lyricTracks)[i]) != doc.m_tracks[i].summary)
            return ObservationError::LyricsNotReady;
    const auto& t = doc.m_tracks[size_t(r.track)];
    if (r.fromLine > t.lines.size())
        return ObservationError::InvalidRequest;
    QJsonObject envelope{{"v", 1},
                         {"kind", "response"},
                         {"id", r.id},
                         {"ok", true},
                         {"instanceId", uuid(r.binding.instanceId)},
                         {"viewEpoch", uuid(r.binding.viewEpoch)},
                         {"trackId", uuid(r.binding.trackId)},
                         {"lyricsRevision", QString::number(r.lyricsRevision)},
                         {"track", trackNames()[size_t(r.track)]},
                         {"availability", t.summary["availability"]},
                         {"fromLine", qint64(r.fromLine)},
                         {"lineCount", qint64(t.lines.size())}};
    auto body = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    body.chop(1);
    body.append(",\"lines\":[");
    qsizetype next = r.fromLine;
    for (; next < t.lines.size() && next - r.fromLine < r.maxLines; ++next) {
        const auto& line = t.lines[next];
        const auto suffix =
            QByteArray("],\"nextLine\":") +
            (next + 1 == t.lines.size() ? QByteArray("null") : QByteArray::number(next + 1)) + '}';
        if (line.size() + (next != r.fromLine ? 1 : 0) > FrameBytes - body.size() - suffix.size())
            break;
        if (next != r.fromLine)
            body.append(',');
        body.append(line);
    }
    if (next == r.fromLine && next != t.lines.size())
        return ObservationError::ExportLimitExceeded;
    body.append("],\"nextLine\":");
    body.append(next == t.lines.size() ? QByteArray("null") : QByteArray::number(next));
    body.append('}');
    return body;
}
} // namespace cirrus::infrastructure::ipc
