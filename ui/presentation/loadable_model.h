#pragma once

#include "load_state.h"
#include "request.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// What went wrong, for the page to show (`model.error.message`). Writable so QML can
// build one, e.g. `({message: "…", retryable: true})` in the gallery.
class LoadError
{
    Q_GADGET
    QML_VALUE_TYPE(loadError)
    QML_STRUCTURED_VALUE
    Q_PROPERTY(int kind MEMBER kind)           // Failure.Kind, -1 for none
    Q_PROPERTY(QString message MEMBER message) // translated
    Q_PROPERTY(bool retryable MEMBER retryable)
    Q_PROPERTY(qint64 retryAfterMs MEMBER retryAfterMs) // 0 when not given

  public:
    int kind = -1;
    QString message;
    bool retryable = false;
    qint64 retryAfterMs = 0;
    friend bool operator==(const LoadError&, const LoadError&) = default;
};

// Base of loading ViewModels: one request at a time, the shared state machine and
// the failure mapping of docs/ui/presentation.md §4.1. Subclasses start their request
// in load() and report through handle(). A page in Offline retries once the network
// comes back.
class LoadableModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Base of the page models")
    Q_PROPERTY(int state READ state NOTIFY stateChanged) // LoadState
    // Reloading while content stays on screen; `state` keeps its value meanwhile.
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY refreshingChanged)
    Q_PROPERTY(cirrus::ui::LoadError error READ error NOTIFY errorChanged)

  public:
    explicit LoadableModel(QObject* parent = nullptr);

    [[nodiscard]] int state() const { return m_state; }
    [[nodiscard]] bool refreshing() const { return m_refreshing; }
    [[nodiscard]] LoadError error() const { return m_error; }

    // Repeats the last request.
    Q_INVOKABLE void retry();
    // Reloads, keeping the current content visible.
    Q_INVOKABLE void refresh();

  signals:
    void stateChanged();
    void refreshingChanged();
    void errorChanged();

  protected:
    // Starts the request for the current inputs; replies go through handle().
    virtual Request load() = 0;
    // Clears the content when a new input (key) replaces it.
    virtual void clear() = 0;

    // Starts loading from scratch (new input): clears content, then Loading.
    void reload();
    // Back to Idle with no content and no request (empty input).
    void reset();

    // Reply adapter: `apply(value)` stores the content and returns whether it is empty.
    template <class T, class Apply> Reply<T> handle(Apply apply)
    {
        return [this, apply = std::move(apply)](Outcome<T> outcome) {
            m_request.release();
            if (outcome.failure)
                fail(*outcome.failure);
            else
                succeed(apply(std::move(*outcome.value)), outcome.partial);
        };
    }

  private:
    void start();
    void succeed(bool empty, bool partial);
    void fail(const Failure& failure);
    void setState(int state);
    void setRefreshing(bool refreshing);
    void setError(const LoadError& error);
    [[nodiscard]] bool hasContent() const;

    Request m_request;
    int m_state = loadstate::Idle;
    bool m_refreshing = false;
    LoadError m_error;
};

} // namespace cirrus::ui
