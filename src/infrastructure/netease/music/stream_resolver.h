#pragma once
#include "application/ports/playback.h"
#include "infrastructure/netease/api/session_api_client.h"
#include <QElapsedTimer>

namespace cirrus::netease
{
[[nodiscard]] application::StreamResult mapStreamResponse(const domain::StreamQuery& query,
                                                          const QJsonObject& object,
                                                          qint64 requestElapsedMs = 0);
class StreamResolver final : public application::IStreamResolver
{
    Q_OBJECT
  public:
    explicit StreamResolver(SessionApiClient& client, QObject* parent = nullptr);
    ~StreamResolver() override;
    quint64 resolve(domain::SessionToken context, domain::StreamQuery query) override;
    void cancel(quint64 id) override;

  private:
    struct Pending {
        domain::SessionToken context;
        domain::StreamQuery query;
        QElapsedTimer age;
    };
    SessionApiClient& m_client;
    QHash<quint64, Pending> m_pending;
};
} // namespace cirrus::netease
