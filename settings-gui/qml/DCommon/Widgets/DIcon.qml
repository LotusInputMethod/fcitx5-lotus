// Qt 6.2+ compatible vector/theme icon component (zero font-TTF or MultiEffect dependency)
import QtQuick
import DCommon.Common

Item {
    id: root

    property string name: ""
    property real size: Style.fontSizeMedium
    property color color: Style.surfaceText
    property bool filled: false
    property real fill: filled ? 1.0 : 0.0
    property int grade: Style.isLightMode ? 0 : -25
    property int weight: filled ? 500 : 400
    property bool smoothTransform: false

    readonly property string canonicalName: {
        switch (name) {
        case "window-close-symbolic":
        case "close":
            return "close";
        case "check":
            return "check";
        case "edit-undo-symbolic":
        case "edit-undo":
        case "undo":
            return "undo";
        case "list-add-symbolic":
        case "list-add":
        case "add":
            return "add";
        case "list-remove-symbolic":
        case "list-remove":
        case "remove":
            return "remove";
        case "user-trash-symbolic":
        case "user-trash":
        case "delete":
            return "delete";
        case "edit-find-symbolic":
        case "edit-find":
        case "search":
            return "search";
        case "drag_indicator":
            return "drag_indicator";
        case "arrow_drop_down":
        case "expand_more":
        case "pan-down-symbolic":
        case "keyboard_arrow_down":
        case "go-down-symbolic":
            return "arrow_down";
        case "keyboard_arrow_up":
        case "go-up-symbolic":
        case "expand_less":
            return "arrow_up";
        case "chevron_right":
        case "go-next-symbolic":
            return "chevron_right";
        case "chevron_left":
        case "go-previous-symbolic":
            return "chevron_left";
        case "keyboard_return":
            return "keyboard_return";
        case "warning":
        case "dialog-warning-symbolic":
            return "warning";
        case "info":
        case "dialog-information-symbolic":
            return "info";
        case "error":
        case "dialog-error-symbolic":
            return "error";
        case "help":
        case "help-about-symbolic":
            return "help";
        case "settings":
        case "preferences-system-symbolic":
            return "settings";
        case "tune":
        case "accessories-character-map-symbolic":
            return "tune";
        case "keyboard":
        case "preferences-desktop-keyboard-shortcuts-symbolic":
        case "configure-shortcuts-symbolic":
        case "input-keyboard-symbolic":
        case "preferences-desktop-keyboard":
            return "keyboard";
        case "apps":
        case "applications-system-symbolic":
            return "apps";
        case "insert-text-symbolic":
        case "code":
        case "edit_note":
            return "macros";
        case "group":
        case "people":
            return "group";
        case "book":
        case "menu_book":
        case "accessories-dictionary-symbolic":
            return "dictionary";
        case "swap_horiz":
        case "keyboard_alt":
            return "keymap";
        case "save":
        case "document-save-symbolic":
        case "document-save-as-symbolic":
            return "save";
        case "folder_open":
        case "document-open-symbolic":
            return "open";
        case "refresh":
        case "view-refresh-symbolic":
            return "refresh";
        case "visibility":
            return "visibility";
        case "visibility_off":
            return "visibility_off";
        case "open_in_new":
            return "open_in_new";
        case "content_copy":
        case "edit-copy-symbolic":
            return "copy";
        case "edit":
        case "document-edit-symbolic":
            return "edit";
        case "minimize":
            return "minimize";
        case "fullscreen":
            return "fullscreen";
        case "fullscreen_exit":
            return "fullscreen_exit";
        default:
            return "";
        }
    }

    readonly property bool isBuiltinGlyph: canonicalName !== ""

    implicitWidth: Math.round(size)
    implicitHeight: Math.round(size)

    function emCenterShift() {
        return 0;
    }

    onCanonicalNameChanged: glyphCanvas.requestPaint()
    onColorChanged: glyphCanvas.requestPaint()
    onFilledChanged: glyphCanvas.requestPaint()
    onWidthChanged: glyphCanvas.requestPaint()
    onHeightChanged: glyphCanvas.requestPaint()

    Canvas {
        id: glyphCanvas
        anchors.fill: parent
        visible: root.isBuiltinGlyph
        antialiasing: true
        renderStrategy: Canvas.Immediate
        Component.onCompleted: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            const w = width;
            const h = height;
            ctx.reset();
            if (w <= 0 || h <= 0 || !root.isBuiltinGlyph)
                return;

            const s = Math.min(w, h) / 24.0;
            ctx.save();
            ctx.translate((w - 24 * s) / 2, (h - 24 * s) / 2);
            ctx.scale(s, s);
            ctx.strokeStyle = root.color;
            ctx.fillStyle = root.color;
            ctx.lineWidth = root.filled ? 2.2 : 1.9;
            ctx.lineCap = "round";
            ctx.lineJoin = "round";

            switch (root.canonicalName) {
            case "check":
                ctx.beginPath();
                ctx.moveTo(5, 12.5);
                ctx.lineTo(10, 17.5);
                ctx.lineTo(19, 7);
                ctx.stroke();
                break;
            case "close":
                ctx.beginPath();
                ctx.moveTo(6, 6);
                ctx.lineTo(18, 18);
                ctx.moveTo(18, 6);
                ctx.lineTo(6, 18);
                ctx.stroke();
                break;
            case "add":
                ctx.beginPath();
                ctx.moveTo(12, 5);
                ctx.lineTo(12, 19);
                ctx.moveTo(5, 12);
                ctx.lineTo(19, 12);
                ctx.stroke();
                break;
            case "remove":
                ctx.beginPath();
                ctx.moveTo(5, 12);
                ctx.lineTo(19, 12);
                ctx.stroke();
                break;
            case "undo":
                ctx.beginPath();
                ctx.moveTo(9, 14);
                ctx.lineTo(4, 9);
                ctx.lineTo(9, 4);
                ctx.moveTo(4, 9);
                ctx.lineTo(14, 9);
                ctx.arc(14, 14, 5, -Math.PI / 2, Math.PI / 2, false);
                ctx.lineTo(10, 19);
                ctx.stroke();
                break;
            case "delete":
                ctx.beginPath();
                ctx.moveTo(5, 7);
                ctx.lineTo(19, 7);
                ctx.moveTo(9, 7);
                ctx.lineTo(10, 4.5);
                ctx.lineTo(14, 4.5);
                ctx.lineTo(15, 7);
                ctx.moveTo(7, 7);
                ctx.lineTo(8, 19.5);
                ctx.lineTo(16, 19.5);
                ctx.lineTo(17, 7);
                ctx.moveTo(10.5, 10.5);
                ctx.lineTo(10.5, 16);
                ctx.moveTo(13.5, 10.5);
                ctx.lineTo(13.5, 16);
                ctx.stroke();
                break;
            case "search":
                ctx.beginPath();
                ctx.arc(10.5, 10.5, 6, 0, Math.PI * 2);
                ctx.moveTo(15, 15);
                ctx.lineTo(20, 20);
                ctx.stroke();
                break;
            case "drag_indicator":
                for (let ry of [6, 12, 18]) {
                    for (let rx of [9, 15]) {
                        ctx.beginPath();
                        ctx.arc(rx, ry, 1.5, 0, Math.PI * 2);
                        ctx.fill();
                    }
                }
                break;
            case "arrow_down":
                ctx.beginPath();
                ctx.moveTo(6.5, 9.5);
                ctx.lineTo(12, 15);
                ctx.lineTo(17.5, 9.5);
                ctx.stroke();
                break;
            case "arrow_up":
                ctx.beginPath();
                ctx.moveTo(6.5, 14.5);
                ctx.lineTo(12, 9);
                ctx.lineTo(17.5, 14.5);
                ctx.stroke();
                break;
            case "chevron_right":
                ctx.beginPath();
                ctx.moveTo(9.5, 6.5);
                ctx.lineTo(15, 12);
                ctx.lineTo(9.5, 17.5);
                ctx.stroke();
                break;
            case "chevron_left":
                ctx.beginPath();
                ctx.moveTo(14.5, 6.5);
                ctx.lineTo(9, 12);
                ctx.lineTo(14.5, 17.5);
                ctx.stroke();
                break;
            case "keyboard_return":
                ctx.beginPath();
                ctx.moveTo(19, 7);
                ctx.lineTo(19, 13);
                ctx.lineTo(6, 13);
                ctx.moveTo(10, 9);
                ctx.lineTo(6, 13);
                ctx.lineTo(10, 17);
                ctx.stroke();
                break;
            case "warning":
                ctx.beginPath();
                ctx.moveTo(12, 3.5);
                ctx.lineTo(21, 19.5);
                ctx.lineTo(3, 19.5);
                ctx.closePath();
                ctx.stroke();
                ctx.beginPath();
                ctx.moveTo(12, 9.5);
                ctx.lineTo(12, 14);
                ctx.stroke();
                ctx.beginPath();
                ctx.arc(12, 16.8, 1.0, 0, Math.PI * 2);
                ctx.fill();
                break;
            case "info":
                ctx.beginPath();
                ctx.arc(12, 12, 8.5, 0, Math.PI * 2);
                ctx.stroke();
                ctx.beginPath();
                ctx.moveTo(12, 11);
                ctx.lineTo(12, 16.5);
                ctx.stroke();
                ctx.beginPath();
                ctx.arc(12, 8, 1.1, 0, Math.PI * 2);
                ctx.fill();
                break;
            case "error":
                ctx.beginPath();
                ctx.arc(12, 12, 8.5, 0, Math.PI * 2);
                ctx.stroke();
                ctx.beginPath();
                ctx.moveTo(12, 7.5);
                ctx.lineTo(12, 13);
                ctx.stroke();
                ctx.beginPath();
                ctx.arc(12, 16.2, 1.1, 0, Math.PI * 2);
                ctx.fill();
                break;
            case "help":
                ctx.beginPath();
                ctx.arc(12, 12, 8.5, 0, Math.PI * 2);
                ctx.stroke();
                ctx.beginPath();
                ctx.arc(12, 9.5, 2.3, Math.PI, Math.PI * 0.25, false);
                ctx.lineTo(12, 13.5);
                ctx.stroke();
                ctx.beginPath();
                ctx.arc(12, 16.5, 1.0, 0, Math.PI * 2);
                ctx.fill();
                break;
            case "settings":
                ctx.beginPath();
                ctx.arc(12, 12, 3.2, 0, Math.PI * 2);
                ctx.stroke();
                ctx.beginPath();
                ctx.arc(12, 12, 6.8, 0, Math.PI * 2);
                ctx.stroke();
                for (let i = 0; i < 8; i++) {
                    const ang = i * Math.PI / 4;
                    ctx.beginPath();
                    ctx.moveTo(12 + Math.cos(ang) * 6.8, 12 + Math.sin(ang) * 6.8);
                    ctx.lineTo(12 + Math.cos(ang) * 9.2, 12 + Math.sin(ang) * 9.2);
                    ctx.stroke();
                }
                break;
            case "tune":
                ctx.beginPath();
                ctx.moveTo(4, 7);
                ctx.lineTo(13, 7);
                ctx.moveTo(17, 7);
                ctx.lineTo(20, 7);
                ctx.moveTo(4, 12);
                ctx.lineTo(7, 12);
                ctx.moveTo(11, 12);
                ctx.lineTo(20, 12);
                ctx.moveTo(4, 17);
                ctx.lineTo(14, 17);
                ctx.moveTo(18, 17);
                ctx.lineTo(20, 17);
                ctx.stroke();
                for (let pt of [[15, 7], [9, 12], [16, 17]]) {
                    ctx.beginPath();
                    ctx.arc(pt[0], pt[1], 1.8, 0, Math.PI * 2);
                    ctx.fill();
                }
                break;
            case "keyboard":
                ctx.beginPath();
                ctx.roundedRect(3, 6, 18, 12, 2.5, 2.5);
                ctx.stroke();
                for (let kx of [6.5, 10, 13.5, 17]) {
                    ctx.beginPath();
                    ctx.arc(kx, 10, 0.9, 0, Math.PI * 2);
                    ctx.fill();
                }
                ctx.beginPath();
                ctx.moveTo(7.5, 14.5);
                ctx.lineTo(16.5, 14.5);
                ctx.stroke();
                break;
            case "apps":
                for (let gx of [5, 13]) {
                    for (let gy of [5, 13]) {
                        ctx.beginPath();
                        ctx.roundedRect(gx, gy, 6, 6, 1.5, 1.5);
                        if (root.filled)
                            ctx.fill();
                        else
                            ctx.stroke();
                    }
                }
                break;
            case "macros":
                ctx.beginPath();
                ctx.moveTo(4, 7);
                ctx.lineTo(20, 7);
                ctx.moveTo(4, 12);
                ctx.lineTo(15, 12);
                ctx.moveTo(4, 17);
                ctx.lineTo(12, 17);
                ctx.moveTo(16, 17);
                ctx.lineTo(20, 17);
                ctx.moveTo(18, 15);
                ctx.lineTo(18, 19);
                ctx.stroke();
                break;
            case "dictionary":
                ctx.beginPath();
                ctx.roundedRect(5, 4, 14, 16, 2, 2);
                ctx.moveTo(8.5, 4);
                ctx.lineTo(8.5, 20);
                ctx.moveTo(11.5, 9);
                ctx.lineTo(16, 9);
                ctx.moveTo(11.5, 13);
                ctx.lineTo(16, 13);
                ctx.stroke();
                break;
            case "keymap":
                ctx.beginPath();
                ctx.moveTo(5, 9);
                ctx.lineTo(19, 9);
                ctx.lineTo(15.5, 5.5);
                ctx.moveTo(19, 15);
                ctx.lineTo(5, 15);
                ctx.lineTo(8.5, 18.5);
                ctx.stroke();
                break;
            case "save":
                ctx.beginPath();
                ctx.roundedRect(4.5, 4.5, 15, 15, 2, 2);
                ctx.moveTo(8, 4.5);
                ctx.lineTo(8, 9.5);
                ctx.lineTo(15, 9.5);
                ctx.lineTo(15, 4.5);
                ctx.moveTo(8, 19.5);
                ctx.lineTo(8, 14);
                ctx.lineTo(16, 14);
                ctx.lineTo(16, 19.5);
                ctx.stroke();
                break;
            case "group":
                ctx.beginPath();
                ctx.arc(9, 8.5, 2.75, 0, Math.PI * 2);
                ctx.stroke();
                ctx.beginPath();
                ctx.arc(16, 9.5, 2.1, 0, Math.PI * 2);
                ctx.stroke();
                ctx.beginPath();
                ctx.arc(9, 19, 5, Math.PI, 0, false);
                ctx.stroke();
                ctx.beginPath();
                ctx.arc(16, 19, 3.8, Math.PI * 1.25, 0, false);
                ctx.stroke();
                break;
            case "open":
                ctx.beginPath();
                ctx.moveTo(3.5, 7);
                ctx.lineTo(3.5, 18.5);
                ctx.lineTo(20.5, 18.5);
                ctx.lineTo(20.5, 9.5);
                ctx.lineTo(11.5, 9.5);
                ctx.lineTo(9.5, 7);
                ctx.closePath();
                ctx.stroke();
                break;
            case "refresh":
                ctx.beginPath();
                ctx.arc(12, 12, 6.5, -Math.PI * 0.85, Math.PI * 0.75, false);
                ctx.stroke();
                ctx.beginPath();
                ctx.moveTo(18.5, 6);
                ctx.lineTo(18.5, 10.5);
                ctx.lineTo(14, 10.5);
                ctx.stroke();
                break;
            case "visibility":
                ctx.beginPath();
                ctx.moveTo(3, 12);
                ctx.quadraticCurveTo(12, 5, 21, 12);
                ctx.quadraticCurveTo(12, 19, 3, 12);
                ctx.stroke();
                ctx.beginPath();
                ctx.arc(12, 12, 2.5, 0, Math.PI * 2);
                ctx.stroke();
                break;
            case "visibility_off":
                ctx.beginPath();
                ctx.moveTo(4, 4);
                ctx.lineTo(20, 20);
                ctx.moveTo(4, 12);
                ctx.quadraticCurveTo(12, 6.5, 20, 12);
                ctx.moveTo(4, 12);
                ctx.quadraticCurveTo(12, 17.5, 20, 12);
                ctx.stroke();
                break;
            case "open_in_new":
                ctx.beginPath();
                ctx.moveTo(14, 5);
                ctx.lineTo(19, 5);
                ctx.lineTo(19, 10);
                ctx.moveTo(19, 5);
                ctx.lineTo(11, 13);
                ctx.moveTo(11, 6);
                ctx.lineTo(6, 6);
                ctx.lineTo(6, 18);
                ctx.lineTo(18, 18);
                ctx.lineTo(18, 13);
                ctx.stroke();
                break;
            case "copy":
                ctx.beginPath();
                ctx.roundedRect(8, 8, 11, 11, 2, 2);
                ctx.moveTo(6, 15);
                ctx.lineTo(5, 15);
                ctx.lineTo(5, 5);
                ctx.lineTo(15, 5);
                ctx.lineTo(15, 6);
                ctx.stroke();
                break;
            case "edit":
                ctx.beginPath();
                ctx.moveTo(5, 19);
                ctx.lineTo(6.5, 14.5);
                ctx.lineTo(16.5, 4.5);
                ctx.lineTo(19.5, 7.5);
                ctx.lineTo(9.5, 17.5);
                ctx.closePath();
                ctx.stroke();
                break;
            case "minimize":
                ctx.beginPath();
                ctx.moveTo(6, 16);
                ctx.lineTo(18, 16);
                ctx.stroke();
                break;
            case "fullscreen":
                ctx.beginPath();
                ctx.moveTo(5, 9);
                ctx.lineTo(5, 5);
                ctx.lineTo(9, 5);
                ctx.moveTo(15, 5);
                ctx.lineTo(19, 5);
                ctx.lineTo(19, 9);
                ctx.moveTo(19, 15);
                ctx.lineTo(19, 19);
                ctx.lineTo(15, 19);
                ctx.moveTo(9, 19);
                ctx.lineTo(5, 19);
                ctx.lineTo(5, 15);
                ctx.stroke();
                break;
            case "fullscreen_exit":
                ctx.beginPath();
                ctx.moveTo(9, 5);
                ctx.lineTo(9, 9);
                ctx.lineTo(5, 9);
                ctx.moveTo(15, 5);
                ctx.lineTo(15, 9);
                ctx.lineTo(19, 9);
                ctx.moveTo(15, 19);
                ctx.lineTo(15, 15);
                ctx.lineTo(19, 15);
                ctx.moveTo(9, 19);
                ctx.lineTo(9, 15);
                ctx.lineTo(5, 15);
                ctx.stroke();
                break;
            }
            ctx.restore();
        }
    }

    Image {
        id: themeImage
        anchors.fill: parent
        visible: !root.isBuiltinGlyph && root.name !== ""
        source: !root.isBuiltinGlyph && root.name !== "" ? Paths.themeIconUrl(root.name) : ""
        sourceSize: Qt.size(Math.max(16, Math.round(root.size)), Math.max(16, Math.round(root.size)))
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}
