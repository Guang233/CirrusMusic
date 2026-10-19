#include "system_appearance.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

#ifdef Q_OS_LINUX
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#endif

namespace cirrus::ui
{

#ifdef Q_OS_LINUX
namespace
{
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kSettings = QStringLiteral("org.freedesktop.portal.Settings");
const QString kAppearance = QStringLiteral("org.freedesktop.appearance");

// Unwraps nested D-Bus variants (ReadOne returns v, the older Read returns vv).
QVariant unwrap(QVariant value)
{
    while (value.canConvert<QDBusVariant>() &&
           value.metaType() == QMetaType::fromType<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value;
}
} // namespace

// Receives org.freedesktop.portal.Settings.SettingChanged.
class PortalWatcher : public QObject
{
    Q_OBJECT

  public:
    explicit PortalWatcher(SystemAppearance* owner) : QObject(owner), m_owner(owner)
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.connect(kPortalService, kPortalPath, kSettings, QStringLiteral("SettingChanged"), this,
                    SLOT(settingChanged(QString, QString, QDBusVariant)));
        for (const QString& key : {QStringLiteral("accent-color"), QStringLiteral("color-scheme")})
            read(bus, key);
    }

  private slots:
    void settingChanged(const QString& group, const QString& key, const QDBusVariant& value)
    {
        if (group == kAppearance)
            m_owner->applyPortalValue(key, unwrap(value.variant()));
    }

  private:
    void read(QDBusConnection& bus, const QString& key)
    {
        QDBusMessage call = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kSettings,
                                                           QStringLiteral("ReadOne"));
        call << kAppearance << key;
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(call, 2000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, key](auto* w) {
            const QDBusMessage reply = w->reply();
            if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
                m_owner->applyPortalValue(key, unwrap(reply.arguments().constFirst()));
            w->deleteLater();
        });
    }

    SystemAppearance* m_owner;
};
#endif

SystemAppearance::SystemAppearance(QObject* parent) : QObject(parent)
{
#ifdef Q_OS_LINUX
    if (QDBusConnection::sessionBus().isConnected()) {
        m_portal = true;
        new PortalWatcher(this);
    }
#endif
    if (!m_portal) {
        // Qt reports the accent on Windows and macOS; Linux without a portal has none.
        qApp->installEventFilter(this);
#ifndef Q_OS_LINUX
        m_accent = QGuiApplication::palette().color(QPalette::Accent);
#endif
    }
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            &SystemAppearance::colorSchemeChanged);
}

Qt::ColorScheme SystemAppearance::colorScheme() const
{
    if (m_portalScheme != Qt::ColorScheme::Unknown)
        return m_portalScheme;
    return QGuiApplication::styleHints()->colorScheme();
}

void SystemAppearance::applyPortalValue(const QString& key, const QVariant& value)
{
#ifdef Q_OS_LINUX
    if (key == QStringLiteral("accent-color")) {
        // (ddd) sRGB in 0..1; values outside the range mean "unset".
        QColor accent;
        if (value.canConvert<QDBusArgument>()) {
            const auto argument = value.value<QDBusArgument>();
            double r = -1, g = -1, b = -1;
            argument.beginStructure();
            argument >> r >> g >> b;
            argument.endStructure();
            if (r >= 0 && r <= 1 && g >= 0 && g <= 1 && b >= 0 && b <= 1)
                accent = QColor::fromRgbF(float(r), float(g), float(b));
        }
        setAccent(accent);
    } else if (key == QStringLiteral("color-scheme")) {
        // 0 no preference, 1 prefer dark, 2 prefer light.
        const uint preference = value.toUInt();
        const Qt::ColorScheme scheme = preference == 1   ? Qt::ColorScheme::Dark
                                       : preference == 2 ? Qt::ColorScheme::Light
                                                         : Qt::ColorScheme::Unknown;
        if (scheme != m_portalScheme) {
            m_portalScheme = scheme;
            emit colorSchemeChanged();
        }
    }
#else
    Q_UNUSED(key)
    Q_UNUSED(value)
#endif
}

bool SystemAppearance::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange) {
#ifndef Q_OS_LINUX
        setAccent(QGuiApplication::palette().color(QPalette::Accent));
#endif
    }
    return QObject::eventFilter(watched, event);
}

void SystemAppearance::setAccent(const QColor& accent)
{
    if (accent == m_accent)
        return;
    m_accent = accent;
    emit accentChanged();
}

} // namespace cirrus::ui

#ifdef Q_OS_LINUX
#include "system_appearance.moc"
#endif
