/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-plasma-theme.h"
#include "lotus-utils.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace fcitx {

    std::string trim(const std::string& s) {
        const auto begin = s.find_first_not_of(" \t\r");
        if (begin == std::string::npos)
            return "";
        const auto end = s.find_last_not_of(" \t\r");
        return s.substr(begin, end - begin + 1);
    }

    std::string joinPath(const std::string& dir, const std::string& name) {
        if (dir.empty())
            return name;
        if (dir.back() == '/')
            return dir + name;
        return dir + "/" + name;
    }

    std::vector<std::string> splitPathList(const std::string& list) {
        std::vector<std::string> result;
        std::stringstream        ss(list);
        std::string              item;
        while (std::getline(ss, item, ':')) {
            if (!item.empty())
                result.push_back(item);
        }
        return result;
    }

    std::string envOr(const std::string& name, const std::string& fallback) {
        std::string value = getEnv(name);
        return !value.empty() ? value : fallback;
    }

    std::optional<std::string> readIniValue(const std::string& path, const std::string& group, const std::string& key) {
        std::ifstream file(path);
        if (!file.is_open())
            return std::nullopt;

        const std::string header  = "[" + group + "]";
        bool              inGroup = false;
        std::string       line;
        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            if (line[0] == '[') {
                // "[Theme][$i]" marks an immutable group; same group.
                inGroup = line == header || line == header + "[$i]";
                continue;
            }
            if (!inGroup)
                continue;
            const auto eq = line.find('=');
            if (eq != std::string::npos && trim(line.substr(0, eq)) == key)
                return trim(line.substr(eq + 1));
        }
        return std::nullopt;
    }

    std::optional<std::string> readConfigValue(const PlasmaThemeSearchPaths& paths, const std::string& fileName, const std::string& group, const std::string& key) {
        if (auto value = readIniValue(joinPath(paths.configHome, fileName), group, key))
            return value;
        for (const auto& dir : paths.configDirs) {
            if (auto value = readIniValue(joinPath(dir, fileName), group, key))
                return value;
        }
        return std::nullopt;
    }

    std::vector<std::string> dataSearchDirs(const PlasmaThemeSearchPaths& paths) {
        std::vector<std::string> dirs;
        if (!paths.dataHome.empty())
            dirs.push_back(paths.dataHome);
        dirs.insert(dirs.end(), paths.dataDirs.begin(), paths.dataDirs.end());
        return dirs;
    }

    std::optional<std::array<int, 3>> parseRgb(const std::string& value) {
        std::array<int, 3> rgb{};
        std::stringstream  ss(value);
        std::string        part;
        size_t             count = 0;
        while (std::getline(ss, part, ',')) {
            part = trim(part);
            if (part.empty() || part.size() > 3 || !std::ranges::all_of(part, [](char c) { return c >= '0' && c <= '9'; }))
                return std::nullopt;
            const int component = std::stoi(part);
            if (component > 255)
                return std::nullopt;
            if (count < rgb.size())
                rgb[count] = component; // NOLINT
            ++count;
        }
        if (count != 3 && count != 4)
            return std::nullopt;
        return rgb;
    }

    std::optional<bool> isDarkBackground(const std::optional<std::string>& value) {
        if (!value)
            return std::nullopt;
        const auto rgb = parseRgb(*value);
        if (!rgb)
            return std::nullopt;

        const int gray = (((*rgb)[0] * 11) + ((*rgb)[1] * 16) + ((*rgb)[2] * 5)) / 32;
        return gray < 192;
    }

    std::optional<bool> isDarkSystemScheme(const PlasmaThemeSearchPaths& paths) {
        if (auto dark = isDarkBackground(readConfigValue(paths, "kdeglobals", "Colors:Window", "BackgroundNormal")))
            return dark;

        // kdeglobals may only name the scheme; its colours live in a data file.
        const auto scheme = readConfigValue(paths, "kdeglobals", "General", "ColorScheme");
        if (!scheme || scheme->empty())
            return std::nullopt;
        for (const auto& dir : dataSearchDirs(paths)) {
            const std::string file = joinPath(dir, "color-schemes/" + *scheme + ".colors");
            if (access(file.c_str(), R_OK) == 0)
                return isDarkBackground(readIniValue(file, "Colors:Window", "BackgroundNormal"));
        }
        return std::nullopt;
    }

    PlasmaThemeSearchPaths plasmaThemeSearchPathsFromEnv() {
        const std::string      home = envOr("HOME", "");

        PlasmaThemeSearchPaths paths;
        paths.configHome = envOr("XDG_CONFIG_HOME", joinPath(home, ".config"));
        paths.configDirs = splitPathList(envOr("XDG_CONFIG_DIRS", "/etc/xdg"));
        paths.dataHome   = envOr("XDG_DATA_HOME", joinPath(home, ".local/share"));
        paths.dataDirs   = splitPathList(envOr("XDG_DATA_DIRS", "/usr/local/share:/usr/share"));

        // Plasma's session startup puts kdedefaults first in XDG_CONFIG_DIRS;
        // add it when fcitx5 was started with a different environment.
        const std::string kdeDefaults = joinPath(paths.configHome, "kdedefaults");
        if (std::ranges::find(paths.configDirs, kdeDefaults) == paths.configDirs.end())
            paths.configDirs.insert(paths.configDirs.begin(), kdeDefaults);

        return paths;
    }

    bool isKdePlasmaSession(const std::string& xdgCurrentDesktop) {
        const auto desktops = splitPathList(xdgCurrentDesktop);
        return std::ranges::find(desktops, "KDE") != desktops.end();
    }

    std::optional<bool> isPlasmaPanelDark(const PlasmaThemeSearchPaths& paths) {
        auto theme = readConfigValue(paths, "plasmarc", "Theme", "name");
        if (!theme || theme->empty())
            theme = "default";

        // Plasma uses the first installed copy of the theme.  A theme with its
        // own colors file paints the panel with it; one without (Breeze
        // "default") follows the system colour scheme.
        for (const auto& dir : dataSearchDirs(paths)) {
            const std::string themeDir = joinPath(dir, "plasma/desktoptheme/" + *theme);
            struct stat       st{};
            if (stat(themeDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
                continue;
            const std::string colors = joinPath(themeDir, "colors");
            if (access(colors.c_str(), R_OK) == 0)
                return isDarkBackground(readIniValue(colors, "Colors:Window", "BackgroundNormal"));
            break;
        }
        return isDarkSystemScheme(paths);
    }

} // namespace fcitx
