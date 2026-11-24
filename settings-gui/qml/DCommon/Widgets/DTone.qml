import QtQuick
import DCommon.Common

QtObject {
    id: root

    property string tone: ""
    property bool floatingWindow: false

    readonly property bool tinted: tone === "primary" || tone === "secondary" || tone === "tertiary"
    readonly property color containerColor: {
        switch (tone) {
        case "primary":
            return Style.primaryContainer;
        case "secondary":
            return Style.secondaryContainer;
        case "tertiary":
            return Style.tertiaryContainer;
        }
        return Style.cardSurface;
    }
    readonly property color contentColor: {
        switch (tone) {
        case "primary":
            return Style.onPrimaryContainer;
        case "secondary":
            return Style.onSecondaryContainer;
        case "tertiary":
            return Style.onTertiaryContainer;
        }
        return Style.surfaceText;
    }
    readonly property color accentColor: tinted ? contentColor : Style.primary
    property color onAccentColor
    readonly property color mutedColor: tinted ? Style.withAlpha(contentColor, 0.72) : Style.onSurfaceVariant
    readonly property color chipColor: tinted ? Style.foregroundColor(Style.withAlpha(contentColor, Style.stateLayerFocus), floatingWindow) : Style.foregroundColor(Style.chipSurface, floatingWindow)
    readonly property color surfaceColor: Style.foregroundColor(containerColor, floatingWindow)

    readonly property list<QtObject> roleBindings: [
        Binding {
            target: root
            property: "onAccentColor"
            value: root.tinted ? root.containerColor : Style.onPrimary
        }
    ]
}
