pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import Cirrus.Theme
import Cirrus.Controls

// A cover by key (`image://cover/<key>`) with rounded corners over a placeholder.
// A new cover replaces the shown one only once decoded, with a cross-fade
// (motion-choreography.md §4.1). Two layers at most: when a cover arrives during a
// fade, the more visible layer becomes the base. The software renderer has no
// masking, so covers stay square there.
Item {
    id: root

    property string coverKey
    property real radius: Theme.shape.small
    property string glyph: Symbols.music_note
    // Decode size; the item's size in device pixels by default.
    property size sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
                                      Math.ceil(height * Screen.devicePixelRatio))

    readonly property bool masked: GraphicsInfo.api !== GraphicsInfo.Software
                                   && GraphicsInfo.api !== GraphicsInfo.Unknown
    // Layer showing the current cover, the other one fades in over it.
    property Image base: first
    // The layer loading a new cover, revealed once it is done. Only it: the other layer
    // also reports status changes (a resize reloads it, or re-reports an empty one).
    property Image pending: null

    function show(): void {
        pending = null;
        const url = coverKey !== "" ? "image://cover/" + encodeURIComponent(coverKey) : "";
        if (fade.running) {
            fade.stop();
            if (second.opacity > first.opacity)
                base = second;
            else
                base = first;
        }
        const incoming = base === first ? second : first;
        base.opacity = 1;
        base.z = 1;
        incoming.z = 2;
        incoming.opacity = 0;
        incoming.source = url;
        // An Image not complete yet (a delegate being created) starts loading later and
        // is still Null here: its status change reveals it.
        if (incoming.status !== Image.Loading && (url === "" || incoming.status !== Image.Null))
            reveal(incoming);
        else
            pending = incoming;
    }

    // Called once the incoming layer has loaded or failed: fades it in, or fades the
    // shown cover out to the placeholder when there is nothing to show.
    function reveal(layer: Image): void {
        const loaded = layer.status === Image.Ready;
        if (!loaded && base.status !== Image.Ready) {
            settle();
            return;
        }
        fade.target = loaded ? layer : base;
        fade.to = loaded ? 1 : 0;
        fade.restart();
    }

    // The incoming layer becomes the base; the old one is released.
    function settle(): void {
        const incoming = base === first ? second : first;
        base.opacity = 0;
        base.source = "";
        // Still loading here only when it is reloading at a new size (retained).
        incoming.opacity = incoming.status === Image.Ready || incoming.status === Image.Loading ? 1 : 0;
        base = incoming;
    }

    onCoverKeyChanged: show()
    Component.onCompleted: show()

    component Layer: Image {
        anchors.fill: parent
        sourceSize: root.sourceSize
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        // A new size (the item resizing) reloads it: keep the old pixels meanwhile.
        retainWhileLoading: true
        opacity: 0
        onStatusChanged: {
            if (this === root.pending && status !== Image.Loading) {
                root.pending = null;
                root.reveal(this);
            }
        }
    }

    Item {
        id: content
        anchors.fill: parent
        layer.enabled: root.masked
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }

        Rectangle {
            anchors.fill: parent
            radius: root.masked ? 0 : root.radius
            color: ColorScope.scheme.surfaceContainerHighest
            Icon {
                anchors.centerIn: parent
                glyph: root.glyph
                size: Math.round(Math.min(root.width, root.height) * 0.4)
                color: ColorScope.scheme.onSurfaceVariant
            }
        }
        Layer {
            id: first
        }
        Layer {
            id: second
        }
    }

    Rectangle {
        id: mask
        anchors.fill: parent
        radius: root.radius
        visible: false
        layer.enabled: root.masked
    }

    NumberAnimation {
        id: fade
        property: "opacity"
        duration: (Theme.motion.reduced ? Theme.motion.fastEffects : Theme.motion.defaultEffects).duration
        easing: (Theme.motion.reduced ? Theme.motion.fastEffects : Theme.motion.defaultEffects).easing
        onFinished: root.settle()
    }
}
