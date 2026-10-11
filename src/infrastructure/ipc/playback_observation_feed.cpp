#include "playback_observation_feed.h"
#include <QThread>
#include <utility>

namespace cirrus::infrastructure::ipc
{
using namespace domain;
PlaybackObservationFeed::PlaybackObservationFeed(
    application::PlaybackObservationPublisher& publisher, QObject* parent)
    : QObject(parent), m_publisher(&publisher)
{
    // Fresh source is retired before first sample; no startup value is authorized.
    m_mailbox->retire({});
    if (publisher.thread() != thread()) {
        m_closed = true;
        return;
    }
    m_barrier = connect(&publisher, &application::PlaybackObservationPublisher::contextRetired,
                        this, &PlaybackObservationFeed::retire, Qt::DirectConnection);
    m_sample = connect(
        &publisher, &application::PlaybackObservationPublisher::changed, this,
        [this] { refreshFromOwner(); }, Qt::DirectConnection);
    m_destroyed = connect(&publisher, &QObject::destroyed, this, &PlaybackObservationFeed::close,
                          Qt::DirectConnection);
}
bool PlaybackObservationFeed::setEnabled(bool enabled)
{
    if (QThread::currentThread() != thread() || m_closed || !m_publisher)
        return false;
    if (enabled == m_enabled)
        return true;
    m_enabled = enabled;
    if (enabled)
        refreshFromOwner(); // live private factory, never a historical changed argument
    else
        m_publisher->retireExports(); // direct barrier now, even before any coherent flush
    return true;
}
void PlaybackObservationFeed::requestWake()
{
    if (m_wake->request())
        emit wakeRequested();
}
void PlaybackObservationFeed::retire(ObservationBinding previous)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed)
        return;
    m_document.reset();
    m_view.reset();
    m_mailbox->retire(previous);
    requestWake();
}
void PlaybackObservationFeed::refreshFromOwner()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed || !m_enabled || !m_publisher)
        return;
    auto current = m_publisher->validatedExport();
    if (!current)
        return; // no coherent live value yet; private retirement uses the synchronous seam
    const auto binding = current->snapshot.binding;
    const bool begin = !m_view || m_view->instanceId != binding.instanceId ||
                       m_view->viewEpoch != binding.viewEpoch;
    if (begin && !m_mailbox->beginView(binding))
        return;
    m_view = binding;
    if (!current->document)
        m_document.reset();
    else if (!m_document || m_document->binding != current->document->binding ||
             m_document->revision != current->document->revision)
        m_document = std::make_shared<const PlaybackLyricDocument>(std::move(*current->document));
    const bool published = m_mailbox->publish(std::move(current->snapshot), m_document);
    if (begin || published)
        requestWake();
}
void PlaybackObservationFeed::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed)
        return;
    m_closed = true;
    m_enabled = false;
    disconnect(m_sample);
    disconnect(m_barrier);
    disconnect(m_destroyed);
    m_document.reset();
    m_mailbox->retire(m_view.value_or(ObservationBinding{}));
    m_view.reset();
    requestWake(); // terminal scalar wake survives through pinned mailbox/ticket ownership
}
} // namespace cirrus::infrastructure::ipc
