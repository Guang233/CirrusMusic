#pragma once
#include "domain/account/session_types.h"
#include "domain/music/collection_types.h"
#include <QObject>
namespace cirrus::application
{
// Dedicated explicit write port, never a MusicQuery/read or an automatic follow-up.
class ICollectionGateway : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual quint64 submit(domain::SessionToken context, QString accountId,
                           domain::CollectionMutation mutation) = 0;
    virtual void cancel(quint64 id) = 0;
  signals:
    void completed(quint64 id, cirrus::domain::SessionToken context,
                   cirrus::domain::CollectionResult result);
};
} // namespace cirrus::application
