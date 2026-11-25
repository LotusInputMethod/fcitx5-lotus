pragma Singleton

import QtQuick
import "Shape.js" as Shape
import "Surface.js" as Surface
import "Contrast.js" as Contrast
import "Accents.js" as Accents
import "Tonal.js" as Tonal

QtObject {
    id: root

    enum TextRenderType {
        Qt,
        Native,
        Curve
    }

    enum TextRenderQuality {
        Default,
        Low,
        Normal,
        High,
        VeryHigh
    }

    property var theme: typeof Theme !== "undefined" ? Theme : null
    property var settings: null

    readonly property bool isLightMode: theme?.isLightMode ?? false

    readonly property color primary: theme?.primary ?? "#D0BCFF"
    readonly property color contrastDark: theme?.contrastDark ?? "#000000"
    readonly property color contrastLight: theme?.contrastLight ?? "#ffffff"
    readonly property color primaryText: theme?.primaryText ?? "#381E72"
    readonly property color primaryContainer: theme?.primaryContainer ?? "#4F378B"
    readonly property real selectedContainerTint: theme?.selectedContainerTint ?? 0.2
    readonly property var selectionNeighbors: [hostSurface, cardSurface, chipSurface]
    readonly property real selectedContainerDistinction: 1.15
    readonly property bool themedSelectedContainer: theme?.themedSelectedContainer ?? (Contrast.isTonal(secondaryContainer, onSecondaryContainer) && Contrast.isTonal(secondaryContainer, surfaceText) && Contrast.worstRatio(secondaryContainer, selectionNeighbors) >= selectedContainerDistinction)
    readonly property color selectedContainer: theme?.selectedContainer ?? (themedSelectedContainer ? secondaryContainer : Contrast.distinctTint(chipSurface, primary, surfaceText, selectedContainerTint, selectionNeighbors, selectedContainerDistinction))
    readonly property color accentOnSelectedContainer: theme?.accentOnSelectedContainer ?? (Contrast.ratio(primary, selectedContainer) >= 3 ? primary : onSelectedContainer)
    readonly property color accentOnPrimaryContainer: theme?.accentOnPrimaryContainer ?? (Contrast.ratio(primary, primaryContainer) >= 3 ? primary : onPrimaryContainer)
    readonly property var accents: theme?.accents ?? Accents.derive(primary, isLightMode, null)
    readonly property color secondary: theme?.secondary ?? "#CCC2DC"
    readonly property color surface: theme?.surface ?? "#141218"
    readonly property color surfaceText: theme?.surfaceText ?? "#e6e0e9"
    readonly property color surfaceVariant: theme?.surfaceVariant ?? "#49454F"
    readonly property color surfaceVariantText: theme?.surfaceVariantText ?? "#CAC4D0"
    readonly property color surfaceTint: theme?.surfaceTint ?? primary
    readonly property color background: theme?.background ?? surface
    readonly property color outline: theme?.outline ?? "#938F99"
    readonly property color surfaceContainer: theme?.surfaceContainer ?? "#211F26"
    readonly property color surfaceContainerHigh: theme?.surfaceContainerHigh ?? "#2B2930"
    readonly property color error: theme?.error ?? "#F2B8B5"
    readonly property color errorContainer: theme?.errorContainer ?? "#8C1D18"
    readonly property color warning: theme?.warning ?? "#FF9800"
    readonly property color tertiary: theme?.tertiary ?? "#EFB8C8"
    readonly property color surfaceContainerLowest: theme?.surfaceContainerLowest ?? (isLightMode ? "#ffffff" : "#0f0d13")
    readonly property color surfaceContainerLow: theme?.surfaceContainerLow ?? (isLightMode ? "#f7f2fa" : "#1d1b20")
    readonly property color surfaceContainerHighest: theme?.surfaceContainerHighest ?? (isLightMode ? "#e6e0e9" : "#36343b")
    readonly property color surfaceBright: theme?.surfaceBright ?? "#3b383e"
    readonly property color surfaceDim: theme?.surfaceDim ?? surface
    readonly property var surfaceLadder: Tonal.surfaceLadder(surface, surfaceContainerLow, surfaceContainer, surfaceContainerHigh, surfaceContainerHighest)
    readonly property color hostSurface: theme?.hostSurface ?? surface
    readonly property color cardSurface: theme?.cardSurface ?? surfaceContainer
    readonly property color chipSurface: theme?.chipSurface ?? surfaceContainerHigh
    readonly property color chipSurfaceNested: theme?.chipSurfaceNested ?? surfaceContainerHighest
    readonly property color hostChipSurface: theme?.hostChipSurface ?? (Tonal.isLight(surface) ? cardSurface : chipSurface)
    readonly property color tooltipSurface: theme?.tooltipSurface ?? inverseSurface
    readonly property color tooltipText: theme?.tooltipText ?? Contrast.readableOn(tooltipSurface, [inverseOnSurface, surfaceText, contrastLight, contrastDark])
    readonly property color outlineVariant: theme?.outlineVariant ?? (isLightMode ? "#CAC4D0" : "#49454f")
    readonly property color secondaryContainer: theme?.secondaryContainer ?? (isLightMode ? "#E8DEF8" : "#4a4458")
    readonly property color tertiaryContainer: theme?.tertiaryContainer ?? (isLightMode ? "#FFD8E4" : "#633b48")
    readonly property color inverseSurface: theme?.inverseSurface ?? (isLightMode ? "#322F35" : "#E6E0E9")
    readonly property color inverseOnSurface: theme?.inverseOnSurface ?? (isLightMode ? "#F5EFF7" : "#322F35")

    property color onSurface
    property color onPrimary
    property color onSurfaceVariant
    property color onPrimaryContainer
    property color onSecondaryContainer
    property color onError
    property color onErrorContainer
    property color onTertiaryContainer
    property color onSelectedContainer
    readonly property color onSurface_10: theme?.onSurface_10 ?? withAlpha(onSurface, 0.1)
    readonly property color onSurface_12: theme?.onSurface_12 ?? withAlpha(onSurface, 0.12)
    readonly property color onSurface_38: theme?.onSurface_38 ?? withAlpha(onSurface, 0.38)
    readonly property color onSurfaceVariant_30: theme?.onSurfaceVariant_30 ?? withAlpha(onSurfaceVariant, 0.3)
    property color onSurfaceVariant_40

    readonly property list<QtObject> roleBindings: [
        Binding {
            target: root
            property: "onSurfaceVariant_40"
            value: root.theme?.onSurfaceVariant_40 ?? root.withAlpha(root.surfaceVariantText, 0.4)
        },
        Binding {
            target: root
            property: "onError"
            value: root.theme?.onError ?? Contrast.readableOn(root.error, [root.contrastLight, root.contrastDark])
        },
        Binding {
            target: root
            property: "onErrorContainer"
            value: root.theme?.onErrorContainer ?? (root.isLightMode ? "#410E0B" : "#F9DEDC")
        },
        Binding {
            target: root
            property: "onSurface"
            value: root.theme?.onSurface ?? root.surfaceText
        },
        Binding {
            target: root
            property: "onPrimary"
            value: root.theme?.onPrimary ?? root.primaryText
        },
        Binding {
            target: root
            property: "onSurfaceVariant"
            value: root.theme?.onSurfaceVariant ?? root.surfaceVariantText
        },
        Binding {
            target: root
            property: "onPrimaryContainer"
            value: root.theme?.onPrimaryContainer ?? (root.isLightMode ? "#21005D" : "#EADDFF")
        },
        Binding {
            target: root
            property: "onSecondaryContainer"
            value: root.theme?.onSecondaryContainer ?? (root.isLightMode ? "#1D192B" : "#E8DEF8")
        },
        Binding {
            target: root
            property: "onTertiaryContainer"
            value: root.theme?.onTertiaryContainer ?? (root.isLightMode ? "#31111D" : "#FFD8E4")
        },
        Binding {
            target: root
            property: "onSelectedContainer"
            value: root.theme?.onSelectedContainer ?? (root.themedSelectedContainer ? root.onSecondaryContainer : root.surfaceText)
        }
    ]
    readonly property real tonalTintAlpha: theme?.tonalTintAlpha ?? 0.16

    readonly property color primaryHover: theme?.primaryHover ?? withAlpha(primary, 0.12)
    readonly property color primaryHoverLight: theme?.primaryHoverLight ?? withAlpha(primary, 0.08)
    readonly property color primaryPressed: theme?.primaryPressed ?? withAlpha(primary, 0.16)
    readonly property color primarySelected: theme?.primarySelected ?? withAlpha(primary, 0.3)

    readonly property color surfaceHover: theme?.surfaceHover ?? withAlpha(surfaceText, stateLayerHover)
    readonly property color surfacePressed: theme?.surfacePressed ?? withAlpha(surfaceText, stateLayerPressed)
    readonly property color surfaceLight: theme?.surfaceLight ?? withAlpha(surfaceText, 0.03)
    readonly property color surfaceVariantAlpha: theme?.surfaceVariantAlpha ?? withAlpha(surfaceText, 0.06)

    readonly property color surfaceTextHover: theme?.surfaceTextHover ?? withAlpha(surfaceText, 0.08)
    readonly property color surfaceTextSecondary: theme?.surfaceTextSecondary ?? withAlpha(surfaceText, 0.6)
    readonly property color surfaceTextMedium: theme?.surfaceTextMedium ?? withAlpha(surfaceText, 0.7)

    readonly property color outlineButton: theme?.outlineButton ?? withAlpha(outline, 0.5)
    readonly property real layerOutlineOpacity: theme?.layerOutlineOpacity ?? 0.12
    readonly property color outlineMedium: theme?.outlineMedium ?? withAlpha(outline, layerOutlineOpacity)
    readonly property int layerOutlineWidth: theme?.layerOutlineWidth ?? (layerOutlineOpacity > 0 ? 1 : 0)
    readonly property color outlineStrong: theme?.outlineStrong ?? withAlpha(outline, Math.min(1, layerOutlineOpacity * 1.5))
    readonly property color outlineHeavy: theme?.outlineHeavy ?? withAlpha(outline, 0.2)

    readonly property color errorHover: theme?.errorHover ?? withAlpha(error, 0.12)
    readonly property color errorSelected: theme?.errorSelected ?? withAlpha(error, 0.3)

    readonly property real foregroundAlpha: theme?.foregroundAlpha ?? Surface.foregroundAlpha(theme?.foregroundLayers ?? true, theme?.foregroundLayerTransparency ?? 1)
    readonly property real floatingWindowForegroundAlpha: theme?.floatingWindowForegroundAlpha ?? Surface.foregroundAlpha(theme?.floatingWindowForegroundLayers ?? theme?.foregroundLayers ?? true, theme?.floatingWindowForegroundTransparency ?? theme?.foregroundLayerTransparency ?? 1)

    function accent(name) {
        return accents[name] ?? null;
    }

    function isFloatingWindow(item) {
        return Surface.isFloatingWindow(item);
    }

    function foregroundColor(baseColor, floatingWindow) {
        const fw = floatingWindow === true;
        return blendAlpha(baseColor, fw ? floatingWindowForegroundAlpha : foregroundAlpha);
    }

    function selectedColor(floatingWindow) {
        const fw = floatingWindow === true;
        return blendAlpha(selectedContainer, Math.max(selectedLayerMinAlpha, fw ? floatingWindowForegroundAlpha : foregroundAlpha));
    }

    readonly property color floatingSurface: theme?.floatingSurface ?? withAlpha(hostSurface, popupTransparency)
    readonly property color nestedSurface: theme?.nestedSurface ?? foregroundColor(cardSurface)
    readonly property real floatingWindowTransparency: theme?.floatingWindowTransparency ?? popupTransparency
    readonly property color floatingWindowSurface: theme?.floatingWindowSurface ?? withAlpha(hostSurface, floatingWindowTransparency)
    readonly property color floatingWindowNestedSurface: theme?.floatingWindowNestedSurface ?? foregroundColor(cardSurface, true)
    readonly property color floatingWindowFieldColor: theme?.floatingWindowFieldColor ?? foregroundColor(chipSurface, true)
    readonly property color floatingWindowFieldBorderColor: theme?.floatingWindowFieldBorderColor ?? withAlpha(outline, 0.16)
    readonly property color floatingWindowFieldFocusedBorderColor: theme?.floatingWindowFieldFocusedBorderColor ?? primary
    readonly property color popupFieldColor: theme?.popupFieldColor ?? foregroundColor(chipSurface)
    readonly property color popupFieldBorderColor: theme?.popupFieldBorderColor ?? withAlpha(outline, 0.16)
    readonly property color popupFieldFocusedBorderColor: theme?.popupFieldFocusedBorderColor ?? primary
    readonly property color shadowStrong: theme?.shadowStrong ?? Qt.rgba(0, 0, 0, 0.3)

    readonly property color buttonBg: theme?.buttonBg ?? primary
    readonly property color buttonText: theme?.buttonText ?? onPrimary
    readonly property color buttonHover: theme?.buttonHover ?? primaryHover
    readonly property color buttonPressed: theme?.buttonPressed ?? primaryPressed
    readonly property color widgetBaseHoverColor: theme?.widgetBaseHoverColor ?? _blend(surfaceContainer, primary, 0.1)

    readonly property int smallBreakpoint: theme?.smallBreakpoint ?? 480
    readonly property int mediumBreakpoint: theme?.mediumBreakpoint ?? 768

    readonly property real radiusStrength: theme?.radiusStrength ?? Shape.strengthFromRadius(theme?.cornerRadius ?? 12)
    readonly property real fixedRadius: theme?.fixedRadius ?? -1
    readonly property real shapeScale: {
        if (fixedRadius >= 0)
            return fixedRadius / Shape.corners.m;
        if (theme?.radiusStrength !== undefined)
            return Shape.scaleForStrength(radiusStrength);
        return theme?.shapeScale ?? Shape.scaleForStrength(radiusStrength);
    }
    readonly property real cornerRadius: cornerRadiusM
    readonly property real cornerRadiusXXS: theme?.cornerRadiusXXS ?? Shape.radius("xxs", shapeScale, fixedRadius)
    readonly property real cornerRadiusXS: theme?.cornerRadiusXS ?? Shape.radius("xs", shapeScale, fixedRadius)
    readonly property real cornerRadiusS: theme?.cornerRadiusS ?? Shape.radius("s", shapeScale, fixedRadius)
    readonly property real cornerRadiusM: theme?.cornerRadiusM ?? Shape.radius("m", shapeScale, fixedRadius)
    readonly property real cornerRadiusL: theme?.cornerRadiusL ?? Shape.radius("l", shapeScale, fixedRadius)
    readonly property real cornerRadiusLIncreased: theme?.cornerRadiusLIncreased ?? Shape.radius("lIncreased", shapeScale, fixedRadius)
    readonly property real cornerRadiusXL: theme?.cornerRadiusXL ?? Shape.radius("xl", shapeScale, fixedRadius)
    readonly property real cornerRadiusXLIncreased: theme?.cornerRadiusXLIncreased ?? Shape.radius("xlIncreased", shapeScale, fixedRadius)
    readonly property real cornerRadiusXXL: theme?.cornerRadiusXXL ?? Shape.radius("xxl", shapeScale, fixedRadius)
    readonly property real cornerRadiusFull: fixedRadius >= 0 ? fixedRadius : (shapeScale > 0 ? (theme?.cornerRadiusFull ?? 9999) : 0)
    readonly property real cornerRadiusSmall: cornerRadiusS
    readonly property real cornerRadiusLarge: cornerRadiusL
    readonly property real windowRadius: theme?.windowRadius ?? cornerRadiusL
    readonly property bool shapeMorph: theme?.shapeMorph ?? true
    readonly property bool pillControls: theme?.pillControls ?? true

    function iconRasterSize(size) {
        return Math.max(16, Math.ceil(size / 4) * 4);
    }

    function scaledRadius(radius, limit) {
        return Shape.scaledRadius(radius, limit, shapeScale, fixedRadius);
    }

    function innerRadius(token) {
        return Shape.innerRadius(token, shapeScale, fixedRadius);
    }

    function fullRadius(width, height) {
        return Shape.fullRadius(width, height, shapeScale, fixedRadius);
    }

    function buttonRadius(width, height, sizeHeight, pressed, round) {
        return Shape.buttonRadius(width, height, sizeHeight, pressed, round, shapeScale, fixedRadius, shapeMorph, pillControls);
    }

    readonly property int densityLevel: theme?.densityLevel ?? 0
    readonly property real menuItemIconSize: densityLevel < -1 ? iconSizeSmall : iconSizeMedium
    readonly property real menuRowHeight: menuItemHeight + densityLevel * spacingXS
    readonly property real menuPadding: spacingS + densityLevel * spacingXXS
    readonly property real groupedListGap: theme?.groupedListGap ?? spacingXXS
    readonly property real groupedListInnerRadius: theme?.groupedListInnerRadius ?? innerRadius("xs")
    readonly property real groupedListOuterRadius: theme?.groupedListOuterRadius ?? cornerRadiusL
    readonly property real spacingXXS: theme?.spacingXXS ?? 2
    readonly property real spacingXS: theme?.spacingXS ?? 4
    readonly property real spacingS: theme?.spacingS ?? 8
    readonly property real spacingM: theme?.spacingM ?? 12
    readonly property real windowInset: theme?.windowInset ?? spacingM
    readonly property real spacingL: theme?.spacingL ?? 16
    readonly property real spacingXL: theme?.spacingXL ?? 24
    readonly property real fontSizeXSmall: theme?.fontSizeXSmall ?? 10
    readonly property real fontSizeSmall: theme?.fontSizeSmall ?? 12
    readonly property real fontScale: theme?.fontScale ?? 1
    readonly property real fontSizeMedium: theme?.fontSizeMedium ?? 14
    readonly property real fontSizeLarge: theme?.fontSizeLarge ?? 16
    readonly property real fontSizeXLarge: theme?.fontSizeXLarge ?? 20
    readonly property real fontSizeXXLarge: theme?.fontSizeXXLarge ?? 28
    readonly property real fontSizeDisplay: theme?.fontSizeDisplay ?? 36
    readonly property real fontSizeDisplayLarge: theme?.fontSizeDisplayLarge ?? 57
    readonly property string defaultFontFamily: theme?.defaultFontFamily ?? Fonts.sans
    readonly property real iconSize: theme?.iconSize ?? 24
    readonly property real iconSizeSmall: theme?.iconSizeSmall ?? 16
    readonly property real iconSizeMedium: theme?.iconSizeMedium ?? 20
    readonly property real iconSizeLarge: theme?.iconSizeLarge ?? 32
    readonly property real iconButtonSize: theme?.iconButtonSize ?? 40
    readonly property real minimumTouchTargetSize: theme?.minimumTouchTargetSize ?? 48
    readonly property real listItemHeight: theme?.listItemHeight ?? 56
    readonly property real listItemTwoLineHeight: theme?.listItemTwoLineHeight ?? 72
    readonly property real avatarSize: theme?.avatarSize ?? 36
    readonly property real osdHeight: theme?.osdHeight ?? 60
    readonly property real dialogMaxWidth: theme?.dialogMaxWidth ?? 560
    readonly property real sidebarWidth: theme?.sidebarWidth ?? 220
    readonly property real bottomSheetHandleWidth: theme?.bottomSheetHandleWidth ?? 36
    readonly property real bottomSheetHandleHeight: theme?.bottomSheetHandleHeight ?? 4
    readonly property real sliderTrackHeight: theme?.sliderTrackHeight ?? 16
    readonly property real sliderHandleWidth: theme?.sliderHandleWidth ?? 4
    readonly property real sliderHandleWidthPressed: theme?.sliderHandleWidthPressed ?? 2
    readonly property real sliderHandleHeight: theme?.sliderHandleHeight ?? 44
    readonly property real sliderHandleGap: theme?.sliderHandleGap ?? 6
    readonly property real sliderTrackHeightS: theme?.sliderTrackHeightS ?? 24
    readonly property real sliderHandleHeightS: theme?.sliderHandleHeightS ?? 36
    readonly property real sliderTrackHeightM: theme?.sliderTrackHeightM ?? 40
    readonly property real sliderHandleHeightM: theme?.sliderHandleHeightM ?? 52
    readonly property real sliderTrackHeightL: theme?.sliderTrackHeightL ?? 56
    readonly property real sliderHandleHeightL: theme?.sliderHandleHeightL ?? 68
    readonly property real sliderTrackHeightXL: theme?.sliderTrackHeightXL ?? 96
    readonly property real sliderHandleHeightXL: theme?.sliderHandleHeightXL ?? 108
    readonly property real sliderTrackCornerRadius: theme?.sliderTrackCornerRadius ?? 8
    readonly property real sliderTrackCornerRadiusS: theme?.sliderTrackCornerRadiusS ?? 8
    readonly property real sliderTrackCornerRadiusM: theme?.sliderTrackCornerRadiusM ?? 12
    readonly property real sliderTrackCornerRadiusL: theme?.sliderTrackCornerRadiusL ?? 16
    readonly property real sliderTrackCornerRadiusXL: theme?.sliderTrackCornerRadiusXL ?? 28
    readonly property real sliderTrackInsideCornerRadius: theme?.sliderTrackInsideCornerRadius ?? 2
    readonly property real switchTrackWidth: theme?.switchTrackWidth ?? 52
    readonly property real switchTrackHeight: theme?.switchTrackHeight ?? 32
    readonly property real switchOutlineWidth: theme?.switchOutlineWidth ?? 2
    readonly property real switchThumbUnselected: theme?.switchThumbUnselected ?? 16
    readonly property real switchThumbSelected: theme?.switchThumbSelected ?? 24
    readonly property real switchThumbPressed: theme?.switchThumbPressed ?? 28
    readonly property real sliderStopSize: theme?.sliderStopSize ?? 4
    readonly property real sliderTickSize: theme?.sliderTickSize ?? 3
    readonly property real sliderTrackMinAlpha: theme?.sliderTrackMinAlpha ?? 0.4
    readonly property real selectedLayerMinAlpha: theme?.selectedLayerMinAlpha ?? 0.4
    readonly property real menuItemHeight: theme?.menuItemHeight ?? 40
    readonly property real outlineWidth: theme?.outlineWidth ?? 1
    readonly property real outlineWidthFocused: theme?.outlineWidthFocused ?? 2
    readonly property real dividerWidth: theme?.dividerWidth ?? 1
    readonly property real focusRingWidth: theme?.focusRingWidth ?? 2
    readonly property real focusRingOffset: theme?.focusRingOffset ?? 4
    readonly property color focusRingColor: theme?.focusRingColor ?? primary
    readonly property real scrimAlpha: theme?.scrimAlpha ?? 0.55
    readonly property color scrimColor: theme?.scrimColor ?? "#000000"
    readonly property color backdropColor: theme?.backdropColor ?? scrimColor
    readonly property real buttonHeightXXS: theme?.buttonHeightXXS ?? 28
    readonly property real buttonHeightXS: theme?.buttonHeightXS ?? 32
    readonly property real buttonHeightS: theme?.buttonHeightS ?? 40
    readonly property real buttonHeightM: theme?.buttonHeightM ?? 56
    readonly property real buttonMinWidth: theme?.buttonMinWidth ?? 58
    readonly property real pressScale: theme?.pressScale ?? 0.98
    readonly property real iconEnterScale: theme?.iconEnterScale ?? 0.6
    readonly property real popupEnterScale: theme?.popupEnterScale ?? 0.92
    readonly property real pendingOpacity: theme?.pendingOpacity ?? 0.6
    readonly property real spinnerStrokeWidth: theme?.spinnerStrokeWidth ?? 2
    readonly property real tabMinWidth: theme?.tabMinWidth ?? 64
    readonly property real tabIndicatorHeight: theme?.tabIndicatorHeight ?? 3
    readonly property real tabIndicatorMinWidth: theme?.tabIndicatorMinWidth ?? 24
    readonly property real tabIndicatorInset: theme?.tabIndicatorInset ?? 2
    readonly property real fieldDefaultWidth: theme?.fieldDefaultWidth ?? 200
    readonly property real fieldHeight: theme?.fieldHeight ?? Math.round(fontSizeMedium * 3) + densityLevel * spacingXS
    readonly property real fieldHeightLarge: theme?.fieldHeightLarge ?? 48 + densityLevel * spacingXS
    readonly property real outlinedFieldLabelLineHeight: theme?.outlinedFieldLabelLineHeight ?? 16
    readonly property real textEditHeight: theme?.textEditHeight ?? Math.round(fontSizeMedium * 8)
    readonly property real tooltipMaxWidth: theme?.tooltipMaxWidth ?? 300
    readonly property int tooltipDelay: theme?.tooltipDelay ?? 400
    readonly property real tooltipEnterScale: theme?.tooltipEnterScale ?? 0.8
    readonly property real scrollbarThickness: theme?.scrollbarThickness ?? 6
    readonly property real scrollbarGap: theme?.scrollbarGap ?? spacingXS
    readonly property int scrollbarHideDelay: theme?.scrollbarHideDelay ?? 1200
    readonly property real menuMaxHeight: theme?.menuMaxHeight ?? 400
    readonly property real chipIconSize: theme?.chipIconSize ?? 18
    readonly property string fontFamily: theme?.fontFamily ?? Fonts.sans
    readonly property string monoFontFamily: theme?.monoFontFamily ?? Fonts.mono
    readonly property string displayFontFamily: theme?.displayFontFamily ?? Fonts.display
    readonly property string passwordMaskCharacter: "●"

    function fontFor(token) {
        switch (token) {
        case "":
        case "ui":
            return fontFamily;
        case "mono":
            return monoFontFamily;
        case "display":
            return displayFontFamily;
        default:
            return token;
        }
    }

    function isUiFontToken(token) {
        switch (token) {
        case "":
        case "ui":
        case "mono":
            return true;
        default:
            return false;
        }
    }

    function fontWeightFor(token) {
        return isUiFontToken(token) ? fontWeight : Font.Normal;
    }

    readonly property int fontWeight: theme?.fontWeight ?? Font.Normal
    readonly property int fontWeightMedium: shiftedFontWeight(Font.Medium)
    readonly property int fontWeightBold: shiftedFontWeight(Font.Bold)
    readonly property real popupTransparency: theme?.popupTransparency ?? 1.0

    function shiftedFontWeight(weight) {
        return Math.max(Font.Thin, Math.min(Font.Black, weight + fontWeight - Font.Normal));
    }

    readonly property real motionScale: theme?.motionScale ?? 1
    readonly property bool motionOff: theme?.motionOff ?? false

    function motionMs(specMs) {
        return Math.round(specMs * motionScale);
    }

    readonly property int shorterDuration: theme?.shorterDuration ?? 100
    readonly property int shortDuration: theme?.shortDuration ?? 150
    readonly property int mediumDuration: theme?.mediumDuration ?? 300

    readonly property var expressiveCurves: theme?.expressiveCurves ?? ({
            "expressiveFastSpatial": [0.42, 1.67, 0.21, 0.9, 1, 1],
            "expressiveDefaultSpatial": [0.38, 1.21, 0.22, 1, 1, 1],
            "expressiveSlowSpatial": [0.39, 1.29, 0.35, 0.98, 1, 1],
            "expressiveFastEffects": [0.31, 0.94, 0.34, 1, 1, 1],
            "expressiveEffects": [0.34, 0.8, 0.34, 1, 1, 1],
            "expressiveSlowEffects": [0.34, 0.88, 0.34, 1, 1, 1]
        })

    readonly property var expressiveDurations: theme?.expressiveDurations ?? ({
            "expressiveFastSpatial": 350,
            "expressiveDefaultSpatial": 500,
            "expressiveSlowSpatial": 650,
            "expressiveFastEffects": 150,
            "expressiveEffects": 200,
            "expressiveSlowEffects": 300
        })

    readonly property bool elevationEnabled: theme?.elevationEnabled ?? true
    readonly property string elevationLightDirection: theme?.elevationLightDirection ?? "top"
    readonly property var elevationLevel1: theme?.elevationLevel1 ?? ({
            blurPx: 4,
            offsetX: 0,
            offsetY: 1,
            spreadPx: 0,
            alpha: 0.2
        })
    readonly property var elevationLevel2: theme?.elevationLevel2 ?? ({
            blurPx: 8,
            offsetX: 0,
            offsetY: 4,
            spreadPx: 0,
            alpha: 0.25
        })
    readonly property var elevationLevel3: theme?.elevationLevel3 ?? ({
            blurPx: 12,
            offsetX: 0,
            offsetY: 6,
            spreadPx: 0,
            alpha: 0.3
        })
    readonly property var elevationLevel4: theme?.elevationLevel4 ?? ({
            blurPx: 16,
            offsetX: 0,
            offsetY: 8,
            spreadPx: 0,
            alpha: 0.32
        })
    readonly property var elevationLevel5: theme?.elevationLevel5 ?? ({
            blurPx: 24,
            offsetX: 0,
            offsetY: 12,
            spreadPx: 0,
            alpha: 0.35
        })

    readonly property real stateLayerHover: theme?.stateLayerHover ?? 0.08
    readonly property real stateLayerFocus: theme?.stateLayerFocus ?? 0.1
    readonly property real stateLayerPressed: theme?.stateLayerPressed ?? 0.1
    readonly property real stateLayerDrag: theme?.stateLayerDrag ?? 0.16

    readonly property var springSpecs: theme?.springSpecs ?? ({
            "fastSpatial": [800, 33.9],
            "defaultSpatial": [380, 31.2],
            "slowSpatial": [200, 22.6],
            "fastEffects": [3800, 123.3],
            "defaultEffects": [1600, 80],
            "slowEffects": [800, 56.6]
        })
    readonly property var springDampingScales: theme?.springDampingScales ?? [1.22, 1.0, 0.82]

    readonly property bool blurLayersActive: false
    readonly property bool connectedSurfaceBlurEnabled: false
    readonly property color blurBorderColor: "transparent"
    readonly property int blurBorderWidth: 0

    readonly property bool enableRippleEffects: settings?.enableRippleEffects ?? true
    readonly property bool popoutElevationEnabled: settings?.popoutElevationEnabled ?? true
    readonly property bool scrollbarsEnabled: settings?.scrollbarsEnabled ?? true
    readonly property bool scrollShadowsEnabled: settings?.scrollShadowsEnabled ?? true
    readonly property bool switchStateIcons: settings?.switchStateIcons ?? false
    readonly property int textRenderType: settings?.textRenderType ?? Style.TextRenderType.Qt
    readonly property int textRenderQuality: settings?.textRenderQuality ?? Style.TextRenderQuality.Default
    readonly property bool reduceMotion: theme?.reduceMotion ?? settings?.reduceMotion ?? false
    readonly property int springBounce: settings?.springBounce ?? 1

    function springPreset(name) {
        if (theme && typeof theme.springPreset === "function")
            return theme.springPreset(name);
        const spec = springSpecs[name];
        const dampingScale = name.endsWith("Spatial") ? (springDampingScales[springBounce] ?? 1) : 1;
        return {
            "stiffness": spec[0],
            "damping": spec[1] * dampingScale,
            "mass": 1
        };
    }

    function toColor(c) {
        if (!c)
            return null;
        if (c.r !== undefined)
            return c;
        if (typeof c === "string") {
            const s = c.trim();
            if (s.startsWith("#")) {
                if (s.length === 7) {
                    return Qt.rgba(
                        parseInt(s.slice(1, 3), 16) / 255,
                        parseInt(s.slice(3, 5), 16) / 255,
                        parseInt(s.slice(5, 7), 16) / 255,
                        1
                    );
                }
                if (s.length === 9) {
                    return Qt.rgba(
                        parseInt(s.slice(3, 5), 16) / 255,
                        parseInt(s.slice(5, 7), 16) / 255,
                        parseInt(s.slice(7, 9), 16) / 255,
                        parseInt(s.slice(1, 3), 16) / 255
                    );
                }
            }
            try {
                return Qt.lighter(s, 1.0);
            } catch (e) {
                return null;
            }
        }
        return null;
    }

    function withAlpha(c, a) {
        const col = toColor(c);
        if (!col)
            return Qt.rgba(0, 0, 0, 0);
        return Qt.rgba(col.r, col.g, col.b, a);
    }

    function blendAlpha(c, a) {
        const col = toColor(c);
        if (!col)
            return Qt.rgba(0, 0, 0, 0);
        return Qt.rgba(col.r, col.g, col.b, col.a * a);
    }

    function _blend(c1, c2, r) {
        const a = toColor(c1) ?? Qt.rgba(0, 0, 0, 1);
        const b = toColor(c2) ?? Qt.rgba(1, 1, 1, 1);
        return Qt.rgba(a.r * (1 - r) + b.r * r, a.g * (1 - r) + b.g * r, a.b * (1 - r) + b.b * r, a.a * (1 - r) + b.a * r);
    }

    function elevationOffsetXFor(level, direction, fallback) {
        if (theme && typeof theme.elevationOffsetXFor === "function")
            return theme.elevationOffsetXFor(level, direction, fallback);
        return level?.offsetX ?? 0;
    }

    function elevationOffsetYFor(level, direction, fallback) {
        if (theme && typeof theme.elevationOffsetYFor === "function")
            return theme.elevationOffsetYFor(level, direction, fallback);
        return level?.offsetY ?? (fallback ?? 0);
    }

    function elevationShadowColor(level) {
        if (theme && typeof theme.elevationShadowColor === "function")
            return theme.elevationShadowColor(level);
        return Qt.rgba(0, 0, 0, level?.alpha ?? 0.3);
    }

    function elevationAmbient(level) {
        if (theme && typeof theme.elevationAmbient === "function")
            return theme.elevationAmbient(level);
        return {
            blurPx: (level?.blurPx ?? 0) * 1.75,
            spreadPx: 1,
            alpha: (level?.alpha ?? 0.3) * 0.5
        };
    }
}
