#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>
#include <utility>

// Why a data source request failed; the backend's MusicError mapped to what the UI
// distinguishes (docs/ui/presentation.md §4.1). Canceled and stale requests never
// reply, so they have no kind here.
namespace cirrus::ui::failure
{
Q_NAMESPACE
QML_NAMED_ELEMENT(Failure)

// Rejected: provider rejection, invalid response or protocol failure.
// Invalid: a malformed request, i.e. a bug on our side.
enum Kind { Network, Timeout, Busy, RateLimited, SignedOut, Rejected, Invalid, Unknown };
Q_ENUM_NS(Kind)
} // namespace cirrus::ui::failure

namespace cirrus::ui
{

struct Failure {
    failure::Kind kind = failure::Unknown;
    std::optional<qint64> retryAfterMs;
};

// Result of a data source request: a value (possibly `partial`) or a failure.
template <class T> struct Outcome {
    std::optional<T> value;
    bool partial = false;
    std::optional<Failure> failure;
};

// Called at most once, never synchronously from the request call, never after the
// request was canceled or the source destroyed.
template <class T> using Reply = std::function<void(Outcome<T>)>;

// Handle to an in-flight request. Destroying or reassigning it cancels the request.
class Request
{
  public:
    Request() = default;
    explicit Request(std::function<void()> cancel) : m_cancel(std::move(cancel)) {}
    Request(Request&& other) noexcept : m_cancel(std::exchange(other.m_cancel, {})) {}
    Request& operator=(Request&& other) noexcept
    {
        if (this != &other) {
            cancel();
            m_cancel = std::exchange(other.m_cancel, {});
        }
        return *this;
    }
    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;
    ~Request() { cancel(); }

    void cancel()
    {
        if (auto cancel = std::exchange(m_cancel, {}))
            cancel();
    }
    // Forgets a request whose reply has run.
    void release() { m_cancel = {}; }
    [[nodiscard]] bool pending() const { return bool(m_cancel); }

  private:
    std::function<void()> m_cancel;
};

} // namespace cirrus::ui
