#include "backend_host.h"

#include "application/backend/backend_facade.h"
#include "infrastructure/runtime/backend_runtime.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>

Q_LOGGING_CATEGORY(lcBackendHost, "cirrus.ui.backend")

namespace cirrus::ui
{
namespace
{

// The app's directory name and keyring service (UQ-06, integration.md §6 D1).
const QString kDirectory = QStringLiteral("Cirrus");
const QString kKeychainService = QStringLiteral("com.guang233.Cirrus");
// How long to wait after the backend reported a delayed close before quitting anyway.
constexpr int kGraceMs = 5000;

// Makes `path` and leaves it readable by the owner only.
bool makePrivateDirectory(const QString& path)
{
    if (!QDir().mkpath(path))
        return false;
    return QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

} // namespace

std::optional<BackendHost::Paths> BackendHost::preparePaths(QString* error)
{
    const QString data =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u'/' + kDirectory;
    const QString cacheBase =
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + u'/' + kDirectory;
    const QString cache = cacheBase + QStringLiteral("/resources");
    const QString audio = cacheBase + QStringLiteral("/audio");
    for (const QString& path : {data, cache, audio}) {
        if (!makePrivateDirectory(path)) {
            if (error)
                *error = path;
            return std::nullopt;
        }
    }
    return Paths{data + QStringLiteral("/state.sqlite"), cache, kKeychainService, audio};
}

BackendHost::BackendHost(const Paths& paths, QObject* parent)
    : QObject(parent), m_runtime(std::make_unique<runtime::BackendRuntime>(runtime::BackendConfig{
                           .databasePath = paths.database,
                           .keychainService = paths.keychainService,
                           .persistListeningState = true,
                           .resourceCacheRoot = paths.cache,
                           .audioCacheRoot = paths.audioCache,
                       }))
{
    auto& facade = m_runtime->facade();
    connect(&facade, &application::BackendFacade::started, this,
            [](application::StartupError error) {
                if (error == application::StartupError::None)
                    qCInfo(lcBackendHost) << "backend ready";
                else if (error == application::StartupError::GuestBootstrap)
                    qCWarning(lcBackendHost) << "backend ready without guest access";
                else
                    qCWarning(lcBackendHost) << "backend failed to start:" << int(error);
            });
    connect(&facade, &application::BackendFacade::persistenceWarning, this,
            [](application::PersistenceArea area) {
                qCWarning(lcBackendHost) << "keyring could not save" << int(area);
            });
    connect(&facade, &application::BackendFacade::stopped, this, [this](bool persistenceFailed) {
        if (persistenceFailed)
            qCWarning(lcBackendHost) << "backend stopped; some records were not saved";
        finish();
    });
    connect(&facade, &application::BackendFacade::closeDelayed, this, [this] {
        qCWarning(lcBackendHost) << "backend close is taking long (keyring?)";
        QTimer::singleShot(kGraceMs, this, [this] {
            if (m_done) {
                qCWarning(lcBackendHost) << "quitting before the backend stopped";
                finish();
            }
        });
    });
}

BackendHost::~BackendHost() = default;

void BackendHost::start() { m_runtime->facade().start(); }

void BackendHost::shutdown(std::function<void()> done)
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    m_done = std::move(done);
    const auto state = m_runtime->facade().state();
    if (state == application::BackendState::Stopped) {
        finish();
        return;
    }
    m_runtime->facade().close();
}

void BackendHost::finish()
{
    if (auto done = std::exchange(m_done, {}))
        done();
}

} // namespace cirrus::ui
