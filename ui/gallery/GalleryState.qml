pragma Singleton
import QtQml

// Gallery-wide flags. `capturing` is set for --grab runs so pages can open popups
// (menus, tooltips) that a static screenshot would otherwise miss.
QtObject {
    property bool capturing: false
}
