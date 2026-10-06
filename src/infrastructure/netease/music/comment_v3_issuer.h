#pragma once
#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <memory>
namespace cirrus::netease
{
enum class CommentTokenError { None, InvalidResponse, Network, Timeout, Canceled, Busy };
struct CommentTokenResult {
    CommentTokenError error{};
    QByteArray token;
}; // secret, infra only
[[nodiscard]] QByteArray commentTokenFrom(QByteArray body);
class CommentV3Issuer final : public QObject
{
    Q_OBJECT
  public:
    explicit CommentV3Issuer(std::unique_ptr<QNetworkAccessManager> manager = {},
                             QObject* parent = nullptr);
    ~CommentV3Issuer() override;
    quint64 acquire();
    void cancel(quint64 id);
  signals:
    void completed(quint64 id, cirrus::netease::CommentTokenResult result);

  private:
    struct Pending;
    void start(quint64 id);
    void consume(quint64 id);
    void finish(quint64 id, CommentTokenError error = CommentTokenError::None);
    QNetworkAccessManager* m_manager;
    QHash<quint64, std::shared_ptr<Pending>> m_pending;
    quint64 m_nextId = 0;
};
} // namespace cirrus::netease
