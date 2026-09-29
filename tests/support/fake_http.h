#pragma once

#include <QHttpHeaders>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <cstring>

namespace cirrus::test
{

// Synthetic data only. No socket, DNS, TLS, credentials or external service.
struct Plan {
    QByteArray body = "ok";
    int status = 200;
    int delayMs = 0; // negative means never completes without abort
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QList<std::pair<QByteArray, QByteArray>> headers{{"Retry-After", "3"}};
};

class FakeReply : public QNetworkReply
{
  public:
    FakeReply(const QNetworkRequest& request, Plan plan, QObject* parent) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        if (plan.delayMs >= 0) {
            QTimer::singleShot(plan.delayMs, this, [this, plan] {
                if (isFinished()) {
                    return;
                }
                m_body = plan.body;
                setAttribute(QNetworkRequest::HttpStatusCodeAttribute, plan.status);
                QHttpHeaders headers;
                for (const auto& [name, value] : plan.headers) {
                    headers.append(name, value);
                }
                setHeaders(std::move(headers));
                if (plan.error != QNetworkReply::NoError) {
                    setError(plan.error, QStringLiteral("synthetic failure"));
                }
                emit readyRead();
                if (!isFinished()) {
                    setFinished(true);
                    emit finished();
                }
            });
        }
    }
    void abort() override
    {
        if (!isFinished()) {
            setError(OperationCanceledError, QStringLiteral("canceled"));
            setFinished(true);
            emit finished(); // deliberately synchronous to exercise reentrancy
        }
    }
    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QIODevice::bytesAvailable();
    }

  protected:
    qint64 readData(char* buffer, qint64 maximum) override
    {
        const qint64 count = qMin(maximum, static_cast<qint64>(m_body.size() - m_offset));
        if (count <= 0) {
            return 0;
        }
        std::memcpy(buffer, m_body.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

  private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class FakeManager : public QNetworkAccessManager
{
  public:
    QList<Plan> plans;
    QList<QNetworkRequest> requests;
    QList<QByteArray> bodies;

  protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                 QIODevice* outgoing) override
    {
        Q_ASSERT(operation == PostOperation);
        requests.append(request);
        bodies.append(outgoing ? outgoing->readAll() : QByteArray{});
        return new FakeReply(request, plans.isEmpty() ? Plan{} : plans.takeFirst(), this);
    }
};

} // namespace cirrus::test
