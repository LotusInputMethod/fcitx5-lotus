/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-engine.h"
#include "common-fd-guard.h"
#include "fcitx-utils/keysym.h"
#include "lotus-config.h"
#include "lotus-state.h"
#include "lotus-candidates.h"
#include "lotus-key-pacer.h"
#include "lotus-monitor.h"
#include "lotus-utils.h"
#include "lotus-plasma-theme.h"

#include "dictionary.h"
#include "utf8.h"

#include <optional>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

#include <fcitx-config/iniparser.h>
#include <fcitx/menu.h>
#include <fcitx/userinterfacemanager.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/utf8.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_set>

#include <fcntl.h>
#include <sstream>

namespace fcitx {
    constexpr const char* CharsetActionPrefix = "lotus-charset-";
    constexpr const char* CustomKeymapFile    = "conf/lotus-custom-keymap.conf";
    constexpr const char* MacroTableFile      = "conf/lotus-macro-table.conf";

    int                   modeToInt(LotusMode mode) {
        switch (mode) {
            case LotusMode::Off: return 0;
            case LotusMode::Smooth: return 1;
            case LotusMode::SuperSmooth: return 3;
            case LotusMode::SurroundingText: return 4;
            case LotusMode::Preedit: return 5;
            case LotusMode::Emoji: return 6;
            case LotusMode::Select: return 7;
            case LotusMode::Minecraft: return 8;
            default: return 0;
        }
    }

    LotusMode intToMode(int mode) {
        switch (mode) {
            case 0: return LotusMode::Off;
            case 1:
            case 2: return LotusMode::Smooth;
            case 3: return LotusMode::SuperSmooth;
            case 4: return LotusMode::SurroundingText;
            case 5: return LotusMode::Preedit;
            case 6: return LotusMode::Emoji;
            case 7: return LotusMode::Select;
            case 8: return LotusMode::Minecraft;
            default: return LotusMode::Off;
        }
    }

    KeySym shortcutKey(const std::string& shortcut) {
        return Key(shortcut).sym();
    }

    bool modeVisible(const lotusConfig& config, LotusMode mode) {
        switch (mode) {
            case LotusMode::Smooth: return *config.showModeSmooth;
            case LotusMode::SuperSmooth: return *config.showModeSuperSmooth;
            case LotusMode::Minecraft: return *config.showModeMinecraft;
            case LotusMode::Select: return *config.showModeSelect;
            case LotusMode::SurroundingText: return *config.showModeSurroundingText;
            case LotusMode::Preedit: return *config.showModePreedit;
            case LotusMode::Emoji: return *config.showModeEmoji;
            case LotusMode::Off: return *config.showModeOff;
        }
        return false;
    }

    KeySym modeShortcut(const lotusConfig& config, LotusMode mode) {
        switch (mode) {
            case LotusMode::Smooth: return shortcutKey(*config.shortcutSmooth);
            case LotusMode::SuperSmooth: return shortcutKey(*config.shortcutSuperSmooth);
            case LotusMode::Minecraft: return shortcutKey(*config.shortcutMinecraft);
            case LotusMode::Select: return shortcutKey(*config.shortcutSelect);
            case LotusMode::SurroundingText: return shortcutKey(*config.shortcutSurroundingText);
            case LotusMode::Preedit: return shortcutKey(*config.shortcutPreedit);
            case LotusMode::Emoji: return shortcutKey(*config.shortcutEmoji);
            case LotusMode::Off: return shortcutKey(*config.shortcutOff);
        }
        return FcitxKey_None;
    }

    std::string modeLabel(const ModeOrderEntry& entry) {
        return entry.isDefault ? _("Default Typing") : LotusModeI18NAnnotation::toString(entry.mode);
    }

    std::vector<ModeOrderEntry> modeOrderEntries(const lotusConfig& config) {
        const auto order = stringutils::split(*config.modeOrder, ",");

        auto       resolve = [&config](const ModeNameEntry& nameEntry) {
            if (nameEntry.mode.has_value()) {
                return ModeOrderEntry{.mode = *nameEntry.mode, .key = modeShortcut(config, *nameEntry.mode), .visible = modeVisible(config, *nameEntry.mode), .isDefault = false};
            }
            return ModeOrderEntry{.mode = config.mode.value(), .key = shortcutKey(*config.shortcutDefault), .visible = *config.showModeDefault, .isDefault = true};
        };

        std::vector<ModeOrderEntry> entries;
        for (const auto& name : order) {
            const auto* const it = std::ranges::find_if(kModeNames, [&name](const ModeNameEntry& entry) { return entry.name == name; });
            if (it != kModeNames.end()) {
                entries.push_back(resolve(*it));
            }
        }
        for (const auto& nameEntry : kModeNames) {
            const bool listed = std::ranges::any_of(order, [&nameEntry](const std::string& name) { return nameEntry.name == name; });
            if (!listed) {
                entries.push_back(resolve(nameEntry));
            }
        }
        return entries;
    }

    bool isAppModeMenuReservedKey(KeySym sym, const lotusConfig& config) {
        std::array<KeySym, 10> modeShortcuts{};
        std::ranges::transform(kModeNames, modeShortcuts.begin(), [&config](const ModeNameEntry& nameEntry) {
            if (nameEntry.mode.has_value()) {
                return modeShortcut(config, *nameEntry.mode);
            }
            return shortcutKey(*config.shortcutDefault);
        });
        if (std::ranges::find(modeShortcuts, sym) != modeShortcuts.end()) {
            return true;
        }

        static const std::array<KeySym, 7> reservedMenuKeys = {
            FcitxKey_Escape, FcitxKey_Tab, FcitxKey_ISO_Left_Tab, FcitxKey_Return, FcitxKey_space, FcitxKey_Up, FcitxKey_Down,
        };
        return std::ranges::find(reservedMenuKeys, sym) != reservedMenuKeys.end();
    }

    KeySym typeKeyForModeMenuHotkey(KeySym hotkeySym, const lotusConfig& config) {
        return isAppModeMenuReservedKey(hotkeySym, config) ? FcitxKey_f : hotkeySym;
    }

    bool LotusEngine::isDarkMode() {
        // Each probe spawns subprocesses, and subModeIconImpl calls this on
        // every tray update while IconTheme is Auto.  Cache the result briefly
        // so the cost is paid at most once per few seconds.
        static int64_t lastCheckMs = 0;
        static bool    cachedValue = false;
        const int64_t  now         = now_ms();
        if (now - lastCheckMs < 5000) {
            return cachedValue;
        }
        lastCheckMs = now;
        cachedValue = false;

        // KDE Plasma: the tray sits on the panel, painted by the Plasma Style,
        // while the portal below reports the application colour scheme.  The
        // two differ in the default Fedora/Kubuntu look (#374).
        if (isKdePlasmaSession(getEnv("XDG_CURRENT_DESKTOP"))) {
            if (const auto dark = isPlasmaPanelDark(plasmaThemeSearchPathsFromEnv())) {
                cachedValue = *dark;
                return cachedValue;
            }
        }

        // GTK_THEME is honored by lightweight DEs that lack the settings
        // portal; covers XFCE, openbox, etc. with a dark theme.
        if (std::string theme = getEnv("GTK_THEME"); !theme.empty()) {
            std::ranges::transform(theme, theme.begin(), ::tolower);
            if (theme.find("dark") != std::string::npos) {
                cachedValue = true;
                return cachedValue;
            }
        }

        const auto openPipe = [](const char* command) { return std::unique_ptr<FILE, int (*)(FILE*)>(popen(command, "r"), pclose); };

        auto pipe = openPipe("dbus-send --session --dest=org.freedesktop.portal.Desktop --print-reply /org/freedesktop/portal/desktop org.freedesktop.portal.Settings.ReadOne "
                             "string:'org.freedesktop.appearance' string:'color-scheme' 2>/dev/null");
        if (pipe) {
            std::array<char, 256> buffer{};
            while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
                uint32_t value = 0;
                // The sscanf return value is checked and the value is only compared against 1.
                if (sscanf(buffer.data(), "%*[^v]variant uint32 %u", &value) == 1) { //NOLINT(bugprone-unchecked-string-to-number-conversion)
                    cachedValue = value == 1;
                    return cachedValue;
                }
            }
        }

        pipe = openPipe("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null");
        if (pipe) {
            std::array<char, 256> buffer{};
            if (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
                cachedValue = strstr(buffer.data(), "prefer-dark") != nullptr;
                return cachedValue;
            }
        }

        // GTK settings file — covers DEs where the dark preference is stored
        // there instead of being exposed via portal/gsettings.
        if (std::string home = getEnv("HOME"); !home.empty()) {
            std::ifstream settingsFile(home + "/.config/gtk-3.0/settings.ini");
            if (settingsFile.is_open()) {
                std::string line;
                while (std::getline(settingsFile, line)) {
                    if (line.find("gtk-application-prefer-dark-theme=1") != std::string::npos) {
                        cachedValue = true;
                        return cachedValue;
                    }
                }
            }
            settingsFile.close();
        }

        return cachedValue;
    }

    std::shared_ptr<const lotus::MacroTable> buildMacroTable(const lotusMacroTable& macroTable) {
        auto        table  = std::make_shared<lotus::MacroTable>();
        const auto& macros = *macroTable.macros;
        for (const auto& keymap : macros) {
            table->set(*keymap.key, *keymap.value);
        }
        return table;
    }

    const std::shared_ptr<const lotus::MacroTable>& LotusEngine::macroTable() const {
        return macroTable_;
    }

    const std::shared_ptr<const lotus::Dictionary>& LotusEngine::dictionary() const {
        return dictionary_;
    }

    LotusEngine::LotusEngine(Instance* instance) : instance_(*instance), factory_([this](InputContext& ic) { return new LotusState(this, &ic); }) { //NOLINT
        KeyPacer::instance().init(instance);
        std::string desktop = getEnv("XDG_CURRENT_DESKTOP");
        isGnome_            = (!desktop.empty()) && desktop.find("GNOME") != std::string::npos;
        // emptyCustomKeymap_.customKeymap is implicitly initialized to empty by fcitx::Option default value macro.

        auto& uiManager = instance_.userInterfaceManager();

        charsetAction_ = std::make_unique<SimpleAction>();
        charsetAction_->setShortText(_("Charset"));
        charsetAction_->setIcon("character-set");
        uiManager.registerAction("lotus-charset", charsetAction_.get());
        charsetMenu_ = std::make_unique<Menu>();
        charsetAction_->setMenu(charsetMenu_.get());

        for (std::size_t i = 0; i < lotus::kCharsetCount; ++i) {
            const auto        charset = static_cast<lotus::Charset>(i);
            const std::string name    = CharsetToString(charset);
            charsetSubAction_.emplace_back(std::make_unique<SimpleAction>());
            auto* action = charsetSubAction_.back().get();
            action->setShortText(name);
            action->setCheckable(true);
            uiManager.registerAction(stringutils::concat(CharsetActionPrefix, name), action);
            connections_.emplace_back(action->connect<SimpleAction::Activated>([this, charset](InputContext* ic) {
                if (config_.outputCharset.value() == charset)
                    return;
                config_.outputCharset.setValue(charset);
                saveConfig();
                refreshEngine();
                updateCharsetAction(ic);
                if (ic)
                    ic->updateUserInterface(UserInterfaceComponent::StatusArea);
            }));
            charsetMenu_->addAction(action);
        }

        initToggleAction(spellCheckAction_, config_.spellCheck, "lotus-spellcheck", "tools-check-spelling", _("Spell Check"), _("Spell Check"), uiManager);
        initToggleAction(macroAction_, config_.enableMacro, "lotus-macro", "document-edit", _("Macro"), _("Macro"), uiManager);
        initToggleAction(capitalizeMacroAction_, config_.capitalizeMacro, "lotus-capitalizemacro", "format-text-uppercase", _("Capitalize Macro"), _("Capitalize Macro"),
                         uiManager);
        initToggleAction(autoNonVnRestoreAction_, config_.autoNonVnRestore, "lotus-autonvnrestore", "edit-undo", _("Auto Restore Invalid Words"), _("Auto Non-VN Restore"),
                         uiManager);
        initToggleAction(enableDictionaryAction_, config_.enableDictionary, "lotus-dictionary", "accessories-dictionary", _("Custom Dictionary"), _("Custom Dictionary"),
                         uiManager);

        settingsAction_ = std::make_unique<SimpleAction>();
        settingsAction_->setShortText(_("Settings"));
        settingsAction_->setIcon("configure");
        connections_.emplace_back(settingsAction_->connect<SimpleAction::Activated>([](InputContext*) { startProcess({FCITX5_LOTUS_SETTINGS_PATH}); }));
        uiManager.registerAction("lotus-settings", settingsAction_.get());

        std::string configDir = lotusConfigDir();

        if (!std::filesystem::exists(configDir)) {
            std::filesystem::create_directories(configDir);
        }
        reloadConfig();
        realMode = config_.mode.value();
        instance_.inputContextManager().registerProperty("LotusState", &factory_);
        appRulesPath_ = configDir + "/lotus-app-rules.conf";
        loadAppRules();
        toggleActions_ = {
            charsetAction_.get(),          spellCheckAction_.get(),       macroAction_.get(),    capitalizeMacroAction_.get(),
            autoNonVnRestoreAction_.get(), enableDictionaryAction_.get(), settingsAction_.get(),
        };
    }

    void LotusEngine::initToggleAction(std::unique_ptr<SimpleAction>& action, Option<bool>& option, const std::string& actionId, const std::string& iconName,
                                       const std::string& textLong, const std::string& textOnOff, UserInterfaceManager& uiManager) {
        action = std::make_unique<SimpleAction>();
        action->setShortText(textLong);
        action->setIcon(iconName);
        action->setCheckable(false);
        auto* const actionPtr = action.get();
        auto* const optionPtr = &option;
        connections_.emplace_back(action->connect<SimpleAction::Activated>([this, actionPtr, optionPtr, textOnOff](InputContext* ic) {
            optionPtr->setValue(!optionPtr->value());
            saveConfig();
            refreshOption();
            updateAction(ic, *actionPtr, *optionPtr, textOnOff);
        }));
        uiManager.registerAction(actionId, action.get());
    }

    void LotusEngine::updateAction(InputContext* ic, SimpleAction& action, Option<bool>& option, const std::string& textOnOff) {
        action.setShortText((option.value() ? "✔ " : "✖ ") + textOnOff);
        if (ic != nullptr) {
            action.update(ic);
        }
    }

    LotusEngine::~LotusEngine() {
        KeyPacer::instance().stop();
        stop_flag_monitor.store(true, std::memory_order_release);
        mouseEventSource_.reset();
        FdGuard fd(mouse_socket_fd.exchange(-1, std::memory_order_acq_rel));
        if (fd.is_valid()) {
            shutdown(fd.get(), SHUT_RDWR);
        }
        // kb_socket fd is owned by VirtualKeyboard inside KeyPacer now.
        LOTUS_INFO("Engine destroyed.");
    }

    const lotusCustomKeymap& LotusEngine::customKeymap() const {
        if (config_.enableCustomKeymap.value()) {
            return customKeymap_;
        }
        return emptyCustomKeymap_;
    }

    void LotusEngine::reloadConfig() {
        readAsIni(config_, "conf/lotus.conf");
        readAsIni(customKeymap_, CustomKeymapFile);
        readAsIni(macroTables_, MacroTableFile);
        macroTable_ = buildMacroTable(macroTables_);
        loadDictionary();
        loadAppRules();
        populateConfig();
    }

    void LotusEngine::loadDictionary() {
        auto dictionary = std::make_shared<lotus::Dictionary>();
        // Go read the descriptor handed to it; the port reads the same descriptor into the text
        // Dictionary::loadFromText expects.
        auto loadFromFd = [&dictionary](int rawFd) {
            FdGuard                fd(rawFd);
            std::string            contents;
            std::array<char, 4096> buffer{};
            for (ssize_t count = ::read(fd.get(), buffer.data(), buffer.size()); count > 0; count = ::read(fd.get(), buffer.data(), buffer.size())) {
                contents.append(buffer.data(), static_cast<std::size_t>(count));
            }
            dictionary->loadFromText(contents);
        };
        if (config_.enableDictionary.value()) {
#if LOTUS_USE_MODERN_FCITX_API
            auto fd = StandardPaths::global().open(StandardPathsType::PkgData, "lotus/vietnamese.cm.dict");
#else
            auto fd = StandardPath::global().open(StandardPath::Type::PkgData, "lotus/vietnamese.cm.dict", O_RDONLY);
#endif
            if (fd.isValid()) {
                loadFromFd(fd.release());
            }
        } else {
#if LOTUS_USE_MODERN_FCITX_API
            auto paths = StandardPaths::global().locateAll(StandardPathsType::PkgData, "lotus/vietnamese.cm.dict");
#else
            auto paths = StandardPath::global().locateAll(StandardPath::Type::PkgData, "lotus/vietnamese.cm.dict");
#endif
            for (const auto& p : paths) {
#if LOTUS_USE_MODERN_FCITX_API
                if (!p.string().starts_with("/home/")) {
                    auto fd = fcitx::UnixFD(::open(p.c_str(), O_RDONLY));
                    if (fd.isValid()) {
                        loadFromFd(fd.release());
#else
                if (!p.starts_with("home/")) {
                    int fd = ::open(p.c_str(), O_RDONLY);
                    if (fd != -1) {
                        loadFromFd(fd);
#endif
                        break;
                    }
                }
            }
        }
        dictionary_ = std::move(dictionary);
    }

    const Configuration* LotusEngine::getSubConfig(const std::string& path) const {
        if (path == "custom_keymap")
            return &customKeymap_;
        if (path == "lotus-macro") {
            return &macroTables_;
        }
        if (path == "app_rules") {
            return &appRulesTables_;
        }
        return nullptr;
    }

    void LotusEngine::setConfig(const RawConfig& config) {
        config_.load(config, true);
        saveConfig();
        loadDictionary();
        populateConfig();
    }

    void LotusEngine::populateConfig() {
        refreshEngine();
        refreshOption();
        // Pre-warm the EIS transport for the configured mode: pays the
        // connect + EI handshake up front (startup, GUI mode switch) so the
        // first replacement does not stall waiting for RESUMED. Idempotent;
        // a dead server fails instantly and the per-key lazy connect remains
        // the fallback.
        if (isUinputMode(config_.mode.value())) {
            KeyPacer::instance().ensureConnected();
        }
        updateCharsetAction(nullptr);
        updateAction(nullptr, *spellCheckAction_, config_.spellCheck, _("Spell Check"));
        updateAction(nullptr, *macroAction_, config_.enableMacro, _("Macro"));
        updateAction(nullptr, *capitalizeMacroAction_, config_.capitalizeMacro, _("Capitalize Macro"));
        updateAction(nullptr, *autoNonVnRestoreAction_, config_.autoNonVnRestore, _("Auto Non-VN Restore"));
        updateAction(nullptr, *enableDictionaryAction_, config_.enableDictionary, _("Custom Dictionary"));
    }

    void LotusEngine::setSubConfig(const std::string& path, const RawConfig& config) {
        if (path == "custom_keymap") {
            customKeymap_.load(config, true);
            safeSaveAsIni(customKeymap_, CustomKeymapFile);
            refreshEngine();
        } else if (path == "lotus-macro") {
            macroTables_.load(config, true);
            safeSaveAsIni(macroTables_, MacroTableFile);
            macroTable_ = buildMacroTable(macroTables_);
            refreshEngine();
        } else if (path == "app_rules") {
            appRulesTables_.load(config, true);
            {
                std::scoped_lock lock(appRulesMutex_);
                for (auto it = appRules_.begin(); it != appRules_.end();) {
                    if (!it->first.starts_with("ctx_")) {
                        it = appRules_.erase(it);
                    } else {
                        ++it;
                    }
                }
                for (const auto& rule : *appRulesTables_.rules) {
                    appRules_[*rule.app] = {
                        .mode              = intToMode(*rule.mode),
                        .commitInterval    = *rule.commitInterval,
                        .backspaceInterval = *rule.backspaceInterval,
                        .postDelay         = *rule.postDelay,
                    };
                }
            }
            saveAppRules();
            refreshEngine();
        }
    }

    std::string LotusEngine::subMode(const InputMethodEntry& /*entry*/, InputContext& /*inputContext*/) {
        return InputMethodIdToString(*config_.inputMethod);
    }

    void LotusEngine::activate(const InputMethodEntry& /*entry*/, InputContextEvent& event) {
        auto*      ic        = event.inputContext();
        const bool surrvalid = ic->surroundingText().isValid();
        const bool is_dbus   = getFrontendName(ic) == "dbus";
        setupMouseWatcher();

        auto& statusArea = event.inputContext()->statusArea();
        if (ic->capabilityFlags().test(CapabilityFlag::Preedit))
            instance_.inputContextManager().setPreeditEnabledByDefault(true);

        std::string appName = getProgramName(ic);
        LOTUS_INFO("App name: " + appName);

        const LotusMode targetMode = getAppRule(appName);
        LOTUS_INFO("Target mode: " + LotusModeI18NAnnotation::toString(targetMode));

        updateCharsetAction(event.inputContext());

        auto*      state = ic->propertyFor(&factory_);

        const bool uinputMode         = isUinputMode(targetMode);
        const bool focusBounce        = uinputMode && state->lastDeactivateTime_ > 0 && now_ms() - state->lastDeactivateTime_ < 100;
        const bool resumeReplacement  = focusBounce && state->deletionInterruptedAt_ > 0 && is_deleting_.load();
        state->deletionInterruptedAt_ = 0;

        // Pre-warm the EIS transport on focus-in so the first replacement in
        // this app does not pay connect + handshake latency. Idempotent (a
        // live connection or an in-flight handshake returns at once).
        if (uinputMode) {
            KeyPacer::instance().ensureConnected();
        }

        if (!resumeReplacement) {
            is_deleting_.store(false);
        }

        if (focusBounce) {
            realMode = targetMode;
            ic->updateUserInterface(UserInterfaceComponent::StatusArea);
            LOTUS_INFO("Focus bounce: keep word buffers");
        } else {
            setMode(targetMode, event.inputContext());
        }
        if (event.type() == EventType::InputContextFocusIn && is_dbus && !surrvalid) {
            LOTUS_INFO("Skip clearAllBuffers");
        } else if (surrvalid && !state->oldPreBuffer_.empty() && (now_ms() - state->lastDeactivateTime_) >= 100) {
            state->clearAllBuffers();
        }
        if (!resumeReplacement) {
            is_deleting_.store(false);
        }
        if (targetMode == LotusMode::Emoji) {
            state->updateEmojiPreedit();
        } else {
            ic->inputPanel().reset();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            if (realMode == LotusMode::Preedit || realMode == LotusMode::SurroundingText)
                ic->updatePreedit();
        }
        for (const auto& action : toggleActions_) {
            statusArea.addAction(StatusGroup::InputMethod, action);
        }
    }

    bool LotusEngine::handleAppModeMenu(KeyEvent& keyEvent) {
        auto* ic    = keyEvent.inputContext();
        auto* state = ic->propertyFor(&factory_);

        if (isSelectingAppMode_) {
            if (keyEvent.isRelease())
                return true;

            auto   baseList = ic->inputPanel().candidateList();
            auto   menuList = std::dynamic_pointer_cast<CommonCandidateList>(baseList);
            KeySym keySym   = keyEvent.key().sym();

            auto   moveCursor = [&](int delta) {
                if (!menuList || menuList->empty()) {
                    return false;
                }

                int totalSize = menuList->totalSize();
                if (totalSize <= 1) {
                    return false;
                }

                int cursorIndex = menuList->globalCursorIndex();
                if (cursorIndex < 0 || cursorIndex >= totalSize) {
                    cursorIndex = 0;
                }

                int nextIndex = cursorIndex + delta;
                if (nextIndex < 0) {
                    nextIndex = totalSize - 1;
                } else if (nextIndex >= totalSize) {
                    nextIndex = 0;
                }

                menuList->setGlobalCursorIndex(nextIndex);
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                return true;
            };

            keyEvent.filterAndAccept();

            std::optional<LotusMode> selectedMode  = std::nullopt;
            bool                     selectionMade = false;

            switch (keySym) {
                case FcitxKey_Tab:
                case FcitxKey_Down: {
                    if (moveCursor(1)) {
                        return true;
                    }
                    break;
                }
                case FcitxKey_ISO_Left_Tab:
                case FcitxKey_Up: {
                    if (moveCursor(-1)) {
                        return true;
                    }
                    break;
                }
                case FcitxKey_space:
                case FcitxKey_Return: {
                    if (menuList && !menuList->empty()) {
                        int selectedIndex = menuList->globalCursorIndex();
                        if (selectedIndex < 0 || selectedIndex >= menuList->totalSize()) {
                            selectedIndex = 0;
                        }
                        menuList->candidateFromAll(selectedIndex).select(ic);
                        return true;
                    }
                    break;
                }
                case FcitxKey_Escape: {
                    selectionMade = true;
                    break;
                }
                default: {
                    auto it = modeMenuMapping_.find(keySym);
                    if (it != modeMenuMapping_.end()) {
                        selectedMode = it->second;
                    }

                    if (selectedMode == std::nullopt) {
                        const auto& kl = *config_.modeMenuKey;
                        if (kl.size() == 1 && !kl[0].hasModifier()) {
                            std::string charStr = Key::keySymToUTF8(kl[0].sym());
                            if (!charStr.empty() && keySym == typeKeyForModeMenuHotkey(kl[0].sym(), config_)) {
                                isSelectingAppMode_ = false;
                                ic->inputPanel().reset();
                                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                                state->commitBuffer();
                                state->reset();
                                ic->commitString(charStr);
                                return true;
                            }
                        }
                    }
                    break;
                }
            }

            if (selectedMode != std::nullopt) {
                LOTUS_INFO("Selected mode: " + LotusModeI18NAnnotation::toString(selectedMode.value()));
                if (selectedMode != LotusMode::Emoji) {
                    if (keySym == Key(*config_.shortcutDefault).sym()) { // Default Typing key
                        clearAppRule(currentConfigureApp_);
                    } else {
                        setAppRule(currentConfigureApp_, selectedMode.value());
                        if (!currentConfigureApp_.starts_with("ctx_")) {
                            saveAppRules();
                        }
                    }
                }
                selectionMade = true;
            }

            if (selectionMade) {
                isSelectingAppMode_ = false;
                ic->inputPanel().reset();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);

                if (selectedMode != std::nullopt) {
                    state->commitBuffer();
                    state->reset();
                    setMode(selectedMode.value(), ic);
                    if (selectedMode == LotusMode::Emoji) {
                        state->updateEmojiPreedit();
                    } else {
                        showCycleModeNotification(selectedMode.value(), ic);
                    }
                }
            }
            return true;
        }
        return false;
    }

    void LotusEngine::keyEvent(const InputMethodEntry& /*entry*/, KeyEvent& keyEvent) {
        auto* ic = keyEvent.inputContext();
        if (ic == nullptr) {
            return;
        }

        auto* state = ic->propertyFor(&factory_);

        if (ic->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive)) {
            if (state != nullptr) {
                state->reset();
            }
            ic->inputPanel().reset();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            return;
        }

        if (isSelectingAppMode_ && g_mouse_clicked.load(std::memory_order_acquire)) {
            closeAppModeMenu();
            ic->inputPanel().reset();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            state->commitBuffer();
            state->reset();
        }

        if (handleAppModeMenu(keyEvent)) {
            return;
        }

        if (handleCycleModeHotkey(ic, keyEvent)) {
            return;
        }

        if (handleModeMenuHotkey(ic, keyEvent)) {
            return;
        }
        state->keyEvent(keyEvent);
        syncRealtextLen(ic);
    }

    bool LotusEngine::handleModeMenuHotkey(InputContext* ic, KeyEvent& keyEvent) {
        auto* state = ic->propertyFor(&factory_);
        if (!keyEvent.isRelease() && !config_.modeMenuKey->empty() && keyEvent.key().checkKeyList(*config_.modeMenuKey)) {
            LOTUS_INFO("Mode menu key pressed");
            if (state != nullptr) {
                state->commitBuffer();
                state->reset();
            }
            currentConfigureApp_ = getProgramName(ic);
            g_mouse_clicked.store(false, std::memory_order_release);
            std::string appName = getProgramName(ic);
            setMode(getAppRule(appName), ic);
            showAppModeMenu(ic);
            keyEvent.filterAndAccept();
            return true;
        }
        return false;
    }

    void LotusEngine::syncRealtextLen(InputContext* ic) {
        const auto&  s       = ic->surroundingText();
        const auto&  text    = s.text();
        const size_t textLen = lotus::utf8::length(text);
        unsigned int cursor  = s.cursor();
        if (textLen == static_cast<size_t>(cursor))
            realtextLen.store(static_cast<unsigned int>(textLen), std::memory_order_release);
    }

    bool LotusEngine::handleCycleModeHotkey(InputContext* ic, KeyEvent& keyEvent) {
        if (!keyEvent.isRelease() && !config_.cycleModeKey->empty() && keyEvent.key().checkKeyList(*config_.cycleModeKey)) {
            LOTUS_INFO("Cycle mode key pressed");
            std::string            appName  = getProgramName(ic);
            LotusMode              realMode = getAppRule(appName);

            std::vector<LotusMode> enabledModes = enabledModesInOrder();

            if (!enabledModes.empty()) {
                size_t currentIdx = 0;
                bool   found      = false;
                for (size_t i = 0; i < enabledModes.size(); ++i) {
                    if (enabledModes[i] == realMode) {
                        currentIdx = i;
                        found      = true;
                        break;
                    }
                }

                LotusMode nextMode = found ? enabledModes[(currentIdx + 1) % enabledModes.size()] : enabledModes[0];
                setMode(nextMode, ic);
                setAppRule(appName, nextMode);
                showCycleModeNotification(nextMode, ic);
            }

            keyEvent.filterAndAccept();
            return true;
        }
        return false;
    }

    std::vector<LotusMode> LotusEngine::enabledModesInOrder() const {
        std::vector<LotusMode> enabledModes;
        for (const auto& entry : modeOrderEntries(config_)) {
            if (!entry.visible) {
                continue;
            }

            bool duplicate = false;
            for (auto m : enabledModes) {
                if (m == entry.mode) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                enabledModes.push_back(entry.mode);
            }
        }
        return enabledModes;
    }

    void LotusEngine::reset(const InputMethodEntry& /*entry*/, InputContextEvent& event) {
        if (is_deleting_.load(std::memory_order_acquire)) {
            LOTUS_INFO("Ignore reset while is_deleting");
            return;
        }
        LOTUS_INFO("Reset engine");
        auto* state = event.inputContext()->propertyFor(&factory_);
        if (!state->isEmptyHistory() && event.type() != EventType::InputContextFocusOut) {
            return;
        }

        if (event.type() == EventType::InputContextFocusOut || event.type() == EventType::InputContextReset) {
            state->reset(event.type() == EventType::InputContextFocusOut);
        }
    }

    void LotusEngine::deactivate(const InputMethodEntry& /*entry*/, InputContextEvent& event) {
        auto*      ic         = event.inputContext();
        auto*      state      = ic->propertyFor(&factory_);
        const bool uinputMode = isUinputMode(realMode);

        if (uinputMode && is_deleting_.load(std::memory_order_acquire) && state->expected_backspaces_ > 0) {
            state->lastDeactivateTime_    = now_ms();
            state->deletionInterruptedAt_ = state->lastDeactivateTime_;
            LOTUS_INFO("Replacement interrupted by focus out");
            return;
        }

        const bool surrvalid       = ic->surroundingText().isValid();
        const bool is_dbus         = getFrontendName(ic) == "dbus";
        state->lastDeactivateTime_ = now_ms();
        if (realMode == LotusMode::Preedit && event.type() != EventType::InputContextFocusOut) {
            state->commitBuffer();
        } else {
            if (event.type() == EventType::InputContextFocusOut && is_dbus && !surrvalid) {
                state->lastDeactivateTime_ = now_ms();
                LOTUS_INFO("Skip clearAllBuffers");
            } else {
                if (surrvalid && !state->oldPreBuffer_.empty())
                    state->clearAllBuffers();
            }

            is_deleting_.store(false, std::memory_order_release);
            ic->inputPanel().reset();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            if (realMode == LotusMode::Preedit || realMode == LotusMode::Emoji || realMode == LotusMode::SurroundingText)
                ic->updatePreedit();
        }
    }

    void LotusEngine::refreshEngine() {
        if (!factory_.registered())
            return;
        instance_.inputContextManager().foreach ([this](InputContext* ic) {
            auto* state = ic->propertyFor(&factory_);
            state->setEngine();
            if (ic->hasFocus()) {
                // Re-resolve the focused window's rule; setEngine() must not
                // reset it to the global mode.
                setMode(getAppRule(getProgramName(ic)), ic);
                state->reset();
            }
            return true;
        });
    }

    void LotusEngine::refreshOption() {
        if (!factory_.registered())
            return;
        instance_.inputContextManager().foreach ([this](InputContext* ic) {
            auto* state = ic->propertyFor(&factory_);
            state->setOption();
            if (ic->hasFocus())
                state->reset();
            return true;
        });
    }

    void LotusEngine::updateCharsetAction(InputContext* ic) {
        auto name = stringutils::concat(CharsetActionPrefix, CharsetToString(*config_.outputCharset));
        for (const auto& action : charsetSubAction_) {
            action->setChecked(action->name() == name);
            if (ic != nullptr)
                action->update(ic);
        }
    }

    void LotusEngine::loadAppRules() {
        {
            std::scoped_lock                                     lock(appRulesMutex_);
            std::unordered_map<std::string, LotusAppRuleSetting> ctxRules;
            for (const auto& [app, setting] : appRules_) {
                if (app.starts_with("ctx_")) {
                    ctxRules[app] = setting;
                }
            }
            appRules_ = std::move(ctxRules);
        }
        auto loadFromFile = [this](const std::string& path) {
            if (path.empty()) {
                LOTUS_WARN("App rules path is empty, skipping load");
                return;
            }
            std::ifstream file(path);
            if (!file.is_open())
                return;

            std::unordered_map<std::string, LotusAppRuleSetting> tempRules;
            std::string                                          line;
            while (std::getline(file, line)) {
                if (line.empty() || line[0] == '#')
                    continue;
                auto delimiterPos = line.find('=');
                if (delimiterPos != std::string::npos) {
                    std::string              app   = line.substr(0, delimiterPos);
                    std::string              rest  = line.substr(delimiterPos + 1);
                    std::vector<std::string> parts = stringutils::split(rest, ",");
                    if (parts.empty()) {
                        LOTUS_WARN("Invalid mode value for app: " + app);
                        continue;
                    }
                    auto field = [&parts](size_t index) -> int { return (parts.size() > index && !parts[index].empty()) ? std::stoi(parts[index]) : 0; };
                    try {
                        int mode       = std::stoi(parts[0]);
                        int commit     = field(1);
                        int backspace  = field(2);
                        int post       = field(3);
                        tempRules[app] = {
                            .mode              = intToMode(mode),
                            .commitInterval    = commit > 0 ? commit : 0,
                            .backspaceInterval = backspace > 0 ? backspace : 0,
                            .postDelay         = post > 0 ? post : 0,
                        };
                    } catch (const std::exception&) { LOTUS_WARN("Invalid mode value for app: " + app); }
                }
            }
            file.close();

            std::scoped_lock lock(appRulesMutex_);
            for (const auto& [app, setting] : tempRules) {
                appRules_[app] = setting;
            }
        };
        loadFromFile(appRulesPath_);

        std::scoped_lock          lock(appRulesMutex_);
        std::vector<lotusAppRule> rules;
        for (const auto& pair : appRules_) {
            if (pair.first.starts_with("ctx_"))
                continue;
            lotusAppRule rule;
            rule.app.setValue(pair.first);
            rule.mode.setValue(modeToInt(pair.second.mode));
            rule.commitInterval.setValue(pair.second.commitInterval);
            rule.backspaceInterval.setValue(pair.second.backspaceInterval);
            rule.postDelay.setValue(pair.second.postDelay);
            rules.push_back(std::move(rule));
        }
        appRulesTables_.rules.setValue(std::move(rules));
    }

    void LotusEngine::saveAppRules() const {
        // Method is const but locks mutable appRulesMutex_ to safely read appRules_ state
        std::ofstream file(appRulesPath_, std::ios::trunc);
        if (!file.is_open())
            return;

        file << "# Lotus Per-App Configuration\n";
        file << "# 0 = Off, 1 = Uinput (Smooth), 2 = Uinput (Smooth, legacy), 3 = Uinput (Super Smooth), 4 = Surrounding Text, 5 = Preedit,\n";
        file << "# 6 = Emoji Picker, 7 = Uinput (Select), 8 = Minecraft\n";
        file << "# app=mode[,commit_interval_ms[,backspace_interval_ms[,post_delay_ms]]]   0/omitted = use defaults\n";
        std::scoped_lock lock(appRulesMutex_);
        for (const auto& pair : appRules_) {
            bool currentIsCtx = pair.first.starts_with("ctx_");
            if (!currentIsCtx) {
                const int   commit    = pair.second.commitInterval;
                const int   backspace = pair.second.backspaceInterval;
                const int   post      = pair.second.postDelay;
                std::string value     = std::to_string(modeToInt(pair.second.mode));
                if (commit > 0 || backspace > 0 || post > 0)
                    value += "," + std::to_string(commit > 0 ? commit : 0);
                if (backspace > 0 || post > 0)
                    value += "," + std::to_string(backspace > 0 ? backspace : 0);
                if (post > 0)
                    value += "," + std::to_string(post);
                file << pair.first << "=" << value << "\n";
            }
        }
        file.close();
    }

    LotusAppRuleSetting LotusEngine::getAppRuleSetting(const std::string& appName) const {
        std::scoped_lock lock(appRulesMutex_);
        auto             it = appRules_.find(appName);
        if (it != appRules_.end()) {
            return it->second;
        }
        return {.mode = config_.mode.value(), .commitInterval = 0, .backspaceInterval = 0, .postDelay = 0};
    }

    LotusMode LotusEngine::getAppRule(const std::string& appName) const {
        return getAppRuleSetting(appName).mode;
    }

    void LotusEngine::setAppRule(const std::string& appName, LotusMode mode) {
        auto rules = *appRulesTables_.rules;

        bool found = false;
        for (auto& rule : rules) {
            if (*rule.app == appName) {
                rule.mode.setValue(modeToInt(mode));
                found = true;
                break;
            }
        }

        if (!found) {
            lotusAppRule newRule;
            newRule.app.setValue(appName);
            newRule.mode.setValue(modeToInt(mode));
            rules.push_back(std::move(newRule));
        }

        {
            std::scoped_lock lock(appRulesMutex_);
            appRules_[appName].mode = mode;
        }
        appRulesTables_.rules.setValue(std::move(rules));
    }

    void LotusEngine::closeAppModeMenu() {
        isSelectingAppMode_ = false;
        g_mouse_clicked.store(false, std::memory_order_release);
    }

    void LotusEngine::showAppModeMenu(InputContext* ic) {
        isSelectingAppMode_ = true;

        auto candidateList = std::make_unique<CommonCandidateList>();

        candidateList->setLayoutHint(CandidateLayoutHint::Vertical);
        candidateList->setPageSize(10);

        auto getLabel = [&](const LotusMode& modeName, const std::string& modeLabel) {
            if (modeName == realMode) {
                return Text(">> " + modeLabel);
            }
            return Text("   " + modeLabel);
        };

        auto cleanup = [this](InputContext* ic) {
            isSelectingAppMode_ = false;
            ic->inputPanel().reset();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            auto* state = ic->propertyFor(&factory_);
            state->commitBuffer();
            state->reset();
        };

        auto applyMode = [this, cleanup](LotusMode mode, bool isDefault = false) {
            return [this, mode, cleanup, isDefault](InputContext* ic) {
                if (mode != LotusMode::Emoji) {
                    if (isDefault) {
                        clearAppRule(currentConfigureApp_);
                    } else {
                        setAppRule(currentConfigureApp_, mode);
                        if (!currentConfigureApp_.starts_with("ctx_")) {
                            saveAppRules();
                        }
                    }
                }

                cleanup(ic);
                setMode(mode, ic);
                if (mode == LotusMode::Emoji) {
                    auto* state = ic->propertyFor(&factory_);
                    state->updateEmojiPreedit();
                } else {
                    showCycleModeNotification(mode, ic);
                }
            };
        };

        const std::vector<ModeOrderEntry> allModes = modeOrderEntries(config_);

        int                               activeSelectionIdx  = -1;
        int                               currentCandidateIdx = 0;
        std::unordered_set<KeySym>        usedModeKeys;

        modeMenuMapping_.clear();
        const LotusMode defaultMode = config_.mode.value();

        for (const auto& entry : allModes) {
            if (entry.visible) {
                const bool hasShortcut = entry.key != FcitxKey_None && entry.key != FcitxKey_VoidSymbol;
                if (hasShortcut && usedModeKeys.insert(entry.key).second) {
                    modeMenuMapping_[entry.key] = entry.mode;
                }

                const bool        isDefaultItem = entry.isDefault;
                const std::string keyUtf8       = Key::keySymToUTF8(entry.key);
                std::string       keyLabel      = keyUtf8.empty() ? "" : "[" + keyUtf8 + "] ";
                candidateList->append(std::make_unique<AppModeCandidateWord>(getLabel(entry.mode, keyLabel + modeLabel(entry)), applyMode(entry.mode, isDefaultItem)));

                const bool selectedByMode    = entry.mode == realMode && !isDefaultItem;
                const bool selectedByDefault = isDefaultItem && getAppRule(currentConfigureApp_) == defaultMode && !appRules_.contains(currentConfigureApp_);
                if (selectedByMode || selectedByDefault) {
                    activeSelectionIdx = currentCandidateIdx;
                }

                currentCandidateIdx++;
            }
        }

        {
            const auto& kl = *config_.modeMenuKey;
            if (kl.size() == 1 && !kl[0].hasModifier()) {
                std::string charStr = Key::keySymToUTF8(kl[0].sym());
                if (!charStr.empty()) {
                    KeySym      typeKeySym   = typeKeyForModeMenuHotkey(kl[0].sym(), config_);
                    std::string typeKeyLabel = Key::keySymToUTF8(typeKeySym);
                    std::string label        = "[" + typeKeyLabel + "] " + _("Type") + " " + charStr;
                    candidateList->append(std::make_unique<AppModeCandidateWord>(Text(label), [cleanup, charStr](InputContext* ic) {
                        cleanup(ic);
                        ic->commitString(charStr);
                    }));
                }
            }
        }

        if (activeSelectionIdx != -1) {
            candidateList->setGlobalCursorIndex(activeSelectionIdx);
        } else if (candidateList->totalSize() > 0) {
            candidateList->setGlobalCursorIndex(0);
        }

        ic->inputPanel().reset();
        ic->inputPanel().setCandidateList(std::move(candidateList));
        ic->inputPanel().setAuxDown(Text(_("App: ") + currentConfigureApp_));
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    void LotusEngine::showCycleModeNotification(LotusMode mode, InputContext* ic) {
        auto candidateList = std::make_unique<CommonCandidateList>();
        candidateList->setLayoutHint(CandidateLayoutHint::Vertical);
        candidateList->setPageSize(1);

        auto cleanup = [](InputContext* ic) {
            ic->inputPanel().reset();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        };

        const std::string modeLabel = LotusModeI18NAnnotation::toString(mode);

        auto              setCurrentMode = [cleanup](LotusMode) { return [cleanup](InputContext* ic) { cleanup(ic); }; };

        candidateList->append(std::make_unique<AppModeCandidateWord>(Text("✓ " + modeLabel), setCurrentMode(mode)));

        candidateList->setGlobalCursorIndex(0);

        ic->inputPanel().reset();
        ic->inputPanel().setCandidateList(std::move(candidateList));
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);

        // Cancel previous timer if any
        cycleModeNotificationTimer_.reset();

        // Schedule auto-close using EventLoop::addTimeEvent
        auto& eventLoop    = instance_.eventLoop();
        auto  now_time     = ::fcitx::now(CLOCK_MONOTONIC);
        auto  timeout_time = now_time + CYCLE_MODE_NOTIFICATION_TIMEOUT_USEC;

        cycleModeNotificationTimer_ = eventLoop.addTimeEvent(CLOCK_MONOTONIC, timeout_time, 0, [icRef = ic->watch()](EventSourceTime*, uint64_t) {
            if (auto* ic = icRef.get(); ic && ic->hasFocus()) {
                ic->inputPanel().reset();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            }
            return false;
        });
    }

    void LotusEngine::setMode(LotusMode mode, InputContext* ic) {
        realMode = mode;
        if (ic != nullptr) {
            if (auto* state = ic->propertyFor(&factory_)) {
                state->appRuleSetting_ = getAppRuleSetting(getProgramName(ic));
                state->clearAllBuffers();
            }
            ic->updateUserInterface(UserInterfaceComponent::StatusArea);
        }
    }

    std::string LotusEngine::subModeIconImpl(const InputMethodEntry& /*entry*/, InputContext& /*inputContext*/) {
        std::string baseIconName;
        switch (realMode) {
            case LotusMode::Off: baseIconName = "fcitx-lotus-off"; break;
            case LotusMode::Emoji: baseIconName = "fcitx-lotus-emoji"; break;
            default: baseIconName = "fcitx-lotus"; break;
        }

        std::string iconName;
        if (*config_.useLotusIcons) {
            iconName = baseIconName;
        } else {
            const auto& iconTheme = config_.iconTheme.value();
            if (iconTheme == IconTheme::Light) {
                iconName = baseIconName + "-default-black";
            } else if (iconTheme == IconTheme::Dark) {
                iconName = baseIconName + "-default";
            } else {
                iconName = baseIconName + (isDarkMode() ? "-default" : "-default-black");
            }
        }
        return iconName;
    }

    std::string LotusEngine::subModeLabelImpl(const InputMethodEntry& /*entry*/, InputContext& /*inputContext*/) {
        switch (realMode) {
            case LotusMode::Off: return _("Lotus - Off");
            case LotusMode::Emoji: return "😄";
            default: return isGnome_ ? "vi" : "🪷";
        }
    }

    std::string LotusEngine::getProgramName(InputContext* ic) {
        if (ic == nullptr) {
            return "unknown-app";
        }
        std::string programName = ic->program();
        if (programName.empty() || programName == "wayland" || programName == "x11") {
            // Fallback: InputContext address-based resolution
            // This ensures at least per-window separation.
            std::ostringstream oss;
            oss << "ctx_" << static_cast<const void*>(ic);
            programName = oss.str();
        }
        return programName;
    }

    void LotusEngine::clearAppRule(const std::string& appName) {
        {
            std::scoped_lock lock(appRulesMutex_);
            appRules_.erase(appName);
        }
        auto rules = *appRulesTables_.rules;
        rules.erase(std::ranges::remove_if(rules, [&appName](const auto& rule) { return *rule.app == appName; }).begin(), rules.end());
        appRulesTables_.rules.setValue(std::move(rules));
        if (!appName.starts_with("ctx_")) {
            saveAppRules();
        }
    }

    void LotusEngine::setupMouseWatcher() {
        if (mouseEventSource_) {
            return;
        }

        const std::string  mouse_socket_path = buildSocketPath("mouse_socket");
        struct sockaddr_un addr{};
        socklen_t          len = 0;
        FdGuard            sock(makeAbstractUnixSocket(mouse_socket_path, addr, len));
        if (!sock.is_valid()) {
            LOTUS_ERROR("Failed to create mouse socket: " + std::string(strerror(errno)));
            return;
        }

        if (connect(sock.get(), reinterpret_cast<struct sockaddr*>(&addr), len) < 0) {
            LOTUS_ERROR("Failed to connect to mouse socket: " + std::string(strerror(errno)));
            return;
        }

        std::string peer_exe_path;
        if (!authenticateMouseSocketPeer(sock.get(), peer_exe_path)) {
            LOTUS_WARN("Unauthorized connection attempt from: " + peer_exe_path);
            return;
        }

        const int sockFd = sock.get();
        mouse_socket_fd.store(sock.release(), std::memory_order_release);
        LOTUS_INFO("Mouse socket connected and registered to Fcitx EventLoop.");

        mouseEventSource_ = instance_.eventLoop().addIOEvent(sockFd, IOEventFlag::In, [this, sockFd](EventSourceIO*, int, IOEventFlags) {
            std::array<char, 16> buf{};
            ssize_t              n = recv(sockFd, buf.data(), buf.size(), 0);
            if (n <= 0) {
                LOTUS_ERROR("Mouse socket recv error or disconnected: " + std::string(strerror(errno)));
                mouseEventSource_.reset();
                mouse_socket_fd.store(-1, std::memory_order_release);
                close(sockFd);
                return true;
            }

            if (n >= 1 && buf[0] == 'C') {
                LOTUS_DEBUG("Mouse click detected from server. Resetting.....");
                g_mouse_clicked.store(true, std::memory_order_release);
                instance_.inputContextManager().foreachFocused([this](InputContext* ic) {
                    if (ic) {
                        auto* state = ic->propertyFor(&factory_);
                        if (state) {
                            state->clearAllBuffers();
                        }
                    }
                    return true;
                });
            }
            return true;
        });
    }
} // namespace fcitx
