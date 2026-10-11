#if defined(_WIN32)
#include "native_media_platform.h"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QUuid>
#include <atomic>
#include <limits>
#include <roapi.h>
#include <systemmediatransportcontrolsinterop.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>

namespace cirrus::infrastructure::system_media
{
namespace
{
using namespace winrt::Windows::Media;
using winrt::Windows::Foundation::TimeSpan;
std::atomic<bool> claimed{false}; // only one of OUR providers per process, not authentication
std::optional<qint64> ticks(std::optional<qint64> value)
{
    if (!value || *value < 0 || *value > std::numeric_limits<qint64>::max() / 10000)
        return {};
    return *value * 10000; // checked ms -> Windows 100ns TimeSpan, never an authoritative clock
}
winrt::hstring text(const QString& value) { return winrt::hstring(value.toStdWString()); }
class WindowsSmtcPlatform final : public NativeMediaPlatform, public QAbstractNativeEventFilter
{
  public:
    ~WindowsSmtcPlatform() override { close(); }
    bool start(const NativeMediaConfig& config, NativeMediaInbox::Callback callback,
               Lost lost) override
    {
        if (m_started || !config.enabled || config.ownedTopLevelWindow == 0 ||
            !QCoreApplication::instance() ||
            QCoreApplication::testAttribute(Qt::AA_PluginApplication))
            return false;
        m_started = true;
        m_thread = GetCurrentThreadId();
        m_hwnd = reinterpret_cast<HWND>(config.ownedTopLevelWindow);
        DWORD process = 0;
        if (!IsWindow(m_hwnd) || GetAncestor(m_hwnd, GA_ROOT) != m_hwnd ||
            GetWindowThreadProcessId(m_hwnd, &process) != m_thread ||
            process != GetCurrentProcessId() ||
            (GetWindowLongPtrW(m_hwnd, GWL_STYLE) & WS_CHILD) != 0)
            return false; // no hidden/foreign/helper HWND or GetForCurrentView fallback
        bool available = false;
        if (!claimed.compare_exchange_strong(available, true))
            return false;
        m_claim = true;
        m_key =
            (QStringLiteral("Cirrus.SMTC.") + QUuid::createUuid().toString(QUuid::WithoutBraces))
                .toStdWString();
        if (!SetPropW(m_hwnd, m_key.c_str(), reinterpret_cast<HANDLE>(this)))
            return false;
        m_pinned = true; // property disappears on HWND destruction; detects numeric HWND reuse
        m_live = true;
        m_lost = std::move(lost);
        QCoreApplication::instance()->installNativeEventFilter(this);
        m_filter = true;
        const HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
        if (FAILED(initialized))
            return false;   // existing incompatible apartment fails; never silently change it
        m_apartment = true; // S_OK AND S_FALSE require the matching RoUninitialize
        try {
            auto factory = winrt::get_activation_factory<SystemMediaTransportControls>();
            auto interop = factory.as<ISystemMediaTransportControlsInterop>();
            winrt::check_hresult(
                interop->GetForWindow(m_hwnd, winrt::guid_of<SystemMediaTransportControls>(),
                                      winrt::put_abi(m_controls)));
            if (!current())
                return false;
            m_display = m_controls.DisplayUpdater();
            if (!clear())
                return false;
            m_button = m_controls.ButtonPressed(
                [callback](const auto&,
                           const SystemMediaTransportControlsButtonPressedEventArgs& args) {
                    // Never capture the adapter/QObject. WinRT may deliver after revocation.
                    try {
                        NativeMediaRequest request;
                        switch (args.Button()) {
                        case SystemMediaTransportControlsButton::Play:
                            request.action = NativeMediaAction::Play;
                            break;
                        case SystemMediaTransportControlsButton::Pause:
                            request.action = NativeMediaAction::Pause;
                            break;
                        case SystemMediaTransportControlsButton::Stop:
                            request.action = NativeMediaAction::Stop;
                            break;
                        case SystemMediaTransportControlsButton::Next:
                            request.action = NativeMediaAction::Next;
                            break;
                        case SystemMediaTransportControlsButton::Previous:
                            request.action = NativeMediaAction::Previous;
                            break;
                        default:
                            return; // record/rewind/fast-forward/channel/etc unsupported
                        }
                        callback(request); // bounded queue admission only, never native playback
                    } catch (
                        ...) { /* malformed/native failure: no command, no provider error export */
                    }
                });
            m_hasButton = true;
            m_seek = m_controls.PlaybackPositionChangeRequested(
                [callback](const auto&, const PlaybackPositionChangeRequestedEventArgs& args) {
                    try {
                        const auto value = args.RequestedPlaybackPosition().count();
                        if (value >= 0)
                            callback({NativeMediaAction::Seek, qint64(value / 10000)});
                    } catch (...) { /* fail closed; no callback authority from historical state */
                    }
                });
            m_hasSeek = true;
            return current();
        } catch (...) {
            return false;
        }
    }
    bool current() const override
    {
        DWORD process = 0;
        return !m_closed && m_controls && m_live && m_pinned && GetCurrentThreadId() == m_thread &&
               IsWindow(m_hwnd) && GetAncestor(m_hwnd, GA_ROOT) == m_hwnd &&
               GetWindowThreadProcessId(m_hwnd, &process) == m_thread &&
               process == GetCurrentProcessId() &&
               GetPropW(m_hwnd, m_key.c_str()) ==
                   reinterpret_cast<HANDLE>(const_cast<WindowsSmtcPlatform*>(this));
    }
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
    {
        const auto* event = static_cast<const MSG*>(message);
        if (m_live && event && event->hwnd == m_hwnd && event->message == WM_NCDESTROY) {
            m_live = false;
            if (m_lost)
                m_lost(); // disarm now, cleanup queued until this native dispatch unwinds
        }
        return false; // do not steal media keys or suppress the original window dispatch
    }
    NativeProjectionResult publish(const domain::PlaybackObservation& s,
                                   const NativeMediaConfig& config, const Permit& permit) override
    {
        NativeProjectionResult result = NativeProjectionResult::Published;
        const auto put = [&](auto operation) {
            if (result != NativeProjectionResult::Published)
                return;
            if (!current() || !permit()) {
                result = NativeProjectionResult::Retired;
                return;
            }
            operation(); // every native field admission is preceded by a live private fence
        };
        try {
            const auto& c = s.capabilities;
            put([&] { m_controls.IsEnabled(false); });
            put([&] { m_display.ClearAll(); });
            put([&] { m_display.Type(MediaPlaybackType::Music); });
            if (config.exportMetadata &&
                s.metadataStatus == domain::ObservationContentStatus::Ready && s.metadata) {
                const auto properties = m_display.MusicProperties();
                put([&] { properties.Title(text(s.metadata->title)); });
                put([&] {
                    properties.Artist(text(s.metadata->artists.join(QStringLiteral(", "))));
                });
                if (s.metadata->album)
                    put([&] { properties.AlbumTitle(text(*s.metadata->album)); });
            }
            put([&] { m_display.Update(); }); // no thumbnail/URI/artwork/title lookup
            const auto length = ticks(s.durationMs), position = ticks(s.positionMs);
            const bool seekable = config.allowControls && c.seek && length && *length > 0 &&
                                  position && *position <= *length;
            SystemMediaTransportControlsTimelineProperties timeline;
            timeline.StartTime(TimeSpan{0});
            timeline.EndTime(TimeSpan{length.value_or(0)});
            timeline.MinSeekTime(TimeSpan{0});
            timeline.MaxSeekTime(TimeSpan{seekable ? *length : 0});
            timeline.Position(TimeSpan{length && position && *position <= *length ? *position : 0});
            put([&] { m_controls.UpdateTimelineProperties(timeline); });
            put([&] { m_controls.PlaybackRate(1.0); });
            auto status = MediaPlaybackStatus::Stopped;
            if (s.state == domain::PlaybackState::Playing)
                status = MediaPlaybackStatus::Playing;
            else if (s.state == domain::PlaybackState::Paused)
                status = MediaPlaybackStatus::Paused;
            put([&] { m_controls.PlaybackStatus(status); });
            put([&] { m_controls.IsPlayEnabled(config.allowControls && c.play); });
            put([&] { m_controls.IsPauseEnabled(config.allowControls && c.pause); });
            put([&] { m_controls.IsStopEnabled(config.allowControls && c.stop); });
            put([&] { m_controls.IsNextEnabled(config.allowControls && c.next); });
            put([&] { m_controls.IsPreviousEnabled(config.allowControls && c.previous); });
            put([&] { m_controls.IsEnabled(true); });
        } catch (...) {
            result = NativeProjectionResult::Failed;
        }
        return result;
    }
    bool clear() noexcept override
    {
        if (!m_controls)
            return true;
        bool ok = true;
        const auto attempt = [&](auto operation) {
            try {
                operation();
            } catch (...) {
                ok = false;
            }
        };
        attempt([&] { m_controls.IsEnabled(false); });
        attempt([&] { m_controls.IsPlayEnabled(false); });
        attempt([&] { m_controls.IsPauseEnabled(false); });
        attempt([&] { m_controls.IsStopEnabled(false); });
        attempt([&] { m_controls.IsNextEnabled(false); });
        attempt([&] { m_controls.IsPreviousEnabled(false); });
        attempt([&] { m_controls.IsFastForwardEnabled(false); });
        attempt([&] { m_controls.IsRewindEnabled(false); });
        attempt([&] { m_controls.IsRecordEnabled(false); });
        attempt([&] { m_controls.PlaybackStatus(MediaPlaybackStatus::Stopped); });
        attempt([&] {
            SystemMediaTransportControlsTimelineProperties timeline;
            timeline.StartTime(TimeSpan{0});
            timeline.EndTime(TimeSpan{0});
            timeline.MinSeekTime(TimeSpan{0});
            timeline.MaxSeekTime(TimeSpan{0});
            timeline.Position(TimeSpan{0});
            m_controls.UpdateTimelineProperties(timeline);
        });
        if (m_display) {
            attempt([&] { m_display.ClearAll(); });
            attempt([&] { m_display.Update(); });
        }
        return ok; // already admitted OS/consumer caches cannot be physically revoked
    }
    bool close() noexcept override
    {
        if (m_closed)
            return m_cleanupOk;
        m_closed = true;
        bool ok = clear();
        try {
            if (m_hasButton) {
                m_controls.ButtonPressed(m_button);
                m_hasButton = false;
            }
        } catch (...) {
            ok = false;
        }
        try {
            if (m_hasSeek) {
                m_controls.PlaybackPositionChangeRequested(m_seek);
                m_hasSeek = false;
            }
        } catch (...) {
            ok = false;
        }
        if (m_filter && QCoreApplication::instance())
            QCoreApplication::instance()->removeNativeEventFilter(this);
        m_filter = false;
        if (m_pinned && GetPropW(m_hwnd, m_key.c_str()) == reinterpret_cast<HANDLE>(this))
            if (RemovePropW(m_hwnd, m_key.c_str()) != reinterpret_cast<HANDLE>(this))
                ok = false;
        m_pinned = m_live = false;
        m_display = nullptr;
        m_controls = nullptr;
        if (m_apartment)
            RoUninitialize();
        m_apartment = false;
        if (m_claim && ok)
            claimed.store(false); // failed revocation never authorizes a replacement provider
        m_claim = false;
        m_cleanupOk = ok;
        return ok;
    }

  private:
    SystemMediaTransportControls m_controls{nullptr};
    SystemMediaTransportControlsDisplayUpdater m_display{nullptr};
    winrt::event_token m_button{}, m_seek{};
    HWND m_hwnd = nullptr;
    DWORD m_thread = 0;
    std::wstring m_key;
    Lost m_lost;
    bool m_started = false, m_closed = false, m_cleanupOk = true, m_claim = false,
         m_apartment = false;
    bool m_filter = false, m_pinned = false, m_live = false, m_hasButton = false, m_hasSeek = false;
};
} // namespace
std::shared_ptr<NativeMediaPlatform> createWindowsSmtcPlatform()
{
    return std::make_shared<WindowsSmtcPlatform>();
}
} // namespace cirrus::infrastructure::system_media
#endif
