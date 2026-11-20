#pragma once
#include "logging/categories.h"

#include <QMetaEnum>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QtLogging>

#include <concepts>
#include <initializer_list>
#include <iterator>
#include <source_location>
#include <type_traits>

class QUrl;

// Writing a log record (docs/tasks/logging.md §4.4): one call, the level, the category, a
// stable English event name, then key=value fields, each value made safe by its type (§5).
//   log::info(lcApp, "app.quit", {"code", code});
//   log::warn(lcNet, "request.failed",
//             {{"req", id}, {"err", error}, {"host", log::host(url)}});
// Field names are lower_snake_case and mean the same thing everywhere: req (request
// number), token (playback load), song/album/playlist/artist (public ids), ms
// (duration), bytes, err (error name), code (provider code), status (HTTP status).
namespace cirrus::log
{
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
struct Message { // a library's own words (an error string), any URL in it cut to its host
    QString value;
};
Host host(const QUrl& url);
Path path(const QString& localPath);
Account account(QStringView id);
// What Qt or a library says went wrong, safe to log: https://h/p?q in it becomes
// https://h/…, and it is cut at 300 characters. Never for user text.
Message message(const QString& text);

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
                       std::is_same_v<U, Account> || std::is_same_v<U, Message>)
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

// A record's fields, written as `key=value key=value`, each value formatted when the field is
// made. Written as braces:
//   {"ms", 203}                        one field
//   {{"req", id}, {"ms", 203}}         several, in this order
//   {{"req", id}, more}                other Fields in among them (a Durations::take())
// Fields are plain values: keep them for a later record, or build them up with add().
class Fields
{
  public:
    Fields() = default;
    template <typename T>
    Fields(const char* key, const T& value)
        : m_text(QString::fromLatin1(key) + u'=' + detail::value(value))
    {
    }
    Fields(std::initializer_list<Fields> fields)
    {
        for (const Fields& f : fields)
            add(f);
    }

    template <typename T> Fields& add(const char* key, const T& value)
    {
        return add(Fields(key, value));
    }
    Fields& add(const Fields& more)
    {
        if (more.m_text.isEmpty()) // fieldIf() without a value: nothing, not even a space
            return *this;
        if (!m_text.isEmpty())
            m_text += u' ';
        m_text += more.m_text;
        return *this;
    }

    [[nodiscard]] bool isEmpty() const { return m_text.isEmpty(); }
    [[nodiscard]] const QString& text() const { return m_text; }

  private:
    QString m_text;
};

// A field only when there is something to say: fieldIf(!retryAfter.isEmpty(), "retry_after",
// retryAfter) writes nothing at all otherwise.
template <typename T> Fields fieldIf(bool present, const char* key, const T& value)
{
    return present ? Fields(key, value) : Fields();
}

// A value as a field writes it, e.g. an enum's name for log::Durations.
template <typename T> QString nameOf(const T& value) { return detail::value(value); }

// The categories of categories.h: lcNet, lcApi, ...
using Category = const QLoggingCategory& (*)();

// The record `event fields` at `level`, from the caller's file, line and function (warnings
// add them as src= and fn=). Nothing is written when the category has the level off.
void write(QtMsgType level, Category category, const char* event, const Fields& fields = {},
           std::source_location where = std::source_location::current());

// The same at a fixed level; write() for a level chosen at run time:
//   log::write(ok ? QtInfoMsg : QtWarningMsg, lcReport, "report.result", fields);
inline void debug(Category category, const char* event, const Fields& fields = {},
                  std::source_location where = std::source_location::current())
{
    write(QtDebugMsg, category, event, fields, where);
}
inline void info(Category category, const char* event, const Fields& fields = {},
                 std::source_location where = std::source_location::current())
{
    write(QtInfoMsg, category, event, fields, where);
}
inline void warn(Category category, const char* event, const Fields& fields = {},
                 std::source_location where = std::source_location::current())
{
    write(QtWarningMsg, category, event, fields, where);
}
inline void critical(Category category, const char* event, const Fields& fields = {},
                     std::source_location where = std::source_location::current())
{
    write(QtCriticalMsg, category, event, fields, where);
}

namespace detail
{
QStringList splitNames(const char* names);
}
} // namespace cirrus::log

// Names an enum in the log (field() finds logName() next to the enum). Put it in the
// enum's namespace, after the enum: CIRRUS_LOG_ENUM(TransportError, None, Timeout, ...).
// Listed enumerators log their own spelling, whatever their values; others a number.
#define CIRRUS_LOG_ENUM(Enum, ...)                                                                 \
    inline QString logName(Enum value)                                                             \
    {                                                                                              \
        using enum Enum;                                                                           \
        static constexpr Enum values[] = {__VA_ARGS__};                                            \
        static const QStringList names = ::cirrus::log::detail::splitNames(#__VA_ARGS__);          \
        for (qsizetype i = 0; i < qsizetype(std::size(values)); ++i)                               \
            if (values[i] == value)                                                                \
                return names.value(i);                                                             \
        return QString::number(qlonglong(value));                                                  \
    }
