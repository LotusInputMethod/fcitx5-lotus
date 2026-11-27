.pragma library
.import "Contrast.js" as Contrast
.import "Hct.js" as Hct

var SLOTS = ["red", "orange", "yellow", "green", "teal", "blue", "purple", "pink"];
var ANCHOR_HUES = {
    "red": 25,
    "orange": 55,
    "yellow": 90,
    "green": 150,
    "teal": 195,
    "blue": 260,
    "purple": 300,
    "pink": 345
};
var NEUTRAL_CHROMA = 8;
var FILL_TONE_DARK = 77;
var FILL_TONE_LIGHT = 86;
var FILL_CHROMA_MIN = 16;
var FILL_CHROMA_MAX = 33;
var GLYPH_CHROMA_MIN = 23;
var GLYPH_CHROMA_MAX = 46;
var KEY_CHROMA_FILL_SCALE = 0.8;
var TARGET_RATIO = 4.5;

function parseColor(c) {
    if (!c)
        return null;
    if (c.r !== undefined)
        return c;
    return Qt.lighter(c, 1.0);
}

function clampChroma(value, min, max) {
    return Math.min(max, Math.max(min, value));
}

function readableGlyph(fill, fillTone, chroma, hue) {
    let tone = Hct.darkerTone(fillTone, TARGET_RATIO);
    if (tone < 0)
        tone = 0;
    let glyph = Hct.fromHct(hue, chroma, tone);
    while (tone > 0 && Contrast.ratio(glyph, fill) < TARGET_RATIO) {
        tone = Math.max(0, tone - 1);
        glyph = Hct.fromHct(hue, chroma, tone);
    }
    return glyph;
}

function fittedChroma(hues, chroma, tone) {
    let fitted = chroma;
    for (const hue of hues)
        fitted = Math.min(fitted, Hct.toHct(Hct.fromHct(hue, chroma, tone)).chroma);
    return fitted;
}

function derive(primary, isLight, overrides) {
    const key = Hct.toHct(primary);
    const neutralKey = key.chroma < NEUTRAL_CHROMA;
    const fillTone = isLight ? FILL_TONE_LIGHT : FILL_TONE_DARK;
    const slots = SLOTS.map(slot => {
        const override = overrides && overrides[slot] ? Hct.toHct(parseColor(overrides[slot])) : null;
        return {
            "name": slot,
            "override": override,
            "hue": override ? override.hue : (neutralKey ? ANCHOR_HUES[slot] : Hct.harmonize(ANCHOR_HUES[slot], key.hue))
        };
    });
    const sharedHues = slots.filter(slot => !slot.override).map(slot => slot.hue);
    const fillChroma = fittedChroma(sharedHues, clampChroma(key.chroma * KEY_CHROMA_FILL_SCALE, FILL_CHROMA_MIN, FILL_CHROMA_MAX), fillTone);
    const accents = {};
    for (const slot of slots) {
        const fill = Hct.fromHct(slot.hue, slot.override ? clampChroma(slot.override.chroma * KEY_CHROMA_FILL_SCALE, FILL_CHROMA_MIN, FILL_CHROMA_MAX) : fillChroma, fillTone);
        accents[slot.name] = {
            "container": fill,
            "onContainer": readableGlyph(fill, fillTone, clampChroma((slot.override ? slot.override.chroma : key.chroma), GLYPH_CHROMA_MIN, GLYPH_CHROMA_MAX), slot.hue)
        };
    }
    return accents;
}
