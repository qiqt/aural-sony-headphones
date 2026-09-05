pragma Singleton
import QtQuick

QtObject {
    id: theme

    // Toggle for the light/dark palette. Dark values are from DESIGN.md;
    // light values are approximated and can be refined from the *_light Stitch exports.
    property bool dark: true

    // ---- Colors ----
    readonly property color bg:                      dark ? "#0b1326" : "#f8fafc"
    readonly property color surface:                 dark ? "#0b1326" : "#ffffff"
    readonly property color surfaceContainerLowest:  dark ? "#060e20" : "#ffffff"
    readonly property color surfaceContainerLow:     dark ? "#131b2e" : "#f1f5f9"
    readonly property color surfaceContainer:        dark ? "#171f33" : "#f8fafc"
    readonly property color surfaceContainerHigh:    dark ? "#222a3d" : "#e9eef5"
    readonly property color surfaceContainerHighest: dark ? "#2d3449" : "#e2e8f0"
    readonly property color surfaceBright:           dark ? "#31394d" : "#ffffff"
    // NOTE: never name a property "on<ExistingSibling>" (e.g. onSurface next to surface) —
    // QML parses the initializer as a signal-handler assignment and silently drops it,
    // leaving the color default black. Hence these names instead of M3's on* roles.
    readonly property color textPrimary:             dark ? "#dae2fd" : "#0f172a"
    readonly property color textSecondary:           dark ? "#cbc3d7" : "#475569"
    readonly property color outline:                 dark ? "#958ea0" : "#94a3b8"
    readonly property color outlineVariant:          dark ? "#494454" : "#e2e8f0"
    readonly property color primary:                 dark ? "#d0bcff" : "#8b5cf6"
    readonly property color primaryText:             dark ? "#3c0091" : "#ffffff"
    readonly property color primaryContainer:        dark ? "#a078ff" : "#ede9fe"
    readonly property color primaryContainerText:    dark ? "#340080" : "#4c1d95"
    readonly property color error:                   dark ? "#ffb4ab" : "#dc2626"
    readonly property color errorText:               dark ? "#690005" : "#ffffff"
    readonly property color success:                 dark ? "#7ee0a8" : "#16a34a"

    // ---- Spacing (8px system) ----
    readonly property int xs: 4
    readonly property int base: 8
    readonly property int sm: 12
    readonly property int md: 16
    readonly property int lg: 24
    readonly property int xl: 32

    // ---- Radius ----
    readonly property int radiusSm: 4
    readonly property int radius: 8
    readonly property int radiusMd: 12
    readonly property int radiusLg: 16
    readonly property int radiusXl: 24
    readonly property int radiusFull: 9999

    // ---- Typography ----
    readonly property string fontFamily: "Manrope"        // body / UI
    readonly property string displayFont: "Space Grotesk" // headings / display
    readonly property int display: 32
    readonly property int headlineLg: 24
    readonly property int headlineMd: 20
    readonly property int bodyLg: 16
    readonly property int bodyMd: 14
    readonly property int labelLg: 14
    readonly property int labelMd: 12
    readonly property int labelSm: 11

    // ---- Shell dimensions ----
    readonly property int sidebarWidth: 240
    readonly property int topBarHeight: 64
}
