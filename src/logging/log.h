#pragma once
#include "logging/categories.h"

#include <QDebug>
#include <QMetaEnum>
#include <QString>
#include <QUuid>

#include <concepts>
#include <type_traits>

class QUrl;

// Writing a log record (docs/tasks/logging.md §4.4): a stable English event name, then
// key=value fields, each value made safe by its type (§5).
//   qCWarning(lcNet) << log::event("request.failed") << log::field("req", id)
//                    << log::field("err", error) << log::field("host", log::host(url));
// Field names are lower_snake_case and mean the same thing everywhere: req (request
// number), token (playback load), song/album/playlist/artist (public ids), ms
// (duration), bytes, err (error name), code (provider code), status (HTTP status).
namespace cirrus::log
{
// Text written as it is (no quotes); what event() and field() return.
struct Text {
    QString text;
};
inline QDebug operator<<(QDebug d, const Text& t)
{
    QDebugStateSaver saver(d);
    d.noquote() << t.text;
    return d;
}

inline Text event(const char* name) { return {QString::fromLatin1(name)}; }

// A value quoted when it has spaces, quotes, '=' or control characters; long values are
// cut at 200 characters.
QString quoted(QStringView value);

// Safe forms of values that must not be logged whole.
struct Host { // a URL's host only: never its path, query or credentials
    QString value;
};
struct Path { // a local path under a known root (<data>/…, <cache>/…) or its file name
    QString value;
};
struct Account { // an account id hashed with this installation's key
    QString value;
};
Host host(const QUrl& url);
Path path(const QString& localPath);
Account account(QStringView id);

namespace detail
{
template <typename T> inline constexpr bool unsafe = false;

template <typename T> QString value(const T& v)
{
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, bool>)
        return v ? QStringLiteral("true") : QStringLiteral("false");
    else if constexpr (requires {
                           { logName(v) } -> std::convertible_to<QString>;
                       })
        return logName(v); // an enum's names, found by ADL next to the enum
    else if constexpr (std::is_enum_v<U> && requires { qt_getEnumMetaObject(v); }) {
        const char* key = QMetaEnum::fromType<U>().valueToKey(int(v));
        return key ? QString::fromLatin1(key) : QString::number(qlonglong(v));
    } else if constexpr (std::is_enum_v<U>)
        return QString::number(qlonglong(std::underlying_type_t<U>(v)));
    else if constexpr (std::is_integral_v<U>)
        return QString::number(v);
    else if constexpr (std::is_floating_point_v<U>)
        return QString::number(v, 'g', 6);
    else if constexpr (std::is_same_v<U, Host> || std::is_same_v<U, Path> ||
                       std::is_same_v<U, Account>)
        return quoted(v.value);
    else if constexpr (std::is_same_v<U, QUuid>)
        return v.toString(QUuid::WithoutBraces);
    else if constexpr (std::is_convertible_v<const U&, QStringView>)
        return quoted(QStringView(v));
    else if constexpr (std::is_convertible_v<const U&, const char*>)
        return quoted(QString::fromUtf8(v));
    else if constexpr (std::is_same_v<U, QLatin1StringView>)
        return quoted(QString(v));
    else
        // URLs, byte arrays (bodies, cookies) and the like: log::host() for a URL, the
        // size for a body; never the thing itself.
        static_assert(unsafe<U>, "not a loggable value; see docs/tasks/logging.md §5");
}
} // namespace detail

template <typename T> Text field(const char* key, const T& value)
{
    return {QString::fromLatin1(key) + u'=' + detail::value(value)};
}
} // namespace cirrus::log
