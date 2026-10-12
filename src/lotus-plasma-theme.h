/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-plasma-theme.h
 * @brief Detects whether the KDE Plasma panel is dark.
 *
 * KDE keeps two independent colour settings: the application colour scheme
 * (kdeglobals) and the Plasma Style that paints the panel (plasmarc).  The
 * settings portal only reports the former, so the default Fedora/Kubuntu
 * look — light applications on a dark panel — picked a black tray icon on a
 * black panel (issue #374).  The tray lives on the panel, so the panel's
 * colours are the ones that matter.
 *
 * Only plain config/data files are read; no subprocess is spawned.
 */

#ifndef _FCITX5_LOTUS_PLASMA_THEME_H_
#define _FCITX5_LOTUS_PLASMA_THEME_H_

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace fcitx {

    struct PlasmaThemeSearchPaths {
        // Config directories searched in KConfig cascade order: the user
        // directory first, then each system directory.
        std::string              configHome;
        std::vector<std::string> configDirs;
        // Data directories holding plasma/desktoptheme and color-schemes.
        std::string              dataHome;
        std::vector<std::string> dataDirs;
    };

    /**
     * @brief Builds search paths from the XDG environment variables.
     *
     * Adds "<configHome>/kdedefaults" (where Plasma 6 stores the Global
     * Theme defaults) when the environment does not list it already.
     */
    PlasmaThemeSearchPaths plasmaThemeSearchPathsFromEnv();

    /**
     * @brief Tells whether XDG_CURRENT_DESKTOP names KDE.
     */
    bool isKdePlasmaSession(const std::string& xdgCurrentDesktop);

    /**
     * @brief Reports whether the Plasma panel background is dark.
     *
     * Uses the Plasma Style's own colours when it ships a colors file, and
     * the system colour scheme otherwise (Breeze "default" follows it).
     *
     * @return true for a dark panel, false for a light one, std::nullopt
     *         when no colour could be found.
     */
    std::optional<bool> isPlasmaPanelDark(const PlasmaThemeSearchPaths& paths);

    /**
     * @brief Removes leading and trailing blanks from a configuration value.
     * @param s Text to trim.
     * @return Trimmed text.
     */
    std::string trim(const std::string& s);

    /**
     * @brief Joins a directory and a name with a single '/' separator.
     * @param dir Directory, empty to keep the name relative.
     * @param name File or directory name.
     * @return Joined path.
     */
    std::string joinPath(const std::string& dir, const std::string& name);

    /**
     * @brief Splits a ':' separated path list.
     * @param list Path list.
     * @return Non-empty entries of the list.
     */
    std::vector<std::string> splitPathList(const std::string& list);

    /**
     * @brief Reads an environment variable, falling back when it is empty.
     * @param name Environment variable name.
     * @param fallback Value used when the variable is unset or empty.
     * @return Environment value, or the fallback.
     */
    std::string envOr(const std::string& name, const std::string& fallback);

    /**
     * @brief Reads "key=value" from "[group]" of a KConfig-style INI file.
     * @param path File to read.
     * @param group Group holding the key.
     * @param key Key to look up.
     * @return The value, or std::nullopt when the file or the key is missing.
     */
    std::optional<std::string> readIniValue(const std::string& path, const std::string& group, const std::string& key);

    /**
     * @brief Reads a KConfig value from the user file, then from the system files.
     * @param paths Search paths.
     * @param fileName File name inside each config directory.
     * @param group Group holding the key.
     * @param key Key to look up.
     * @return The value, or std::nullopt when no file provides it.
     */
    std::optional<std::string> readConfigValue(const PlasmaThemeSearchPaths& paths, const std::string& fileName, const std::string& group, const std::string& key);

    /**
     * @brief Lists the data directories holding plasma themes and colour schemes.
     * @param paths Search paths.
     * @return The data home first, then the data directories.
     */
    std::vector<std::string> dataSearchDirs(const PlasmaThemeSearchPaths& paths);

    /**
     * @brief Parses an "r,g,b" colour value.
     * @param value Colour value; an optional fourth alpha component is ignored.
     * @return The three colour components, or std::nullopt when malformed.
     */
    std::optional<std::array<int, 3>> parseRgb(const std::string& value);

    /**
     * @brief Reports whether a colour value is a dark background.
     *
     * Uses the same rule as xdg-desktop-portal-kde: qGray(background) < 192.
     *
     * @param value Colour value, empty when nothing could be read.
     * @return true for dark, false for light, std::nullopt when unreadable.
     */
    std::optional<bool> isDarkBackground(const std::optional<std::string>& value);

    /**
     * @brief Reports whether the system colour scheme is dark.
     * @param paths Search paths.
     * @return true for dark, false for light, std::nullopt when no scheme is found.
     */
    std::optional<bool> isDarkSystemScheme(const PlasmaThemeSearchPaths& paths);

} // namespace fcitx

#endif // _FCITX5_LOTUS_PLASMA_THEME_H_
