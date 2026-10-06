#pragma once
#include "fake_http.h"
#include <QBuffer>
#include <QImage>
namespace cirrus::test
{
inline QByteArray syntheticCover(int width = 8, int height = 8)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(qRgb(13, 29, 47));
    image.setText(QStringLiteral("synthetic-private"), QStringLiteral("synthetic metadata"));
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    return image.save(&buffer, "JPEG", 85) ? data : QByteArray{};
}
class SyntheticImageManager : public QNetworkAccessManager
{
  public:
    QList<Plan> plans;
    QList<QNetworkRequest> requests;
    QList<QByteArray> bodies;

  protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request,
                                 QIODevice* outgoing) override
    {
        Q_ASSERT(op == PostOperation);
        requests.append(request);
        bodies.append(outgoing ? outgoing->readAll() : QByteArray{});
        auto plan = plans.isEmpty() ? Plan{"auto"} : plans.takeFirst();
        if (plan.body == "auto")
            plan.body =
                "{\"code\":200,\"size\":\"" + QByteArray::number(bodies.last().size()) + "\"}";
        return new FakeReply(request, plan, this);
    }
};
} // namespace cirrus::test
