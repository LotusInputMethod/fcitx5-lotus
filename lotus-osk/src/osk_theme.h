#ifndef OSK_THEME_H
#define OSK_THEME_H

#include <QString>
#include <QColor>

namespace Lotus {

    /**
 * @brief Structure defining visual properties of the On-Screen Keyboard.
 */
    struct OSKTheme {
        QString bgActive;
        QString bgNormal;
        QString bgSpecial;
        QString fgNormal;
        QString fgSpecial;
        QString border;
        QString hover;
        QString pressed;
        QString windowBg;

        // Dimensions (base values before DPR scaling)
        int keyWidth     = 40;
        int keyHeight    = 40;
        int spacing      = 5;
        int margin       = 10;
        int fontSize     = 18;
        int borderRadius = 6;

        // Derived or specific values
        int windowOpacity = 220; // 0-255
    };

    // Dark Theme (Default)
    const OSKTheme DarkTheme = {"#005a9e", // bgActive
                                "#333333", // bgNormal
                                "#252525", // bgSpecial
                                "#ffffff", // fgNormal
                                "#999999", // fgSpecial
                                "#444444", // border
                                "#444444", // hover
                                "#555555", // pressed
                                "#191919", // windowBg
                                40,        40, 5, 10, 18, 6, 220};

    // Light Theme
    const OSKTheme LightTheme = {"#0078d4", // bgActive
                                 "#ffffff", // bgNormal
                                 "#e5e5e5", // bgSpecial
                                 "#000000", // fgNormal
                                 "#666666", // fgSpecial
                                 "#cccccc", // border
                                 "#e1e1e1", // hover
                                 "#d1d1d1", // pressed
                                 "#f0f0f0", // windowBg
                                 40,        40, 5, 10, 18, 6, 255};

} // namespace Lotus

#endif // OSK_THEME_H
