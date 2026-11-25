.pragma library

function containsFocus(item) {
    if (!item)
        return false;
    if (item.activeFocus)
        return true;
    return Array.from(item.children || []).some(containsFocus);
}

function focusItem(item, backwards) {
    if (!item || !item.visible || !item.enabled)
        return false;
    if (typeof item.requestFocus === "function") {
        item.requestFocus(backwards);
        return true;
    }
    item.forceActiveFocus(backwards ? Qt.BacktabFocusReason : Qt.TabFocusReason);
    return true;
}

function moveFocus(items, backwards) {
    const targets = items.filter(item => item?.visible && item.enabled);
    const index = targets.findIndex(containsFocus);
    const next = index < 0 ? (backwards ? targets.length - 1 : 0) : index + (backwards ? -1 : 1);
    return focusItem(targets[next], backwards);
}

// the items Tab can land on under item, in tree order
function tabStops(item, stops) {
    const out = stops ?? [];
    if (!item || !item.visible || !item.enabled)
        return out;
    if (item.activeFocusOnTab)
        out.push(item);
    for (const child of Array.from(item.children || []))
        tabStops(child, out);
    return out;
}

// whether focus on item is fine for an open overlay dialog: it's inside it, or inside one opened after it
function keepsFocus(dialog, item) {
    for (let a = item; a; a = a.parent) {
        if (a === dialog || a.overlayOpenedAt > dialog.overlayOpenedAt)
            return true;
    }
    return false;
}

function handleHorizontalKey(event, items, rtl) {
    if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
        return false;
    switch (event.key) {
    case Qt.Key_Left:
    case Qt.Key_Right:
        moveFocus(items, (event.key === Qt.Key_Left) !== rtl);
        return true;
    }
    return false;
}
