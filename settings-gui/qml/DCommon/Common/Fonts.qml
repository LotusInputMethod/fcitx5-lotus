pragma Singleton

import QtQuick

QtObject {
    readonly property string sans: metrics.font.family
    readonly property string mono: "monospace"
    readonly property string icons: metrics.font.family
    readonly property string nerd: "monospace"
    readonly property string display: metrics.font.family
    readonly property string system: metrics.font.family
    readonly property string systemMono: "monospace"

    function widestDigit(font) {
        metrics.font = font;
        let widest = "0";
        let widestAdvance = 0;
        for (const digit of "0123456789") {
            const advance = metrics.advanceWidth(digit);
            if (advance <= widestAdvance)
                continue;
            widestAdvance = advance;
            widest = digit;
        }
        return widest;
    }

    function textWidth(font, text) {
        if (text === "")
            return 0;
        metrics.font = font;
        const last = text.slice(-1);
        const trailing = /\d/.test(last) ? "0123456789" : last;
        let overhang = 0;
        for (const glyph of trailing) {
            const box = metrics.boundingRect(glyph);
            overhang = Math.max(overhang, box.x + box.width - Math.round(metrics.advanceWidth(glyph)));
        }
        return metrics.advanceWidth(text) + overhang;
    }

    property FontMetrics metrics: FontMetrics {}
}
