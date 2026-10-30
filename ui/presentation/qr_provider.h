#pragma once

#include <QQuickImageProvider>

namespace cirrus::ui
{

class Session;

// `image://qr/<serial>`: the current sign-in code from the session snapshot, scaled
// without smoothing so modules stay sharp. Older serials get the current code.
class QrProvider : public QQuickImageProvider
{
  public:
    explicit QrProvider(const Session* session);
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

  private:
    const Session* m_session;
};

} // namespace cirrus::ui
