#pragma once
#include "domain/account/session_types.h"
#include "domain/music/together_types.h"
#include <QObject>
namespace cirrus::application
{
// Explicit HTTP requests only. No IM, timers, local player or queue changes.
class ITogetherGateway : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual quint64 submit(domain::SessionToken context, QString accountId,
                           domain::TogetherRequest request) = 0;
    virtual void cancel(quint64 id) = 0;
  signals:
    void completed(quint64 id, cirrus::domain::SessionToken context,
                   cirrus::domain::TogetherResult result);
};
} // namespace cirrus::application
