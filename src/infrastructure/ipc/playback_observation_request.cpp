#include "playback_observation_codec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QStringDecoder>
#include <algorithm>
#include <cmath>

namespace cirrus::infrastructure::ipc
{
namespace
{
// Independent bounded grammar pass: QJsonDocument alone silently collapses duplicate
// keys and doesn't establish our depth/Unicode contract. No generic JSON extensions.
class StrictJson final
{
  public:
    explicit StrictJson(QString text) : m_text(std::move(text)) {}
    bool object()
    {
        space();
        if (peek() != u'{' || !value(1))
            return false;
        space();
        return m_at == m_text.size();
    }

  private:
    QChar peek() const { return m_at < m_text.size() ? m_text[m_at] : QChar{}; }
    bool take(QChar c)
    {
        if (peek() != c)
            return false;
        ++m_at;
        return true;
    }
    void space()
    {
        while (peek() == u' ' || peek() == u'\t' || peek() == u'\r' || peek() == u'\n')
            ++m_at;
    }
    bool hex(ushort& out)
    {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const auto c = peek().unicode();
            int n = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : -1;
            if (n < 0)
                return false;
            out = ushort((out << 4) | n);
            ++m_at;
        }
        return true;
    }
    bool string(QString& out)
    {
        if (!take(u'"'))
            return false;
        while (m_at < m_text.size()) {
            auto c = m_text[m_at++];
            if (c == u'"')
                return out.isValidUtf16();
            if (c.unicode() < 32)
                return false;
            if (c != u'\\') {
                out += c;
                continue;
            }
            if (m_at == m_text.size())
                return false;
            c = m_text[m_at++];
            if (c == u'"' || c == u'\\' || c == u'/')
                out += c;
            else if (c == u'b')
                out += QChar(8);
            else if (c == u'f')
                out += QChar(12);
            else if (c == u'n')
                out += u'\n';
            else if (c == u'r')
                out += u'\r';
            else if (c == u't')
                out += u'\t';
            else if (c == u'u') {
                ushort n = 0;
                if (!hex(n) || QChar::isLowSurrogate(n))
                    return false;
                out += QChar(n);
                if (QChar::isHighSurrogate(n)) {
                    if (!take(u'\\') || !take(u'u') || !hex(n) || !QChar::isLowSurrogate(n))
                        return false;
                    out += QChar(n);
                }
            } else
                return false;
        }
        return false;
    }
    bool number()
    {
        take(u'-');
        if (!take(u'0')) {
            if (peek() < u'1' || peek() > u'9')
                return false;
            do {
                ++m_at;
            } while (peek() >= u'0' && peek() <= u'9');
        }
        // Requests only have integer-number fields. Reject fractional/exponent
        // lexemes BEFORE Qt double rounding can turn 128.000000000000001 into 128.
        return peek() != u'.' && peek() != u'e' && peek() != u'E';
    }
    bool literal(QStringView x)
    {
        if (QStringView(m_text).mid(m_at, x.size()) != x)
            return false;
        m_at += x.size();
        return true;
    }
    bool value(int depth)
    {
        space();
        if (peek() == u'{' || peek() == u'[') {
            if (depth > 8)
                return false;
            const bool obj = take(u'{');
            if (!obj)
                ++m_at;
            const auto end = obj ? u'}' : u']';
            space();
            if (take(end))
                return true;
            QSet<QString> keys;
            do {
                space();
                if (obj) {
                    QString key;
                    if (!string(key) || keys.contains(key))
                        return false;
                    keys.insert(key);
                    space();
                    if (!take(u':'))
                        return false;
                }
                if (!value(depth + 1))
                    return false;
                space();
                if (take(end))
                    return true;
            } while (take(u','));
            return false;
        }
        if (peek() == u'"') {
            QString s;
            return string(s);
        }
        if (peek() == u't')
            return literal(u"true");
        if (peek() == u'f')
            return literal(u"false");
        if (peek() == u'n')
            return literal(u"null");
        return number();
    }
    QString m_text;
    qsizetype m_at = 0;
};
bool asciiId(const QString& s)
{
    if (s.isEmpty() || s.size() > 64)
        return false;
    for (auto c : s) {
        const auto n = c.unicode();
        if (!((n >= 'a' && n <= 'z') || (n >= 'A' && n <= 'Z') || (n >= '0' && n <= '9') ||
              n == '_' || n == '-'))
            return false;
    }
    return true;
}
bool uuid(const QJsonValue& v, QUuid& id)
{
    if (!v.isString())
        return false;
    const auto text = v.toString();
    id = QUuid(text);
    return !id.isNull() && text == id.toString(QUuid::WithoutBraces);
}
bool counter(const QJsonValue& v, quint64& n)
{
    if (!v.isString())
        return false;
    const auto s = v.toString();
    if (s.isEmpty() || s.size() > 20 || (s.size() > 1 && s[0] == u'0'))
        return false;
    for (auto c : s)
        if (c < u'0' || c > u'9')
            return false;
    bool ok = false;
    n = s.toULongLong(&ok);
    return ok;
}
bool integer(const QJsonValue& v, qsizetype low, qsizetype high, qsizetype& out)
{
    if (!v.isDouble())
        return false;
    const auto n = v.toDouble();
    if (!std::isfinite(n) || n < low || n > high || std::floor(n) != n)
        return false;
    out = qsizetype(n);
    return true;
}
} // namespace
bool PlaybackObservationCodec::validId(const QString& id) { return asciiId(id); }
const std::array<QString, 6>& PlaybackObservationCodec::trackNames()
{
    static const std::array<QString, 6> names{"original.standard",     "original.enhanced",
                                              "translation.standard",  "translation.enhanced",
                                              "romanization.standard", "romanization.enhanced"};
    return names;
}
std::optional<QJsonObject> PlaybackObservationCodec::strictLocalObject(QByteArrayView body)
{
    if (body.isEmpty() || body.size() > RequestBytes ||
        body.startsWith(QByteArrayView("\xef\xbb\xbf", 3)))
        return {};
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(body);
    if (decoder.hasError() || !text.isValidUtf16() || !StrictJson(text).object())
        return {};
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(body.toByteArray(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object();
}
DecodedRequest PlaybackObservationCodec::decode(QByteArrayView body)
{
    RequestFailure failure{ObservationError::InvalidRequest, {}};
    const auto parsed = strictLocalObject(body);
    if (!parsed)
        return failure;
    const auto o = *parsed;
    if (o["id"].isString() && asciiId(o["id"].toString()))
        failure.id = o["id"].toString();
    if (failure.id.isEmpty() || !o["v"].isDouble() || o["kind"] != QJsonValue("request") ||
        !o["method"].isString())
        return failure;
    if (o["v"].toDouble() != 1) {
        failure.code = ObservationError::UnsupportedVersion;
        return failure;
    }
    ObservationRequest r;
    r.id = failure.id;
    const auto method = o["method"].toString();
    QSet<QString> fields{"v", "kind", "id", "method"};
    if (method == "hello")
        r.method = ObservationMethod::Hello;
    else if (method == "getSnapshot")
        r.method = ObservationMethod::Snapshot;
    else if (method == "unsubscribe")
        r.method = ObservationMethod::Unsubscribe;
    else if (method == "subscribe") {
        r.method = ObservationMethod::Subscribe;
        fields.insert("topics");
        if (!o["topics"].isArray())
            return failure;
        const auto topics = o["topics"].toArray();
        if (topics.isEmpty() || topics.size() > 3)
            return failure;
        for (const auto t : topics) {
            if (!t.isString() ||
                (t.toString() != "snapshot" && t.toString() != "lyrics" &&
                 t.toString() != "cursor") ||
                r.topics.contains(t.toString()))
                return failure;
            r.topics.append(t.toString());
        }
    } else if (method == "getLyricsSlice") {
        r.method = ObservationMethod::LyricsSlice;
        fields.unite({"instanceId", "viewEpoch", "trackId", "lyricsRevision", "track", "fromLine",
                      "maxLines"});
        if (!uuid(o["instanceId"], r.binding.instanceId) ||
            !uuid(o["viewEpoch"], r.binding.viewEpoch) || !uuid(o["trackId"], r.binding.trackId) ||
            !counter(o["lyricsRevision"], r.lyricsRevision) || !o["track"].isString() ||
            !integer(o["fromLine"], 0, 10000, r.fromLine) ||
            !integer(o["maxLines"], 1, 128, r.maxLines))
            return failure;
        const auto it = std::find(trackNames().begin(), trackNames().end(), o["track"].toString());
        if (it == trackNames().end())
            return failure;
        r.track = int(it - trackNames().begin());
    } else
        return failure;
    const auto keys = o.keys();
    if (QSet<QString>(keys.begin(), keys.end()) != fields)
        return failure;
    return r;
}
QJsonObject PlaybackObservationCodec::error(const QString& id, ObservationError code)
{
    static const std::array<QString, 8> codes{
        "UnsupportedVersion", "InvalidRequest",      "NotReady", "StaleView",
        "LyricsNotReady",     "ExportLimitExceeded", "Busy",     "RateLimited"};
    if (!validId(id))
        return {}; // malformed correlation: close, don't echo or send id:null
    const auto n = int(code);
    return {{"v", 1},
            {"kind", "response"},
            {"id", id},
            {"ok", false},
            {"error",
             QJsonObject{{"code", n >= 0 && n < int(codes.size()) ? codes[size_t(n)]
                                                                  : QString("InvalidRequest")}}}};
}
QByteArray PlaybackObservationCodec::frame(QByteArrayView body)
{
    if (body.isEmpty() || body.size() > FrameBytes)
        return {};
    QByteArray out(4, '\0');
    const auto n = quint32(body.size());
    for (int i = 0; i < 4; ++i)
        out[i] = char(n >> (24 - 8 * i));
    out.append(body.data(), body.size());
    return out;
}
void ObservationRequestFramer::close()
{
    m_closed = true;
    m_prefix.clear();
    m_body.clear();
    m_length = 0;
    m_started.reset();
}
ObservationRequestFramer::Result ObservationRequestFramer::feed(QByteArrayView input, qint64 nowMs)
{
    Result r;
    if (nowMs < 0 || (m_lastNow && nowMs < *m_lastNow) || (m_started && nowMs - *m_started >= 2000))
        close();
    m_lastNow = nowMs;
    if (m_closed) {
        r.closed = true;
        return r;
    }
    if (!input.isEmpty() && !m_started)
        m_started = nowMs;
    while (r.consumed < input.size()) {
        if (m_prefix.size() < 4) {
            m_prefix.append(input[r.consumed++]);
            if (m_prefix.size() == 4) {
                for (auto c : m_prefix)
                    m_length = (m_length << 8) | quint8(c);
                if (!m_length || m_length > PlaybackObservationCodec::RequestBytes) {
                    close();
                    r.closed = true;
                    return r;
                }
                m_body.reserve(m_length);
            }
        } else {
            const auto n = qMin<qsizetype>(input.size() - r.consumed, m_length - m_body.size());
            m_body.append(input.data() + r.consumed, n);
            r.consumed += n;
            if (m_body.size() == m_length) {
                r.body = std::move(m_body);
                m_body.clear();
                m_prefix.clear();
                m_length = 0;
                m_started.reset();
                return r;
            }
        }
    }
    return r;
}
} // namespace cirrus::infrastructure::ipc
