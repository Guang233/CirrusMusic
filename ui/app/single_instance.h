#pragma once

#include <QObject>

#include <memory>

class QLocalServer;
class QLocalSocket;
class QLockFile;

namespace cirrus::ui
{

// One running app per user (docs/10-linux-integration.md §7): a lock file and a local
// socket in a private directory under XDG_RUNTIME_DIR. A second launch asks the
// running app to show its window and exits. The only message is
//   activate [<xdg-activation-token>]\n
// so nothing else can be passed in.
class SingleInstance : public QObject
{
    Q_OBJECT

  public:
    explicit SingleInstance(QObject* parent = nullptr);
    ~SingleInstance() override;

    // True when this process should run: it holds the lock and listens, or the lock's
    // owner does not answer (a stuck lock never blocks starting). False when the
    // running app took the activation; this process should exit.
    [[nodiscard]] bool claim();

  signals:
    // Another launch wants the window. `token` lets Wayland raise it; may be empty.
    void activateRequested(const QString& token);

  private:
    void listen(const QString& name);
    void read(QLocalSocket* socket);

    std::unique_ptr<QLockFile> m_lock;
    QLocalServer* m_server = nullptr;
};

} // namespace cirrus::ui
