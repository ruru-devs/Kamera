pragma Singleton
import QtQuick

// Design tokens for Kamera.
// Dark-friendly, KDE-native feel: a calm blue accent used only for
// selection/active state, hairline dividers instead of card chrome,
// and a grid-first layout with no decoration competing with the photos.
QtObject {
    readonly property color bg: "#15161A"
    readonly property color surface: "#1D1E24"
    readonly property color surfaceRaised: "#24262E"
    readonly property color hairline: "#2C2E36"

    readonly property color textPrimary: "#E8E8EA"
    readonly property color textMuted: "#8B8D96"
    readonly property color textFaint: "#5C5E68"

    readonly property color accent: "#5B8DEF"
    readonly property color accentDim: "#3A5BA8"
    readonly property color danger: "#E2685B"

    readonly property real radiusSmall: 4
    readonly property real radiusMedium: 8

    readonly property int durationFast: 100
    readonly property int durationNormal: 180

    readonly property string fontFamily: "Inter"
}
