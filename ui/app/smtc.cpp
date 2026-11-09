#include "smtc.h"
#include "logging/log.h"

#include "media_state.h"
#include "player.h"

#include <QWindow>

#include <chrono>

#include <roapi.h>
#include <systemmediatransportcontrolsinterop.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

namespace cirrus::ui
{
namespace
{
using namespace winrt::Windows::Media;
using winrt::Windows::Foundation::TimeSpan;
using winrt::Windows::Foundation::Uri;
using winrt::Windows::Storage::Streams::RandomAccessStreamReference;

// The timeline the flyout shows drifts; refresh it this often while playing.
constexpr int kTickMs = 5000;
// The thumbnail's size (the lock screen shows it largest).
constexpr int kCoverSize = 300;

winrt::hstring text(const QString& value)
{
    return winrt::hstring(reinterpret_cast<const wchar_t*>(value.utf16()),
                          winrt::hstring::size_type(value.size()));
}

TimeSpan span(qint64 ms) { return std::chrono::milliseconds(std::max<qint64>(ms, 0)); }

qint64 msOf(TimeSpan value)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(value).count();
}

MediaPlaybackAutoRepeatMode repeatMode(int repeat)
{
    switch (repeat) {
    case playback::RepeatAll:
        return MediaPlaybackAutoRepeatMode::List;
    case playback::RepeatOne:
        return MediaPlaybackAutoRepeatMode::Track;
    default:
        return MediaPlaybackAutoRepeatMode::None;
    }
}
} // namespace

struct Smtc::Native {
    SystemMediaTransportControls controls{nullptr};
    winrt::event_token button{}, position{}, shuffle{}, repeat{};
    std::shared_ptr<media::Mailbox> mailbox;
    bool apartment = false;
    bool shownOnce = false;
    media::State shown; // what the display shows (metadata only)
};

Smtc::Smtc(Player* player, QWindow* window, QObject* parent) : QObject(parent), m_player(player)
{
    auto native = std::make_unique<Native>();
    // Qt's GUI thread is already a single-threaded apartment (S_FALSE); RoUninitialize
    // must still match. A different apartment fails here but WinRT works in it anyway.
    native->apartment = SUCCEEDED(RoInitialize(RO_INIT_SINGLETHREADED));
    try {
        auto factory = winrt::get_activation_factory<SystemMediaTransportControls>();
        auto interop = factory.as<ISystemMediaTransportControlsInterop>();
        winrt::check_hresult(interop->GetForWindow(reinterpret_cast<HWND>(window->winId()),
                                                   winrt::guid_of<SystemMediaTransportControls>(),
                                                   winrt::put_abi(native->controls)));
        auto& controls = native->controls;
        auto mailbox = native->mailbox = std::make_shared<media::Mailbox>(this);

        // WinRT raises these on its own threads: hand them to ours.
        native->button = controls.ButtonPressed(
            [this, mailbox](const auto&,
                            const SystemMediaTransportControlsButtonPressedEventArgs& args) {
                try {
                    switch (args.Button()) {
                    case SystemMediaTransportControlsButton::Play:
                        mailbox->post([this] { media::play(*m_player); });
                        break;
                    case SystemMediaTransportControlsButton::Pause:
                    case SystemMediaTransportControlsButton::Stop:
                        mailbox->post([this] { media::pause(*m_player); });
                        break;
                    case SystemMediaTransportControlsButton::Next:
                        mailbox->post([this] { m_player->next(); });
                        break;
                    case SystemMediaTransportControlsButton::Previous:
                        mailbox->post([this] { m_player->previous(); });
                        break;
                    default:
                        break;
                    }
                } catch (...) {
                }
            });
        native->position = controls.PlaybackPositionChangeRequested(
            [this, mailbox](const auto&, const PlaybackPositionChangeRequestedEventArgs& args) {
                try {
                    const qint64 ms = msOf(args.RequestedPlaybackPosition());
                    mailbox->post([this, ms] { media::seek(*m_player, ms); });
                } catch (...) {
                }
            });
        native->shuffle = controls.ShuffleEnabledChangeRequested(
            [this, mailbox](const auto&, const ShuffleEnabledChangeRequestedEventArgs& args) {
                try {
                    const bool on = args.RequestedShuffleEnabled();
                    mailbox->post([this, on] {
                        m_player->setOrder(on ? playback::Shuffle : playback::Sequential);
                    });
                } catch (...) {
                }
            });
        native->repeat = controls.AutoRepeatModeChangeRequested(
            [this, mailbox](const auto&, const AutoRepeatModeChangeRequestedEventArgs& args) {
                try {
                    int repeat = playback::RepeatOff;
                    if (args.RequestedAutoRepeatMode() == MediaPlaybackAutoRepeatMode::List)
                        repeat = playback::RepeatAll;
                    else if (args.RequestedAutoRepeatMode() == MediaPlaybackAutoRepeatMode::Track)
                        repeat = playback::RepeatOne;
                    mailbox->post([this, repeat] { m_player->setRepeat(repeat); });
                } catch (...) {
                }
            });
        controls.IsStopEnabled(false);
        controls.IsFastForwardEnabled(false);
        controls.IsRewindEnabled(false);
        controls.IsEnabled(true);
    } catch (const winrt::hresult_error& error) {
        qCWarning(lcPlatform) << log::event("smtc.unavailable")
                              << log::field("hresult", qint64(error.code().value));
        native->controls = nullptr;
    }
    if (!native->controls) {
        if (native->apartment)
            RoUninitialize();
        return;
    }
    m_native = std::move(native);

    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &Smtc::update);
    connect(m_player, &Player::changed, this, &Smtc::update);
    connect(m_player, &Player::currentChanged, this, &Smtc::update);
    update();
}

Smtc::~Smtc()
{
    if (!m_native)
        return;
    m_native->mailbox->close();
    auto& controls = m_native->controls;
    try {
        controls.ButtonPressed(m_native->button);
        controls.PlaybackPositionChangeRequested(m_native->position);
        controls.ShuffleEnabledChangeRequested(m_native->shuffle);
        controls.AutoRepeatModeChangeRequested(m_native->repeat);
        controls.DisplayUpdater().ClearAll();
        controls.DisplayUpdater().Update();
        controls.PlaybackStatus(MediaPlaybackStatus::Closed);
        controls.IsEnabled(false);
    } catch (...) {
    }
    controls = nullptr;
    if (m_native->apartment)
        RoUninitialize();
}

void Smtc::update()
{
    const media::State s = media::stateOf(*m_player, kCoverSize);
    auto& controls = m_native->controls;
    try {
        // The display only when the song or its details change (a new thumbnail loads).
        const media::State& shown = m_native->shown;
        if (!m_native->shownOnce || s.key != shown.key || s.title != shown.title ||
            s.artist != shown.artist || s.album != shown.album || s.artUrl != shown.artUrl) {
            auto display = controls.DisplayUpdater();
            display.ClearAll();
            if (!s.key.isEmpty()) {
                display.Type(MediaPlaybackType::Music);
                auto music = display.MusicProperties();
                music.Title(text(s.title));
                music.Artist(text(s.artist));
                music.AlbumTitle(text(s.album));
                if (!s.artUrl.isEmpty())
                    display.Thumbnail(
                        RandomAccessStreamReference::CreateFromUri(Uri(text(s.artUrl))));
            }
            display.Update();
            m_native->shown = s;
            m_native->shownOnce = true;
        }

        switch (s.status) {
        case media::Status::Playing:
            controls.PlaybackStatus(MediaPlaybackStatus::Playing);
            break;
        case media::Status::Paused:
            controls.PlaybackStatus(MediaPlaybackStatus::Paused);
            break;
        default:
            controls.PlaybackStatus(s.key.isEmpty() ? MediaPlaybackStatus::Closed
                                                    : MediaPlaybackStatus::Stopped);
        }
        controls.IsPlayEnabled(s.canPlay);
        controls.IsPauseEnabled(s.canPlay);
        controls.IsNextEnabled(s.canNext);
        controls.IsPreviousEnabled(s.canPrevious);
        controls.ShuffleEnabled(s.shuffle);
        controls.AutoRepeatMode(repeatMode(s.repeat));
        controls.PlaybackRate(1.0);

        SystemMediaTransportControlsTimelineProperties timeline;
        const qint64 length = std::max<qint64>(s.durationMs, 0);
        timeline.StartTime(span(0));
        timeline.EndTime(span(length));
        timeline.MinSeekTime(span(0));
        timeline.MaxSeekTime(span(s.canSeek ? length : 0));
        timeline.Position(span(s.positionMs));
        controls.UpdateTimelineProperties(timeline);
    } catch (const winrt::hresult_error& error) {
        qCWarning(lcPlatform) << log::event("smtc.update_failed")
                              << log::field("hresult", qint64(error.code().value));
    }

    if (s.status == media::Status::Playing)
        m_tick.start();
    else
        m_tick.stop();
}

} // namespace cirrus::ui
