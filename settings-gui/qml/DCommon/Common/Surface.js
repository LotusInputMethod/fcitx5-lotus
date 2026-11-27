.pragma library

function foregroundAlpha(enabled, opacity) {
    if (!enabled)
        return 0;
    if (typeof opacity !== "number" || !isFinite(opacity))
        return 1;
    return Math.max(0, Math.min(1, opacity));
}

function isFloatingWindow(item) {
    for (let current = item; current; current = current.parent) {
        if (typeof current.isFloatingWindowSurface === "boolean")
            return current.isFloatingWindowSurface;
        if (current.disablePopupTransparency === true)
            return true;
    }
    return false;
}

function composite(paintsNearestFirst) {
    let r = 0;
    let g = 0;
    let b = 0;
    let a = 0;
    for (const paint of paintsNearestFirst) {
        const weight = paint.a * (1 - a);
        r += paint.r * weight;
        g += paint.g * weight;
        b += paint.b * weight;
        a += weight;
        if (a >= 0.999)
            break;
    }
    if (a <= 0)
        return {
            r: 0,
            g: 0,
            b: 0,
            a: 0
        };
    return {
        r: r / a,
        g: g / a,
        b: b / a,
        a: a
    };
}
