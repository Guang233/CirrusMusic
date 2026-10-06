#pragma once
#include "infrastructure/netease/api/session_api_client.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <memory>
namespace cirrus::netease
{
// All three fields are provider capabilities, infrastructure-only and never logged.
struct CoverAllocation {
    QString objectKey;
    QByteArray token;
    QString imageId;
};
[[nodiscard]] std::optional<CoverAllocation> coverAllocation(const ApiResult& result);
[[nodiscard]] QByteArray prepareCoverJpeg(const QByteArray& input);
class CoverUpload final : public QObject
{
    Q_OBJECT
  public:
    explicit CoverUpload(std::unique_ptr<QNetworkAccessManager> manager = {},
                         QObject* parent = nullptr);
    ~CoverUpload() override;
    quint64 post(CoverAllocation allocation, QByteArray jpeg);
    void cancel(quint64 id);
  signals:
    void completed(quint64 id, cirrus::netease::ApiResult result);

  private:
    struct Pending;
    void start(quint64 id);
    void consume(quint64 id);
    void finish(quint64 id, ApiError error = ApiError::None,
                TransportError transport = TransportError::None);
    QNetworkAccessManager* m_manager;
    QHash<quint64, std::shared_ptr<Pending>> m_pending;
    quint64 m_nextId = 0;
};
} // namespace cirrus::netease
