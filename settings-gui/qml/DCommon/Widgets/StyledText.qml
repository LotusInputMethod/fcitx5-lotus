import QtQuick
import DCommon.Common

Text {
    property bool isMonospace: false
    property string fontToken: isMonospace ? "mono" : "ui"

    readonly property string resolvedFontFamily: Style.fontFor(fontToken)

    readonly property int resolvedRenderType: {
        if (!Style.isUiFontToken(fontToken))
            return Text.NativeRendering;
        switch (Style.textRenderType) {
        case Style.TextRenderType.Qt:
        case Style.TextRenderType.Curve:
            return Text.QtRendering;
        default:
            return Text.NativeRendering;
        }
    }

    readonly property int resolvedRenderQuality: {
        switch (Style.textRenderQuality) {
        case Style.TextRenderQuality.Low:
            return Text.LowRenderTypeQuality;
        case Style.TextRenderQuality.Normal:
            return Text.NormalRenderTypeQuality;
        case Style.TextRenderQuality.High:
            return Text.HighRenderTypeQuality;
        case Style.TextRenderQuality.VeryHigh:
            return Text.VeryHighRenderTypeQuality;
        default:
            return Text.DefaultRenderTypeQuality;
        }
    }

    color: Style.surfaceText
    font.pixelSize: Appearance.fontSize.normal
    font.family: resolvedFontFamily
    font.weight: Style.fontWeightFor(fontToken)
    textFormat: Text.PlainText
    wrapMode: Text.WordWrap
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
    renderType: resolvedRenderType
    renderTypeQuality: resolvedRenderQuality

    Behavior on opacity {
        NumberAnimation {
            duration: Style.expressiveDurations.expressiveSlowEffects
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Style.expressiveCurves.expressiveSlowEffects
        }
    }
}
