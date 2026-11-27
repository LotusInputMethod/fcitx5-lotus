.pragma library

// contentY that keeps [top, bottom] (content coordinates) clear of the fade strips, top edge winning
function contentYFor(view, top, bottom, insetTop, insetBottom) {
    const minY = view.originY;
    const maxY = Math.max(minY, minY + view.contentHeight - view.height);
    let y = view.contentY;
    if (bottom > y + view.height - insetBottom)
        y = bottom - view.height + insetBottom;
    if (top < y + insetTop)
        y = top - insetTop;
    return Math.max(minY, Math.min(maxY, y));
}
