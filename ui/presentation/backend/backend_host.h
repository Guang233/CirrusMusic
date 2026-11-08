#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

// Owns the backend (BackendRuntime) for the app: where its files live, starting it,
// and closing it before the app quits (docs/ui/integration.md §3). The backend
// sources (UI phase 5) are made from its runtime.
class BackendHost : public QObject
{
    Q_OBJECT

  public:
    struct Paths {
        // The installation database (account records, device identity).
        QString database;
        // The resource cache (covers, song details, lyrics): a private directory.
        QString cache;
        QString keychainService;
        // The audio cache (played songs, ADR-0008): a private directory.
        QString audioCache;
    };
    // The app's directories (Cirrus, com.guang233.Cirrus), created private (0700);
    // nullopt with `error` set when they cannot be made.
    static std::optional<Paths> preparePaths(QString* error);

    explicit BackendHost(const Paths& paths, QObject* parent = nullptr);
    ~BackendHost() override;

    [[nodiscard]] runtime::BackendRuntime& runtime() const { return *m_runtime; }

    void start();
    // Closes the backend (it never signs out); `done` runs once it has stopped, or
    // when it still hasn't some seconds after reporting the delay (a keyring that
    // never answers, integration.md §6 D6). Runs `done` at most once.
    void shutdown(std::function<void()> done);

  private:
    void finish();

    std::unique_ptr<runtime::BackendRuntime> m_runtime;
    std::function<void()> m_done;
    bool m_shuttingDown = false;
};

} // namespace cirrus::ui
