pragma Singleton
import QtQuick

// What the shell draws over the pages: the floating mini player covers `bottom` pixels
// at the bottom of the content panel. Scrolling content adds it to its bottom margin,
// so the last rows can be scrolled clear of the bar; the shell sets it.
QtObject {
    property real bottom: 0
}
