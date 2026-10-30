#include "qr_provider.h"

#include "session.h"
#include "sources.h"

namespace cirrus::ui
{

QrProvider::QrProvider(const Session* session)
    : QQuickImageProvider(QQuickImageProvider::Image), m_session(session)
{
}

QImage QrProvider::requestImage(const QString&, QSize* size, const QSize& requestedSize)
{
    QImage image;
    if (const SessionSource* source = m_session->source())
        image = source->snapshot().qrImage;
    if (!image.isNull() && requestedSize.isValid())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::FastTransformation);
    if (size)
        *size = image.size();
    return image;
}

} // namespace cirrus::ui
