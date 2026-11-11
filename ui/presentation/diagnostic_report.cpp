#include "diagnostic_report.h"

#include "account.h"
#include "app.h"
#include "app_settings.h"
#include "cache_status.h"
#include "logging/log.h"
#include "logging/logger.h"
#include "network_status.h"
#include "playback.h"
#include "player.h"
#include "session.h"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QHostInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QMediaDevices>
#include <QMetaEnum>
#include <QMetaProperty>
#include <QNetworkInformation>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QOperatingSystemVersion>
#include <QProcessEnvironment>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QSslSocket>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyleHints>
#include <QSysInfo>
#include <QThread>
#include <QTimeZone>
#include <QTimer>
#ifdef CIRRUS_HAVE_RHI
#include <rhi/qrhi.h>
#endif
#if QT_CONFIG(opengl)
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#endif

#include <algorithm>

#ifdef Q_OS_LINUX
#include <link.h>
#endif

using namespace Qt::StringLiterals;

namespace cirrus::ui
{
namespace
{
// "[section]" then "key: value" lines, the keys padded to line up.
class Text
{
  public:
    void section(const QString& title)
    {
        if (!m_out.isEmpty())
            m_out += u'\n';
        m_out += u'[' + title + u"]\n";
    }
    void line(const QString& key, const QString& value)
    {
        m_out += key.leftJustified(22) + u": " + (value.isEmpty() ? u"-"_s : value) + u'\n';
    }
    void line(const QString& key, qint64 value) { line(key, QString::number(value)); }
    void line(const QString& key, bool value) { line(key, value ? u"yes"_s : u"no"_s); }
    void raw(const QString& text) { m_out += text + u'\n'; }
    [[nodiscard]] QString text() const { return m_out; }

  private:
    QString m_out;
};

QString bytes(qint64 n)
{
    if (n < 0)
        return u"unknown"_s;
    return QLocale::c().formattedDataSize(n, 1, QLocale::DataSizeTraditionalFormat);
}

// The home directory and the app's roots as placeholders, for text that may hold paths.
QString scrubbed(QString text)
{
    for (const auto& [placeholder, location] :
         {std::pair{u"<data>"_s, QStandardPaths::GenericDataLocation},
          std::pair{u"<cache>"_s, QStandardPaths::GenericCacheLocation},
          std::pair{u"<config>"_s, QStandardPaths::GenericConfigLocation}}) {
        const QString root = QStandardPaths::writableLocation(location) + u"/Cirrus"_s;
        text.replace(root, placeholder);
    }
    const QString home = QDir::homePath();
    if (home.size() > 1)
        text.replace(home, u"~"_s);
    return text;
}

QString enumName(const QMetaEnum& meta, int value)
{
    const char* key = meta.valueToKey(value);
    return key ? QString::fromLatin1(key) : QString::number(value);
}

// The first line of /proc/<file> that starts with `key`, after the colon.
QString procValue(const QString& file, const QString& key)
{
    QFile f(u"/proc/"_s + file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    // /proc files say they are empty: read until there is no more, not to their size.
    const QStringList lines = QString::fromUtf8(f.readAll()).split(u'\n');
    for (const QString& line : lines)
        if (line.startsWith(key))
            return line.section(u':', 1).trimmed();
    return {};
}

QString installKind()
{
    if (qEnvironmentVariableIsSet("APPIMAGE"))
        return u"AppImage"_s;
    if (qEnvironmentVariableIsSet("FLATPAK_ID"))
        return u"Flatpak"_s;
    if (qEnvironmentVariableIsSet("SNAP"))
        return u"Snap"_s;
    const QString dir = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(dir + u"/CMakeCache.txt"_s))
        return u"development build"_s;
#if defined(Q_OS_WIN)
    return dir.contains(u"Program Files", Qt::CaseInsensitive) ? u"installer"_s : u"portable"_s;
#elif defined(Q_OS_MACOS)
    return dir.contains(u".app/Contents") ? u"app bundle"_s : u"other"_s;
#else
    if (dir.startsWith(u"/opt/cirrus"))
        return u"deb (/opt/cirrus)"_s;
    if (dir.startsWith(u"/usr/"))
        return u"system package"_s;
    return u"other"_s;
#endif
}

QString proxyText(const QNetworkProxy& proxy)
{
    switch (proxy.type()) {
    case QNetworkProxy::NoProxy:
        return u"none"_s;
    case QNetworkProxy::DefaultProxy:
        return u"default"_s;
    case QNetworkProxy::Socks5Proxy:
        return QStringLiteral("socks5://%1:%2").arg(proxy.hostName()).arg(proxy.port());
    case QNetworkProxy::HttpProxy:
        return QStringLiteral("http://%1:%2").arg(proxy.hostName()).arg(proxy.port());
    case QNetworkProxy::HttpCachingProxy:
    case QNetworkProxy::FtpCachingProxy:
        return QStringLiteral("caching %1:%2").arg(proxy.hostName()).arg(proxy.port());
    }
    return u"?"_s;
}

QString systemProxyFor(const QString& host)
{
    const auto proxies =
        QNetworkProxyFactory::proxyForQuery(QNetworkProxyQuery(QUrl(u"https://"_s + host + u'/')));
    return proxies.isEmpty() ? u"none"_s : proxyText(proxies.first());
}

// A proxy variable's value without credentials: http://user:pw@host:1 -> http://host:1.
QString withoutCredentials(const QString& value)
{
    static const QRegularExpression userInfo(QStringLiteral(R"(^([A-Za-z0-9+.\-]+://)[^@/]*@)"));
    QString out = value;
    out.replace(userInfo, u"\\1"_s);
    return out;
}
QString sampleFormatName(QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8:
        return u"UInt8"_s;
    case QAudioFormat::Int16:
        return u"Int16"_s;
    case QAudioFormat::Int32:
        return u"Int32"_s;
    case QAudioFormat::Float:
        return u"Float"_s;
    default:
        return u"Unknown"_s;
    }
}

QString graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Software:
        return u"Software"_s;
    case QSGRendererInterface::OpenGL:
        return u"OpenGL"_s;
    case QSGRendererInterface::Direct3D11:
        return u"Direct3D11"_s;
    case QSGRendererInterface::Direct3D12:
        return u"Direct3D12"_s;
    case QSGRendererInterface::Vulkan:
        return u"Vulkan"_s;
    case QSGRendererInterface::Metal:
        return u"Metal"_s;
    case QSGRendererInterface::Null:
        return u"Null"_s;
    default:
        return QString::number(int(api));
    }
}

#ifdef CIRRUS_HAVE_RHI
QString deviceTypeName(QRhiDriverInfo::DeviceType type)
{
    switch (type) {
    case QRhiDriverInfo::IntegratedDevice:
        return u"integrated"_s;
    case QRhiDriverInfo::DiscreteDevice:
        return u"discrete"_s;
    case QRhiDriverInfo::ExternalDevice:
        return u"external"_s;
    case QRhiDriverInfo::VirtualDevice:
        return u"virtual"_s;
    case QRhiDriverInfo::CpuDevice:
        return u"cpu"_s;
    default:
        return u"unknown"_s;
    }
}
#endif
} // namespace

namespace report
{
QString sessionOf(const QString& logFileName)
{
    static const QRegularExpression name(QStringLiteral(R"(^(cirrus-\d{8}-\d{6}-\d+))"));
    const auto match = name.match(logFileName);
    return match.hasMatch() ? match.captured(1) : logFileName;
}

LogScan scanLog(const QString& path)
{
    LogScan scan;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return scan;
    static const QRegularExpression record(
        QStringLiteral(R"(^(\S+) ([DIWCF]) \S+\s+(\S+) (\S+)(.*)$)"));
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(u"fatal signal ")) {
            scan.signal = line;
            continue;
        }
        const auto match = record.match(line);
        if (!match.hasMatch())
            continue;
        ++scan.records;
        const QString time = match.captured(1);
        if (scan.first.isEmpty())
            scan.first = time;
        scan.last = time;
        const QString event = match.captured(4);
        if (event == u"app.start" && scan.header.isEmpty())
            scan.header = match.captured(5).trimmed();
        else if (event == u"app.exit")
            scan.ended = true;
        else if (event == u"user.mark")
            scan.marks.append(time);
        if (match.captured(2) == u"W" || match.captured(2) == u"C")
            ++scan.warnings[match.captured(3) + u' ' + event];
    }
    return scan;
}

QString about(const UiState& ui)
{
    Text t;
    t.section(u"application"_s);
    t.line(u"name"_s, QCoreApplication::applicationName());
    t.line(u"version"_s, QCoreApplication::applicationVersion());
    // commit, build_id and the build type come from this run's app.start record.
    const QString current = log::currentFile();
    const QString header = current.isEmpty() ? QString() : scanLog(current).header;
    static const QRegularExpression field(QStringLiteral(R"((\w+)=("[^"]*"|\S+))"));
    for (const QString key : {u"commit"_s, u"build"_s, u"build_id"_s}) {
        QString value;
        for (auto it = field.globalMatch(header); it.hasNext();) {
            const auto m = it.next();
            if (m.captured(1) == key)
                value = m.captured(2);
        }
        t.line(key, value);
    }
    t.line(u"install"_s, installKind());
    t.line(u"executable"_s, QFileInfo(QCoreApplication::applicationFilePath()).fileName());
    QStringList arguments = QCoreApplication::arguments().mid(1);
    for (QString& a : arguments)
        a = scrubbed(a);
    t.line(u"arguments"_s, arguments.join(u' '));
    t.line(u"pid"_s, QCoreApplication::applicationPid());
    t.line(u"started"_s, ui.started.toString(Qt::ISODateWithMs));
    t.line(u"uptime_s"_s, ui.started.secsTo(QDateTime::currentDateTime()));
    t.line(u"exported"_s, QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    t.line(u"detailed_logging"_s, log::verbose());

    t.section(u"build"_s);
#if defined(__clang__)
    t.line(u"compiler"_s, u"Clang "_s + QStringLiteral(__clang_version__));
#elif defined(__GNUC__)
    t.line(u"compiler"_s, u"GCC "_s + QStringLiteral(__VERSION__));
#elif defined(_MSC_VER)
    t.line(u"compiler"_s, u"MSVC "_s + QString::number(_MSC_FULL_VER));
#endif
    t.line(u"cxx_standard"_s, qint64(__cplusplus));
    t.line(u"qt_built_with"_s, QStringLiteral(QT_VERSION_STR));
    t.line(u"qt_running"_s, QString::fromLatin1(qVersion()));
    t.line(u"qt_build"_s, QString::fromLatin1(QLibraryInfo::build()));
    t.line(u"qt_debug_build"_s, QLibraryInfo::isDebugBuild());
    t.line(u"tls_backend"_s, QSslSocket::activeBackend());
    t.line(u"tls_library"_s, QSslSocket::sslLibraryVersionString());
    t.line(u"tls_built_with"_s, QSslSocket::sslLibraryBuildVersionString());
    return t.text();
}

QString system(const UiState& ui)
{
    Text t;
    t.section(u"os"_s);
    t.line(u"name"_s, QSysInfo::prettyProductName());
    t.line(u"product"_s, QSysInfo::productType() + u' ' + QSysInfo::productVersion());
    t.line(u"kernel"_s, QSysInfo::kernelType() + u' ' + QSysInfo::kernelVersion());
    t.line(u"cpu_arch"_s, QSysInfo::currentCpuArchitecture());
    t.line(u"build_abi"_s, QSysInfo::buildAbi());
    t.line(u"desktop"_s, qEnvironmentVariable("XDG_CURRENT_DESKTOP"));
    t.line(u"session_type"_s, qEnvironmentVariable("XDG_SESSION_TYPE"));
    t.line(u"qpa"_s, QGuiApplication::platformName());
    t.line(u"color_scheme"_s, enumName(QMetaEnum::fromType<Qt::ColorScheme>(),
                                       int(QGuiApplication::styleHints()->colorScheme())));

    t.section(u"cpu"_s);
    t.line(u"model"_s, procValue(u"cpuinfo"_s, u"model name"_s));
    t.line(u"threads"_s, qint64(QThread::idealThreadCount()));

    t.section(u"memory"_s);
    t.line(u"total"_s, procValue(u"meminfo"_s, u"MemTotal"_s));
    t.line(u"available"_s, procValue(u"meminfo"_s, u"MemAvailable"_s));
    t.line(u"swap_total"_s, procValue(u"meminfo"_s, u"SwapTotal"_s));
    t.line(u"process_rss"_s, procValue(u"self/status"_s, u"VmRSS"_s));
    t.line(u"process_peak_rss"_s, procValue(u"self/status"_s, u"VmHWM"_s));
    t.line(u"process_threads"_s, procValue(u"self/status"_s, u"Threads"_s));

    t.section(u"storage"_s);
    for (const auto& [name, location] :
         {std::pair{u"data"_s, QStandardPaths::GenericDataLocation},
          std::pair{u"cache"_s, QStandardPaths::GenericCacheLocation},
          std::pair{u"config"_s, QStandardPaths::GenericConfigLocation}}) {
        const QStorageInfo volume(QStandardPaths::writableLocation(location));
        t.line(name, volume.isValid()
                         ? QStringLiteral("%1 free of %2, %3%4")
                               .arg(bytes(volume.bytesAvailable()), bytes(volume.bytesTotal()),
                                    QString::fromLatin1(volume.fileSystemType()),
                                    volume.isReadOnly() ? u", read-only"_s : QString())
                         : u"unknown"_s);
    }
    const QStorageInfo logs(log::directory());
    t.line(u"logs"_s, logs.isValid() ? bytes(logs.bytesAvailable()) + u" free"_s : u"-"_s);

    t.section(u"locale"_s);
    t.line(u"system"_s, QLocale::system().name());
    t.line(u"ui_languages"_s, QLocale::system().uiLanguages().join(u", "));
    t.line(u"utc_offset_min"_s, qint64(QDateTime::currentDateTime().offsetFromUtc() / 60));

    const auto screens = QGuiApplication::screens();
    for (qsizetype i = 0; i < screens.size(); ++i) {
        const QScreen* s = screens[i];
        t.section(QStringLiteral("screen %1%2")
                      .arg(i)
                      .arg(s == QGuiApplication::primaryScreen() ? u" (primary)"_s : QString()));
        t.line(u"name"_s, s->name());
        t.line(u"model"_s, (s->manufacturer() + u' ' + s->model()).trimmed());
        t.line(u"geometry"_s, QStringLiteral("%1x%2 at %3,%4")
                                  .arg(s->geometry().width())
                                  .arg(s->geometry().height())
                                  .arg(s->geometry().x())
                                  .arg(s->geometry().y()));
        t.line(
            u"physical_mm"_s,
            QStringLiteral("%1x%2").arg(s->physicalSize().width()).arg(s->physicalSize().height()));
        t.line(u"device_pixel_ratio"_s, QString::number(s->devicePixelRatio()));
        t.line(u"logical_dpi"_s, QString::number(s->logicalDotsPerInch()));
        t.line(u"refresh_hz"_s, QString::number(s->refreshRate()));
        t.line(u"depth"_s, qint64(s->depth()));
    }
    if (ui.window) {
        t.section(u"window"_s);
        t.line(u"size"_s, QStringLiteral("%1x%2").arg(ui.window->width()).arg(ui.window->height()));
        t.line(u"visibility"_s, log::nameOf(ui.window->visibility()));
        t.line(u"screen"_s, ui.window->screen() ? ui.window->screen()->name() : QString());
        t.line(u"device_pixel_ratio"_s, QString::number(ui.window->devicePixelRatio()));
    }
    return t.text();
}

QString graphicsDevice(QQuickWindow* window)
{
    Text t;
    t.section(u"renderer"_s);
#ifdef CIRRUS_HAVE_RHI
    if (QRhi* rhi = window->rhi()) {
        const QRhiDriverInfo info = rhi->driverInfo();
        t.line(u"rhi_backend"_s, QString::fromLatin1(rhi->backendName()));
        t.line(u"device"_s, QString::fromUtf8(info.deviceName));
        t.line(u"device_id"_s, u"0x"_s + QString::number(info.deviceId, 16));
        t.line(u"vendor_id"_s, u"0x"_s + QString::number(info.vendorId, 16));
        t.line(u"device_type"_s, deviceTypeName(info.deviceType));
    } else {
        t.line(u"rhi_backend"_s, u"none (software)"_s);
    }
#else
    Q_UNUSED(window);
    t.line(u"rhi_backend"_s, u"not available in this build"_s);
#endif
#if QT_CONFIG(opengl)
    if (QOpenGLContext* gl = QOpenGLContext::currentContext()) {
        const auto string = [gl](GLenum name) {
            const auto* text = reinterpret_cast<const char*>(gl->functions()->glGetString(name));
            return text ? QString::fromLatin1(text) : QString();
        };
        t.section(u"opengl"_s);
        t.line(u"vendor"_s, string(GL_VENDOR));
        t.line(u"renderer"_s, string(GL_RENDERER));
        t.line(u"version"_s, string(GL_VERSION));
        t.line(u"glsl"_s, string(GL_SHADING_LANGUAGE_VERSION));
        const QSurfaceFormat format = gl->format();
        t.line(u"context"_s,
               QStringLiteral("%1.%2 %3%4")
                   .arg(format.majorVersion())
                   .arg(format.minorVersion())
                   .arg(enumName(QMetaEnum::fromType<QSurfaceFormat::OpenGLContextProfile>(),
                                 format.profile()))
                   .arg(gl->isOpenGLES() ? u" ES"_s : QString()));
        t.line(u"samples"_s, qint64(format.samples()));
        t.line(u"swap"_s, enumName(QMetaEnum::fromType<QSurfaceFormat::SwapBehavior>(),
                                   format.swapBehavior()));
    }
#endif
    return t.text();
}

QString graphics(const UiState& ui)
{
    Text t;
    t.section(u"scene graph"_s);
    if (ui.window) {
        t.line(u"graphics_api"_s, graphicsApiName(ui.window->rendererInterface()->graphicsApi()));
        t.line(u"persistent_graphics"_s, ui.window->isPersistentGraphics());
    }
    if (ui.settings)
        t.line(u"prefer_discrete_gpu"_s, ui.settings->preferDiscreteGpu());
    for (const char* name :
         {"CIRRUS_GPU", "QSG_RHI_BACKEND", "QT_QUICK_BACKEND", "QSG_INFO", "DRI_PRIME",
          "__NV_PRIME_RENDER_OFFLOAD", "__GLX_VENDOR_LIBRARY_NAME", "LIBGL_ALWAYS_SOFTWARE"})
        if (qEnvironmentVariableIsSet(name))
            t.line(QString::fromLatin1(name), qEnvironmentVariable(name));
    QString text = t.text();
    if (!ui.graphics.isEmpty())
        text += u'\n' + ui.graphics;
    else
        text += u"\n[renderer]\nnot started yet (the window has not been shown)\n"_s;
    return text;
}

QString audio()
{
    Text t;
    const QAudioDevice preferred = QMediaDevices::defaultAudioOutput();
    t.section(u"default output"_s);
    t.line(u"device"_s, preferred.isNull() ? u"none"_s : preferred.description());
    const auto outputs = QMediaDevices::audioOutputs();
    t.line(u"outputs"_s, qint64(outputs.size()));
    for (qsizetype i = 0; i < outputs.size(); ++i) {
        const QAudioDevice& d = outputs[i];
        t.section(
            QStringLiteral("output %1%2").arg(i).arg(d.isDefault() ? u" (default)"_s : QString()));
        t.line(u"description"_s, d.description());
        const QAudioFormat f = d.preferredFormat();
        t.line(u"preferred_format"_s, QStringLiteral("%1 Hz, %2 ch, %3")
                                          .arg(f.sampleRate())
                                          .arg(f.channelCount())
                                          .arg(sampleFormatName(f.sampleFormat())));
        t.line(u"sample_rates"_s,
               QStringLiteral("%1–%2 Hz").arg(d.minimumSampleRate()).arg(d.maximumSampleRate()));
        t.line(u"channels"_s,
               QStringLiteral("%1–%2").arg(d.minimumChannelCount()).arg(d.maximumChannelCount()));
        QStringList formats;
        for (const auto format : d.supportedSampleFormats())
            formats.append(sampleFormatName(format));
        t.line(u"sample_formats"_s, formats.join(u", "));
    }
    return t.text();
}

QString environment()
{
    // Variables that change how Qt, the GPU, the desktop or the network behave; values with
    // the home directory as ~ and proxy credentials removed. Nothing else is read.
    static const QRegularExpression wanted(QStringLiteral(
        R"(^(QT_|QSG_|QML_|QTWEBENGINE_|CIRRUS_|XDG_(CURRENT_DESKTOP|SESSION_TYPE|SESSION_DESKTOP)$|DESKTOP_SESSION$|GDK_BACKEND$|LANG$|LANGUAGE$|LC_|DRI_PRIME$|__NV_|__GLX_|LIBGL_|MESA_|VK_ICD|PULSE_|PIPEWIRE_|SDL_AUDIODRIVER$|APPIMAGE$|FLATPAK_ID$|SNAP$))"));
    static const QRegularExpression proxy(QStringLiteral(R"(^(https?|all|no|ftp)_proxy$)"),
                                          QRegularExpression::CaseInsensitiveOption);
    Text t;
    t.section(u"environment"_s);
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList keys = env.keys();
    keys.sort();
    for (const QString& key : std::as_const(keys)) {
        if (proxy.match(key).hasMatch())
            t.line(key, withoutCredentials(env.value(key)));
        else if (wanted.match(key).hasMatch())
            t.line(key, scrubbed(env.value(key)));
    }
    t.line(u"WAYLAND_DISPLAY"_s, env.contains(u"WAYLAND_DISPLAY"_s) ? u"set"_s : u"unset"_s);
    t.line(u"DISPLAY"_s, env.contains(u"DISPLAY"_s) ? u"set"_s : u"unset"_s);
    return t.text();
}

QString modules()
{
    Text t;
    t.section(u"loaded modules"_s);
#ifdef Q_OS_LINUX
    QStringList names;
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) -> int {
            if (info->dlpi_name && info->dlpi_name[0])
                static_cast<QStringList*>(data)->append(
                    QFileInfo(QString::fromLocal8Bit(info->dlpi_name)).fileName());
            return 0;
        },
        &names);
    names.removeDuplicates();
    names.sort();
    t.line(u"count"_s, qint64(names.size()));
    for (const QString& name : std::as_const(names))
        t.raw(name);
#else
    t.raw(u"not listed on this system"_s);
#endif
    return t.text();
}

QString settings(const AppSettings* settings)
{
    Text t;
    t.section(u"settings"_s);
    if (!settings)
        return t.text();
    // Every setting; for lists only how many (search history is the user's own words).
    const QMetaObject* meta = settings->metaObject();
    for (int i = QObject::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        const QVariant value = property.read(settings);
        QString text;
        if (value.typeId() == QMetaType::QStringList)
            text = QStringLiteral("%1 items").arg(value.toStringList().size());
        else if (value.typeId() == QMetaType::QDateTime)
            text = value.toDateTime().toString(Qt::ISODate);
        else if (value.canConvert<QString>())
            text = value.toString();
        t.line(QString::fromLatin1(property.name()), text);
    }
    return t.text();
}

QString state(const UiState& ui)
{
    Text t;
    const App* app = App::instance();
    t.section(u"ui"_s);
    t.line(u"pages"_s, ui.pages.join(u" > "));
    t.line(u"popup"_s, ui.popup);
    t.line(u"player_page"_s, ui.player);
    if (ui.window && ui.window->activeFocusItem())
        t.line(u"focus"_s,
               QString::fromLatin1(ui.window->activeFocusItem()->metaObject()->className())
                   .remove(QRegularExpression(u"_QML(TYPE)?_\\d+$"_s)));
    if (!app)
        return t.text();

    t.section(u"account"_s);
    t.line(u"status"_s, enumName(QMetaEnum::fromType<account::Status>(), app->session()->status()));

    t.section(u"playback"_s);
    const Player* player = app->player();
    t.line(u"status"_s, enumName(QMetaEnum::fromType<playback::Status>(), player->status()));
    t.line(u"mode"_s, enumName(QMetaEnum::fromType<playback::Mode>(), player->mode()));
    t.line(u"song"_s, player->current().key);
    t.line(u"position_ms"_s, player->positionMs());
    t.line(u"duration_ms"_s, player->durationMs());
    t.line(u"queue_index"_s, qint64(player->queueIndex()));
    t.line(u"volume"_s, QString::number(player->volume()));

    t.section(u"network"_s);
    t.line(u"online"_s, app->network()->online());
    if (const auto* info = QNetworkInformation::instance()) {
        t.line(u"backend"_s, info->backendName());
        t.line(u"reachability"_s, log::nameOf(info->reachability()));
        t.line(u"transport"_s, log::nameOf(info->transportMedium()));
        t.line(u"metered"_s, info->isMetered());
        t.line(u"captive_portal"_s, info->isBehindCaptivePortal());
    }

    t.section(u"storage"_s);
    t.line(u"cache"_s, bytes(app->cache()->bytes()));
    t.line(u"audio_cache"_s, bytes(app->cache()->audioBytes()));
    qint64 logBytes = 0;
    const auto files = log::files();
    for (const auto& f : files)
        logBytes += f.bytes;
    t.line(u"logs"_s, QStringLiteral("%1 files, %2").arg(files.size()).arg(bytes(logBytes)));
    return t.text();
}

QString readme(const QStringList& files, const QList<LogScan>& scans, const QStringList& logNames,
               bool description)
{
    Text t;
    t.raw(u"Cirrus diagnostic package"_s);
    t.raw(u"Made by Settings > Diagnostics > Export, "_s +
          QDateTime::currentDateTime().toString(Qt::ISODate) + u'.');
    t.raw(u"Read the logs with tools/logs/cirrus_log.py (summary, timeline --around mark, "
          "symbolize)."_s);
    t.raw(QString());
    t.section(u"contents"_s);
    static const QHash<QString, QString> what = {
        {u"about.txt"_s, u"the app: version, commit, build id, install, Qt, TLS"_s},
        {u"system.txt"_s, u"OS, CPU, memory, disks, locale, screens, window"_s},
        {u"graphics.txt"_s, u"graphics API, GPU and driver, related variables"_s},
        {u"audio.txt"_s, u"audio outputs and their formats"_s},
        {u"network.txt"_s, u"proxies, network state, TLS, and a check of each NetEase host"_s},
        {u"environment.txt"_s, u"variables that change Qt, the GPU, the desktop or proxies"_s},
        {u"modules.txt"_s, u"libraries and plugins loaded"_s},
        {u"state.txt"_s, u"what the app was doing: pages, account state, playback"_s},
        {u"settings.txt"_s, u"the app's settings"_s},
        {u"description.txt"_s, u"what the user wrote"_s},
        {u"crashes/"_s, u"stacks of crashes, as module and offset"_s},
        {u"logs/"_s, u"the chosen sessions' logs"_s},
    };
    for (const QString& file : files)
        t.line(file, what.value(file));
    if (!description)
        t.line(u"description.txt"_s, u"(the user wrote nothing)"_s);
    t.raw(QString());
    t.raw(u"Not included: account ids, cookies or tokens, playlist or song titles, search "
          "words, comments, the database, caches, any file of the user's."_s);

    for (qsizetype i = 0; i < scans.size(); ++i) {
        const LogScan& scan = scans[i];
        t.section(u"session "_s + logNames.value(i));
        t.line(u"from"_s, scan.first);
        t.line(u"to"_s, scan.last);
        t.line(u"records"_s, qint64(scan.records));
        const bool current = QFileInfo(log::currentFile()).fileName() == logNames.value(i);
        t.line(u"ended"_s, current                 ? u"(this run, still running)"_s
                           : scan.ended            ? u"yes"_s
                           : scan.signal.isEmpty() ? u"no (killed, or the system went down)"_s
                                                   : u"no, "_s + scan.signal);
        if (!scan.marks.isEmpty())
            t.line(u"marks"_s, scan.marks.join(u", "));
        QList<std::pair<QString, int>> warnings;
        for (auto it = scan.warnings.cbegin(); it != scan.warnings.cend(); ++it)
            warnings.append({it.key(), it.value()});
        std::sort(warnings.begin(), warnings.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        for (qsizetype w = 0; w < qMin<qsizetype>(10, warnings.size()); ++w)
            t.line(u"warnings"_s,
                   QStringLiteral("%1 x %2").arg(warnings[w].second).arg(warnings[w].first));
    }
    return t.text();
}
} // namespace report

namespace
{
// The hosts the app talks to: API, web, covers, audio.
const QStringList kProbeHosts = {u"interface.music.163.com"_s, u"music.163.com"_s,
                                 u"interface3.music.163.com"_s, u"p1.music.126.net"_s,
                                 u"m701.music.126.net"_s};
} // namespace

NetworkProbe::NetworkProbe(QObject* parent) : QObject(parent)
{
    m_direct.setProxy(QNetworkProxy::NoProxy);
}

void NetworkProbe::start(int timeoutMs)
{
    for (const QString& name : kProbeHosts)
        m_hosts.append({name, {}, {}, {}});
    for (int i = 0; i < m_hosts.size(); ++i) {
        ++m_waiting;
        auto clock = std::make_shared<QElapsedTimer>();
        clock->start();
        QHostInfo::lookupHost(m_hosts[i].name, this, [this, i, clock](const QHostInfo& info) {
            if (m_finished)
                return;
            QStringList addresses;
            int v4 = 0, v6 = 0;
            for (const QHostAddress& a : info.addresses()) {
                (a.protocol() == QAbstractSocket::IPv6Protocol ? v6 : v4)++;
                if (addresses.size() < 3)
                    addresses.append(a.toString());
            }
            m_hosts[i].dns = info.error() == QHostInfo::NoError
                                 ? QStringLiteral("%1 ms, IPv4 %2, IPv6 %3: %4")
                                       .arg(clock->elapsed())
                                       .arg(v4)
                                       .arg(v6)
                                       .arg(addresses.join(u", "))
                                 : QStringLiteral("failed after %1 ms: %2 (%3)")
                                       .arg(clock->elapsed())
                                       .arg(log::nameOf(info.error()), info.errorString());
            done();
        });
        request(i, false, timeoutMs);
        request(i, true, timeoutMs);
    }
    // Whatever has not answered by then is reported as such.
    QTimer::singleShot(timeoutMs + 1000, this, [this] {
        m_waiting = 0;
        done();
    });
}

void NetworkProbe::request(int host, bool direct, int timeoutMs)
{
    ++m_waiting;
    QNetworkRequest request(QUrl(u"https://"_s + m_hosts[host].name + u'/'));
    request.setTransferTimeout(timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    QNetworkReply* reply = (direct ? m_direct : m_system).head(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, host, direct, clock] {
        reply->deleteLater();
        if (m_finished)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool h2 = reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool();
        // Any HTTP status means the host was reached; an error without one did not.
        const QString text = status != 0 ? QStringLiteral("HTTP %1 in %2 ms%3")
                                               .arg(status)
                                               .arg(clock->elapsed())
                                               .arg(h2 ? u" (HTTP/2)"_s : QString())
                                         : QStringLiteral("failed after %1 ms: %2 \"%3\"")
                                               .arg(clock->elapsed())
                                               .arg(log::nameOf(reply->error()),
                                                    log::message(reply->errorString()).value);
        (direct ? m_hosts[host].direct : m_hosts[host].viaProxy) = text;
        done();
    });
}

void NetworkProbe::done()
{
    if (m_finished || (m_waiting > 0 && --m_waiting > 0))
        return;
    m_finished = true;
    Text t;
    t.section(u"proxy"_s);
    t.line(u"application_proxy"_s, proxyText(QNetworkProxy::applicationProxy()));
    t.line(u"used_for_netease"_s, systemProxyFor(u"interface.music.163.com"_s));
    for (const char* name : {"https_proxy", "http_proxy", "all_proxy", "no_proxy", "HTTPS_PROXY",
                             "HTTP_PROXY", "ALL_PROXY", "NO_PROXY"})
        if (qEnvironmentVariableIsSet(name))
            t.line(QString::fromLatin1(name), withoutCredentials(qEnvironmentVariable(name)));

    t.section(u"network"_s);
    if (const auto* info = QNetworkInformation::instance()) {
        t.line(u"backend"_s, info->backendName());
        t.line(u"reachability"_s, log::nameOf(info->reachability()));
        t.line(u"transport"_s, log::nameOf(info->transportMedium()));
        t.line(u"metered"_s, info->isMetered());
        t.line(u"captive_portal"_s, info->isBehindCaptivePortal());
    } else {
        t.line(u"backend"_s, u"none loaded"_s);
    }

    t.section(u"tls"_s);
    t.line(u"supported"_s, QSslSocket::supportsSsl());
    t.line(u"backend"_s, QSslSocket::activeBackend());
    t.line(u"library"_s, QSslSocket::sslLibraryVersionString());

    for (const Host& host : std::as_const(m_hosts)) {
        t.section(u"host "_s + host.name);
        t.line(u"dns"_s, host.dns.isEmpty() ? u"no answer"_s : host.dns);
        t.line(u"https (proxy)"_s, u"via "_s + systemProxyFor(host.name) + u": "_s +
                                       (host.viaProxy.isEmpty() ? u"no answer"_s : host.viaProxy));
        t.line(u"https direct"_s, host.direct.isEmpty() ? u"no answer"_s : host.direct);
    }
    emit finished(t.text());
}
} // namespace cirrus::ui
