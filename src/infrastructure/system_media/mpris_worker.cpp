#include "mpris_worker.h"
#include "infrastructure/ipc/linux_observation_endpoint.h"
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QUrl>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace cirrus::infrastructure::system_media
{
using namespace domain;
using application::SystemMediaAction;
using application::SystemMediaCommand;
namespace
{
const QString Path = QStringLiteral("/org/mpris/MediaPlayer2");
const QString Root = QStringLiteral("org.mpris.MediaPlayer2");
const QString Player = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString NoTrack = QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack");
std::optional<qint64> microseconds(std::optional<qint64> value)
{
    if (!value || *value < 0 || *value > std::numeric_limits<qint64>::max() / 1000)
        return {};
    return *value * 1000;
}
QString track(const PlaybackObservation& value)
{
    if (value.status != ObservationStatus::Ready || value.binding.trackId.isNull())
        return NoTrack;
    auto id = value.binding.trackId.toString(QUuid::WithoutBraces);
    id.remove('-');
    return Path + "/track/t_" + id;
}
bool nameValid(const QString& name)
{
    const QString prefix = "org.mpris.MediaPlayer2.";
    if (!name.startsWith(prefix) || name.size() > 255)
        return false;
    const auto parts = name.mid(prefix.size()).split('.');
    for (const auto& part : parts) {
        if (part.isEmpty() || (part.front() >= '0' && part.front() <= '9'))
            return false;
        for (QChar c : part) {
            const auto u = c.unicode();
            if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||
                  u == '_' || u == '-'))
                return false;
        }
    }
    return true;
}
const char Xml[] = R"xml(
<interface name="org.mpris.MediaPlayer2">
 <method name="Raise"/><method name="Quit"/>
 <property name="CanQuit" type="b" access="read"/>
 <property name="CanRaise" type="b" access="read"/>
 <property name="HasTrackList" type="b" access="read"/>
 <property name="Identity" type="s" access="read"/>
 <property name="SupportedUriSchemes" type="as" access="read"/>
 <property name="SupportedMimeTypes" type="as" access="read"/>
</interface>
<interface name="org.mpris.MediaPlayer2.Player">
 <method name="Next"/><method name="Previous"/><method name="Pause"/>
 <method name="PlayPause"/><method name="Stop"/><method name="Play"/>
 <method name="Seek"><arg name="Offset" type="x" direction="in"/></method>
 <method name="SetPosition"><arg name="TrackId" type="o" direction="in"/><arg name="Position" type="x" direction="in"/></method>
 <method name="OpenUri"><arg name="Uri" type="s" direction="in"/></method>
 <signal name="Seeked"><arg name="Position" type="x"/></signal>
 <property name="PlaybackStatus" type="s" access="read"/>
 <property name="LoopStatus" type="s" access="readwrite"/>
 <property name="Rate" type="d" access="readwrite"/>
 <property name="Shuffle" type="b" access="readwrite"/>
 <property name="Metadata" type="a{sv}" access="read"/>
 <property name="Volume" type="d" access="readwrite"/>
 <property name="Position" type="x" access="read"><annotation name="org.freedesktop.DBus.Property.EmitsChangedSignal" value="false"/></property>
 <property name="MinimumRate" type="d" access="read"/>
 <property name="MaximumRate" type="d" access="read"/>
 <property name="CanGoNext" type="b" access="read"/>
 <property name="CanGoPrevious" type="b" access="read"/>
 <property name="CanPlay" type="b" access="read"/>
 <property name="CanPause" type="b" access="read"/>
 <property name="CanSeek" type="b" access="read"/>
 <property name="CanControl" type="b" access="read"><annotation name="org.freedesktop.DBus.Property.EmitsChangedSignal" value="const"/></property>
</interface>
<interface name="org.freedesktop.DBus.Properties">
 <method name="Get"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
 <method name="GetAll"><arg type="s" direction="in"/><arg type="a{sv}" direction="out"/></method>
 <method name="Set"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/></method>
 <signal name="PropertiesChanged"><arg type="s"/><arg type="a{sv}"/><arg type="as"/></signal>
</interface>
)xml";
} // namespace
// Select only the explicit private runtimeRoot/bus. Do not use the ambient
// session-bus address: it can contain TCP, multiple transports or autolaunch.
class MprisWorker::SessionBusGuard
{
  public:
    ~SessionBusGuard()
    {
        if (socket >= 0)
            ::close(socket);
        if (root >= 0)
            ::close(root);
    }
    bool open(const QString& configured)
    {
        if (configured.isEmpty() || configured.size() > 1024 || !configured.isValidUtf16() ||
            configured.contains(QChar::Null) || !QFileInfo(configured).isAbsolute() ||
            ::getuid() != ::geteuid())
            return false;
        path = QFileInfo(configured).canonicalFilePath();
        if (path.isEmpty())
            return false;
        root = ::open(QFile::encodeName(path).constData(),
                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (!privateRoot(root) || ::fstat(root, &rootIdentity) != 0)
            return false;
        socket = ::openat(root, "bus", O_PATH | O_NOFOLLOW | O_CLOEXEC);
        if (socket < 0 || ::fstat(socket, &socketIdentity) != 0 ||
            !S_ISSOCK(socketIdentity.st_mode) || socketIdentity.st_uid != ::geteuid())
            return false;
        const auto native = QFile::encodeName(path + "/bus");
        sockaddr_un address{};
        if (native.size() >= qsizetype(sizeof(address.sun_path)))
            return false;
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, native.constData(), size_t(native.size()) + 1);
        const int probe = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (probe < 0)
            return false;
        const int connected =
            ::connect(probe, reinterpret_cast<sockaddr*>(&address),
                      socklen_t(offsetof(sockaddr_un, sun_path) + size_t(native.size()) + 1));
        bool ready = connected == 0;
        if (!ready && errno == EINPROGRESS) {
            pollfd wait{probe, POLLOUT, 0};
            if (::poll(&wait, 1, 5000) > 0) {
                int error = 0;
                socklen_t size = sizeof(error);
                ready = ::getsockopt(probe, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0;
            }
        }
        const bool peer = ready && ipc::linuxSameUserPeer(probe);
        ::close(probe);
        return peer && current();
    }
    bool current() const
    {
        const int visible = ::open(QFile::encodeName(path).constData(),
                                   O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (visible < 0)
            return false;
        struct stat directory{}, node{};
        const bool valid = privateRoot(root) && privateRoot(visible) &&
                           ::fstat(visible, &directory) == 0 && same(directory, rootIdentity) &&
                           ::fstatat(root, "bus", &node, AT_SYMLINK_NOFOLLOW) == 0 &&
                           same(node, socketIdentity) && node.st_uid == ::geteuid();
        ::close(visible);
        return valid;
    }
    QString address() const
    {
        return "unix:path=" + QString::fromLatin1(QUrl::toPercentEncoding(path + "/bus"));
    }

  private:
    static bool same(const struct stat& a, const struct stat& b)
    {
        return a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
               (a.st_mode & S_IFMT) == (b.st_mode & S_IFMT);
    }
    static bool privateRoot(int descriptor)
    {
        struct stat s{};
        return descriptor >= 0 && ::fstat(descriptor, &s) == 0 && S_ISDIR(s.st_mode) &&
               s.st_uid == ::geteuid() && (s.st_mode & 07777) == 0700;
    }
    int root = -1, socket = -1;
    struct stat rootIdentity{}, socketIdentity{};
    QString path;
};
MprisWorker::~MprisWorker() { stop(); }
MprisWorker::MprisWorker(std::shared_ptr<ipc::ObservationMailbox> source,
                         std::shared_ptr<ipc::ObservationWakeTicket> wake,
                         std::shared_ptr<MprisShutdown> shutdown)
    : m_source(std::move(source)), m_wake(std::move(wake)), m_shutdown(std::move(shutdown))
{
}
bool MprisWorker::start(MprisServiceConfig config)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed || m_bus || !config.enabled || !nameValid(config.serviceName) ||
        config.identity.isEmpty() || config.identity.size() > 128 ||
        !config.identity.isValidUtf16() || config.identity.contains(QChar::Null) ||
        m_shutdown->canceled.load(std::memory_order_acquire)) {
        m_failed = true;
        emit initialized(false);
        stop();
        return false;
    }
    m_config = std::move(config);
    m_clock.start();
    m_connectionName = "cirrus-mpris-private-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_guard = std::make_unique<SessionBusGuard>();
    if (!m_guard->open(m_config.runtimeRoot)) {
        m_failed = true;
        emit initialized(false);
        stop();
        return false;
    }
    m_bus = std::make_unique<QDBusConnection>(
        QDBusConnection::connectToBus(m_guard->address(), m_connectionName));
    if (m_bus->isConnected() && m_guard->current() &&
        !m_shutdown->canceled.load(std::memory_order_acquire))
        m_object = m_bus->registerVirtualObject(Path, this, QDBusConnection::SingleNode);
    if (m_object)
        m_registered =
            m_bus->registerService(m_config.serviceName); // no queue/replacement/takeover
    if (!m_registered) {
        m_failed = true;
        emit initialized(false);
        stop();
        return false;
    }
    m_timer = new QTimer(this);
    m_timer->setInterval(25);
    connect(m_timer, &QTimer::timeout, this, &MprisWorker::tick);
    m_timer->start();
    emit initialized(true);
    refresh();
    return true;
}
void MprisWorker::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed)
        return;
    m_closed = true;
    m_shutdown->canceled.store(true, std::memory_order_release);
    if (m_timer)
        m_timer->stop();
    if (m_ack) {
        disconnect(m_ack, nullptr, this, nullptr);
        m_ack->deleteLater();
        m_ack = nullptr;
    }
    m_latest.reset();
    m_sent.clear();
    if (m_bus) {
        if (m_object)
            m_bus->unregisterObject(Path);
        if (m_registered)
            m_bus->unregisterService(m_config.serviceName);
        m_bus.reset();
        QDBusConnection::disconnectFromBus(m_connectionName);
    }
    m_registered = m_object = false;
    m_guard.reset();
    emit stopped(m_failed);
}
void MprisWorker::wake()
{
    // Only the actual wake callback acknowledges its queued ticket.
    m_wake->acknowledge();
    if (!m_closed)
        refresh();
}
std::optional<ipc::ObservationMailbox::Value> MprisWorker::current() const
{
    if (m_closed || m_shutdown->canceled.load(std::memory_order_acquire) || !m_guard ||
        !m_guard->current() || !m_latest || m_latest->snapshot.status != ObservationStatus::Ready ||
        m_latest->fence != m_source->fence())
        return {};
    return m_latest;
}
QVariantMap MprisWorker::properties(const QString& interface) const
{
    if (interface == Root)
        return {{"CanQuit", false},
                {"CanRaise", false},
                {"HasTrackList", false},
                {"Identity", m_config.identity},
                {"SupportedUriSchemes", QStringList{}},
                {"SupportedMimeTypes", QStringList{}}};
    if (interface != Player)
        return {};
    const auto value = current();
    const PlaybackObservation s = value ? value->snapshot : PlaybackObservation{};
    const auto& c = s.capabilities;
    const bool controls = value && m_config.allowControls;
    const bool canSeek = controls && c.seek && microseconds(s.positionMs) &&
                         microseconds(s.durationMs) && *s.durationMs > 0;
    const bool canControl = m_config.allowControls; // intrinsic immutable implementation policy
    QString status = "Stopped";
    if (value && s.state == PlaybackState::Playing)
        status = "Playing";
    else if (value && s.state == PlaybackState::Paused)
        status = "Paused";
    QVariantMap metadata{{"mpris:trackid", QVariant::fromValue(QDBusObjectPath(track(s)))}};
    if (value && !s.binding.trackId.isNull()) {
        if (const auto length = microseconds(s.durationMs))
            metadata.insert("mpris:length", *length);
        if (m_config.exportMetadata && s.metadataStatus == ObservationContentStatus::Ready &&
            s.metadata) {
            metadata.insert("xesam:title", s.metadata->title);
            metadata.insert("xesam:artist", s.metadata->artists);
            if (s.metadata->album)
                metadata.insert("xesam:album", *s.metadata->album);
        }
    }
    QVariantMap result{{"PlaybackStatus", status},
                       {"Rate", 1.0},
                       {"MinimumRate", 1.0},
                       {"MaximumRate", 1.0},
                       {"Metadata", metadata},
                       {"Position", microseconds(s.positionMs).value_or(0)},
                       {"CanGoNext", controls && c.next},
                       {"CanGoPrevious", controls && c.previous},
                       {"CanPlay", controls && c.play},
                       {"CanPause", controls && c.pause},
                       {"CanSeek", canSeek},
                       {"CanControl", canControl}};
    // Unknown stays inaccessible/invalidated, not invented 1.0/Off/false.
    if (value && s.volumeLinear && std::isfinite(*s.volumeLinear))
        result.insert("Volume", double(*s.volumeLinear));
    if (value && s.mode == ListeningMode::Queue && s.order && s.repeat) {
        result.insert("Shuffle", *s.order == QueueOrder::Shuffle);
        result.insert("LoopStatus", *s.repeat == QueueRepeat::One   ? "Track"
                                    : *s.repeat == QueueRepeat::All ? "Playlist"
                                                                    : "None");
    }
    return result;
}
QString MprisWorker::introspect(const QString& path) const
{
    return path == Path ? QString::fromLatin1(Xml) : QString{};
}
bool MprisWorker::reply(const QDBusMessage& message, const QDBusConnection& bus,
                        const QVariantList& args)
{
    // Last fence before admission; kernel/daemon/consumer bytes cannot be revoked.
    if (!m_guard || !m_guard->current() || m_shutdown->canceled.load(std::memory_order_acquire)) {
        m_failed = true;
        stop();
        return true;
    }
    const auto request = message.arguments();
    const bool content = message.interface() == Properties && !request.isEmpty() &&
                         request.front().toString() == Player;
    if (content && !args.isEmpty() && m_latest &&
        m_latest->snapshot.status == ObservationStatus::Ready &&
        m_latest->fence != m_source->fence())
        return error(message, bus, "Failed");
    if (!bus.send(message.createReply(args))) {
        m_failed = true;
        stop();
    }
    return true;
}
bool MprisWorker::error(const QDBusMessage& message, const QDBusConnection& bus,
                        const QString& name)
{
    if (!bus.send(message.createErrorReply("org.freedesktop.DBus.Error." + name,
                                           "MPRIS request unavailable"))) {
        m_failed = true;
        stop();
    }
    return true;
}
bool MprisWorker::budget()
{
    const auto now = m_clock.elapsed();
    if (now - m_window >= 1000) {
        m_window = now;
        m_requests = 0;
    }
    if (m_requests >= 32)
        return false;
    ++m_requests;
    return true;
}
bool MprisWorker::enqueue(SystemMediaCommand intent)
{
    const auto value = current();
    if (!value || !m_config.allowControls)
        return false;
    const auto previous = m_shutdown->pendingCommands.fetch_add(1, std::memory_order_acq_rel);
    if (previous >= application::SystemMediaCommands::MaximumPending) {
        m_shutdown->pendingCommands.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }
    emit commandRequested(value->snapshot.binding, value->fence, intent);
    return true;
}
bool MprisWorker::setProperty(const QDBusMessage& message, const QDBusConnection& bus,
                              const QString& property, const QVariant& value)
{
    if (property == "PlaybackStatus" || property == "Metadata" || property == "Position" ||
        property == "MinimumRate" || property == "MaximumRate" || property == "CanGoNext" ||
        property == "CanGoPrevious" || property == "CanPlay" || property == "CanPause" ||
        property == "CanSeek" || property == "CanControl")
        return error(message, bus, "PropertyReadOnly");
    if (property != "Rate" && property != "Volume" && property != "Shuffle" &&
        property != "LoopStatus")
        return error(message, bus, "UnknownProperty");
    const auto sample = current();
    if (!sample || !m_config.allowControls || !properties(Player).value("CanControl").toBool())
        return error(message, bus, "NotSupported");
    const auto& s = sample->snapshot;
    SystemMediaCommand intent;
    if (property == "Rate") {
        if (value.metaType().id() != QMetaType::Double || !std::isfinite(value.toDouble()))
            return error(message, bus, "InvalidArgs");
        if (value.toDouble() == 0.0) {
            if (!s.capabilities.pause)
                return error(message, bus, "NotSupported");
            intent.action = SystemMediaAction::Pause;
            if (!enqueue(intent))
                return error(message, bus, "LimitsExceeded");
        } else if (value.toDouble() != 1.0)
            return error(message, bus, "NotSupported");
        return reply(message, bus); // rate remains 1; zero requests the original Pause
    }
    if (property == "Volume") {
        if (!s.capabilities.volume || !s.volumeLinear)
            return error(message, bus, "NotSupported");
        if (value.metaType().id() != QMetaType::Double || !std::isfinite(value.toDouble()))
            return error(message, bus, "InvalidArgs");
        intent.action = SystemMediaAction::Volume;
        intent.volume = float(qBound(0.0, value.toDouble(), 1.0));
    } else if (property == "Shuffle" || property == "LoopStatus") {
        if (!s.capabilities.modes || s.mode != ListeningMode::Queue || !s.order || !s.repeat)
            return error(message, bus, "NotSupported");
        intent.action = SystemMediaAction::Modes;
        intent.order = *s.order;
        intent.repeat = *s.repeat;
        if (property == "Shuffle") {
            if (value.metaType().id() != QMetaType::Bool)
                return error(message, bus, "InvalidArgs");
            intent.order = value.toBool() ? QueueOrder::Shuffle : QueueOrder::Sequential;
        } else {
            if (value.metaType().id() != QMetaType::QString)
                return error(message, bus, "InvalidArgs");
            const auto text = value.toString();
            if (text == "None")
                intent.repeat = QueueRepeat::Off;
            else if (text == "Track")
                intent.repeat = QueueRepeat::One;
            else if (text == "Playlist")
                intent.repeat = QueueRepeat::All;
            else
                return error(message, bus, "InvalidArgs");
        }
    } else
        return error(message, bus, "UnknownProperty");
    if (!enqueue(intent))
        return error(message, bus, "LimitsExceeded");
    return reply(message, bus);
}
bool MprisWorker::handleMessage(const QDBusMessage& message, const QDBusConnection& bus)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed || m_shutdown->canceled.load(std::memory_order_acquire)) {
        stop();
        return true;
    }
    if (!m_guard || !m_guard->current()) {
        m_failed = true;
        stop();
        return true;
    }
    if (message.path() != Path || message.type() != QDBusMessage::MethodCallMessage)
        return false;
    if (!budget())
        return error(message, bus, "LimitsExceeded");
    const auto interface = message.interface(), member = message.member(),
               signature = message.signature();
    const auto args = message.arguments();
    if (interface == "org.freedesktop.DBus.Introspectable" && member == "Introspect" &&
        signature.isEmpty() && args.isEmpty())
        return false; // Qt generates the XML using introspect()
    if (interface == "org.freedesktop.DBus.Peer" && member == "Ping" && signature.isEmpty())
        return reply(message, bus);
    if (interface == Properties) {
        if (member == "GetAll" && signature == "s" && args.size() == 1) {
            const auto target = args[0].toString();
            if (target != Root && target != Player)
                return error(message, bus, "UnknownInterface");
            return reply(message, bus, {properties(target)});
        }
        if (member == "Get" && signature == "ss" && args.size() == 2) {
            const auto target = args[0].toString(), property = args[1].toString();
            if (target != Root && target != Player)
                return error(message, bus, "UnknownInterface");
            const auto values = properties(target);
            if (!values.contains(property))
                return error(message, bus,
                             (target == Player && (property == "Volume" || property == "Shuffle" ||
                                                   property == "LoopStatus"))
                                 ? "NotSupported"
                                 : "UnknownProperty");
            return reply(message, bus, {QVariant::fromValue(QDBusVariant(values.value(property)))});
        }
        if (member == "Set" && signature == "ssv" && args.size() == 3) {
            const auto target = args[0].toString();
            if (target == Root)
                return error(message, bus, "PropertyReadOnly");
            if (target != Player)
                return error(message, bus, "UnknownInterface");
            return setProperty(message, bus, args[1].toString(),
                               qvariant_cast<QDBusVariant>(args[2]).variant());
        }
        return error(message, bus, "InvalidArgs");
    }
    if (interface == Root) {
        if ((member == "Raise" || member == "Quit") && signature.isEmpty())
            return error(message, bus, "NotSupported");
        return error(message, bus, "UnknownMethod");
    }
    if (interface != Player)
        return error(message, bus, "UnknownInterface");
    if (member == "OpenUri")
        return error(message, bus,
                     signature == "s" && args.size() == 1 ? "NotSupported" : "InvalidArgs");
    const QStringList emptyMethods{"Play", "Pause", "PlayPause", "Stop", "Next", "Previous"};
    const QString expected = member == "Seek" ? "x" : member == "SetPosition" ? "ox" : QString{};
    if (member != "Seek" && member != "SetPosition" && !emptyMethods.contains(member))
        return error(message, bus, "UnknownMethod");
    const int expectedCount = member == "Seek" ? 1 : member == "SetPosition" ? 2 : 0;
    if (signature != expected || args.size() != expectedCount)
        return error(message, bus, "InvalidArgs");
    const auto value = current();
    if (!value || !m_config.allowControls)
        return reply(message, bus);
    const auto& s = value->snapshot;
    const auto& c = s.capabilities;
    SystemMediaCommand intent;
    bool permitted = false;
    if (member == "SetPosition") {
        if (signature != "ox")
            return error(message, bus, "InvalidArgs");
        const auto id = qvariant_cast<QDBusObjectPath>(args[0]).path();
        const auto position = args[1].toLongLong();
        const auto length = microseconds(s.durationMs);
        if (id == NoTrack || id != track(s) || position < 0 || !length || position >= *length ||
            !c.seek)
            return reply(message, bus); // stale is ignored, never retargeted
        intent.action = SystemMediaAction::Seek;
        intent.positionMs = position / 1000;
        permitted = true;
    } else if (member == "Seek") {
        if (signature != "x")
            return error(message, bus, "InvalidArgs");
        const auto position = microseconds(s.positionMs), length = microseconds(s.durationMs);
        if (!c.seek || !position || !length || *length <= 0)
            return reply(message, bus);
        const auto offset = args[0].toLongLong();
        if (offset >= *length - *position) {
            intent.action = SystemMediaAction::Next;
            permitted = c.next;
        } else {
            const auto target = qMax<qint64>(0, *position + offset);
            intent.action = SystemMediaAction::Seek;
            intent.positionMs = target / 1000;
            permitted = true;
        }
    } else {
        if (!signature.isEmpty())
            return error(message, bus, "InvalidArgs");
        if (member == "Play") {
            intent.action = SystemMediaAction::Play;
            permitted = c.play;
        } else if (member == "Pause") {
            intent.action = SystemMediaAction::Pause;
            permitted = c.pause;
        } else if (member == "PlayPause") {
            intent.action = s.state == PlaybackState::Playing ? SystemMediaAction::Pause
                                                              : SystemMediaAction::Play;
            permitted = s.state == PlaybackState::Playing ? c.pause : c.play;
        } else if (member == "Stop") {
            intent.action = SystemMediaAction::Stop;
            permitted = c.stop;
        } else if (member == "Next") {
            intent.action = SystemMediaAction::Next;
            permitted = c.next;
        } else if (member == "Previous") {
            intent.action = SystemMediaAction::Previous;
            permitted = c.previous;
        } else
            return error(message, bus, "UnknownMethod");
    }
    if (permitted && !enqueue(intent))
        return error(message, bus, "LimitsExceeded");
    return reply(message, bus);
}
void MprisWorker::refresh()
{
    if (m_shutdown->canceled.load(std::memory_order_acquire)) {
        stop();
        return;
    }
    m_latest = m_source->take();
    m_dirty = true;
    announce();
}
void MprisWorker::announce()
{
    if (m_closed || !m_bus || m_ack || !m_dirty)
        return;
    if (!m_guard || !m_guard->current()) {
        m_failed = true;
        stop();
        return;
    }
    const auto fence = m_source->fence(); // bracket projection construction, not only final send
    auto now = properties(Player);
    now.remove("Position");   // MUST NOT be sent via PropertiesChanged
    now.remove("CanControl"); // MPRIS forbids PropertiesChanged for this intrinsic policy
    QVariantMap changed;
    QStringList invalidated;
    for (auto it = now.cbegin(); it != now.cend(); ++it)
        if (!m_sent.contains(it.key()) || m_sent.value(it.key()) != it.value())
            changed.insert(it.key(), it.value());
    for (auto it = m_sent.cbegin(); it != m_sent.cend(); ++it)
        if (!now.contains(it.key()))
            invalidated.append(it.key());
    const auto value = current();
    const auto binding = value ? value->snapshot.binding : ObservationBinding{};
    const auto seek = value ? value->snapshot.seekRevision : 0;
    const auto position =
        value ? microseconds(value->snapshot.positionMs) : std::optional<qint64>{};
    const bool confirmedJump = value && seek > 0 && position &&
                               ((binding == m_sentBinding && seek > m_sentSeekRevision) ||
                                (binding != m_sentBinding && *position > 0));
    if (m_source->fence() != fence || (value && value->fence != fence))
        return;
    if (!changed.isEmpty() || !invalidated.isEmpty()) {
        auto signal = QDBusMessage::createSignal(Path, Properties, "PropertiesChanged");
        signal << Player << changed << invalidated;
        if (m_source->fence() != fence)
            return;
        if (!m_bus->send(signal)) {
            m_failed = true;
            stop();
            return;
        }
    }
    if (confirmedJump) {
        auto signal = QDBusMessage::createSignal(Path, Player, "Seeked");
        signal << *position;
        if (m_source->fence() != fence)
            return;
        if (!m_bus->send(signal)) {
            m_failed = true;
            stop();
            return;
        }
    }
    m_sent = std::move(now);
    m_sentBinding = binding;
    m_sentSeekRevision = seek;
    m_dirty = false;
    if (changed.isEmpty() && invalidated.isEmpty() && !confirmedJump)
        return;
    // One publication batch plus one daemon barrier in flight. Coalesce source
    // changes until daemon response; this is NOT consumer drain/device proof.
    const auto ping = QDBusMessage::createMethodCall(
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetId");
    m_ackStarted = m_clock.elapsed();
    m_ack = new QDBusPendingCallWatcher(m_bus->asyncCall(ping, 5000), this);
    connect(m_ack, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher* watcher) {
                const bool failure = watcher->isError();
                watcher->deleteLater();
                m_ack = nullptr;
                if (m_closed)
                    return;
                if (failure) {
                    m_failed = true;
                    stop();
                    return;
                }
                announce();
            });
}
void MprisWorker::tick()
{
    if (m_closed)
        return;
    if (m_shutdown->canceled.load(std::memory_order_acquire)) {
        stop();
        return;
    }
    if (!m_bus || !m_bus->isConnected() || !m_guard || !m_guard->current() ||
        (m_ack && m_clock.elapsed() - m_ackStarted >= 5000)) {
        m_failed = true;
        stop();
        return;
    }
    // Periodic privacy/transport check does NOT acknowledge a queued source wake.
    if (m_latest && m_latest->fence != m_source->fence()) {
        m_latest.reset();
        m_dirty = true;
        announce();
    }
}
} // namespace cirrus::infrastructure::system_media
