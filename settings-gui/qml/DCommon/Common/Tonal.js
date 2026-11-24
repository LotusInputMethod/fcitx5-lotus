.pragma library
.import "Contrast.js" as Contrast
.import "Hct.js" as Hct

var CONTAINER_TINT_DARK = 0.5;
var CONTAINER_TINT_LIGHT = 1;
var CONTAINER_CHROMA_MAX = 48;
var DARK_TONE_LIMIT = 50;
var DARK_TONE_FLOOR = 30;
var DARK_TONE_LIFT = 16;
var LIGHT_TONE_CEILING = 90;
var LIGHT_TONE_DROP = 4;
var LIGHT_SURFACE_TONE = 50;
var LIGHT_HOST_DROP = 11;
var LIGHT_CHIP_DROP = 6;
var LIGHT_NESTED_DROP = 10.5;
var LIGHT_RECESS_DROP = 4;

function defaultTint(card) {
    return Hct.toHct(card).tone < DARK_TONE_LIMIT ? CONTAINER_TINT_DARK : CONTAINER_TINT_LIGHT;
}

// Fading toward the card's own hue, not grey: a grey-ward purple reads red on a blue-tinted surface.
function softContainer(accent, card, tint) {
    const source = Hct.toHct(accent);
    const surface = Hct.toHct(card);
    const tone = surface.tone < DARK_TONE_LIMIT ? Math.max(DARK_TONE_FLOOR, surface.tone + DARK_TONE_LIFT) : Math.min(LIGHT_TONE_CEILING, surface.tone - LIGHT_TONE_DROP);
    const base = Hct.fromHct(surface.hue, surface.chroma, tone);
    const full = Hct.fromHct(source.hue, Math.min(source.chroma, CONTAINER_CHROMA_MAX), tone);
    return Contrast.mix(base, full, Math.max(0, Math.min(1, tint)));
}

function isLight(color) {
    return Hct.toHct(color).tone >= LIGHT_SURFACE_TONE;
}

function lighterOf(first, second) {
    return Hct.toHct(second).tone > Hct.toHct(first).tone ? second : first;
}

function dropTone(color, drop) {
    const source = Hct.toHct(color);
    return Hct.fromHct(source.hue, source.chroma, Math.max(0, source.tone - drop));
}

// Light: the card is the brightest surface, the host sits far enough below to clear a hovered card, chips step down inside it
function surfaceLadder(surface, low, container, high, highest) {
    if (!isLight(surface))
        return {
            host: surface,
            card: container,
            chip: high,
            nested: highest
        };
    const card = lighterOf(surface, low);
    return {
        host: dropTone(card, LIGHT_HOST_DROP),
        card: card,
        chip: dropTone(card, LIGHT_CHIP_DROP),
        nested: dropTone(card, LIGHT_NESTED_DROP)
    };
}

// Steps an accent container's tone off the surface roles it blends into, darker on light surfaces
function standApart(color, neighbors, distinction) {
    if (Contrast.worstRatio(color, neighbors) >= distinction)
        return color;
    const source = Hct.toHct(color);
    const step = isLight(neighbors[0]) ? -1 : 1;
    for (let tone = source.tone + step; tone >= 0 && tone <= 100; tone += step) {
        const candidate = Hct.fromHct(source.hue, source.chroma, tone);
        if (Contrast.worstRatio(candidate, neighbors) >= distinction)
            return candidate;
    }
    return color;
}
