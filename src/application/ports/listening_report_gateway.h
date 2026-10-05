#pragma once
#include "domain/account/session_types.h"
#include "domain/playback/listening_report.h"
#include <QObject>

namespace cirrus::application
{
// Same-thread deferred completions. Each request is ONE write, no retries or trigger policy.
class IListeningReportGateway : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual quint64 submit(domain::SessionToken context, QString accountId,
                           domain::ListeningReport report) = 0;
    virtual void
    cancel(quint64 id) = 0; // cancellation never proves that a dispatched write was undone
  signals:
    void completed(quint64 id, cirrus::domain::SessionToken context,
                   cirrus::domain::ListeningReportResult result);
};
} // namespace cirrus::application
