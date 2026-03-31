/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-engine.h"
#include "lotus-config.h"
#include "lotus-state.h"
#include "lotus-candidates.h"
#include "lotus-monitor.h"
#include "lotus-utils.h"
#include "app_quirks.h"
#include <sys/socket.h>
#include <utility>
#ifndef DISABLE_VERSION_ACTION
#include "lotus-version.h"
#endif

#include <fcitx-config/iniparser.h>
#include <fcitx/menu.h>
#include <fcitx/userinterfacemanager.h>
#include <fcitx-utils/utf8.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <fcntl.h>
#include <sstream>

namespace fcitx {
    constexpr const char* CharsetActionPrefix = "lotus-charset-";
    const std::string     CustomKeymapFile    = "conf/lotus-custom-keymap.conf";
    const std::string     MacroTableFile      = "conf/lotus-macro-table.conf";

    // Returns the KeySym that triggers the "Type hotkey char" action in the mode
    // menu.  If the hotkey itself conflicts with a reserved menu key, falls back
    // to FcitxKey_f.
    static bool isAppModeMenuReservedKey(KeySym sym) {
        switch (sym) {
            case FcitxKey_1:
            case FcitxKey_2:
            case FcitxKey_3:
            case FcitxKey_4:
            case FcitxKey_q:
            case FcitxKey_w:
            case FcitxKey_e:
            case FcitxKey_r:
            case FcitxKey_Escape:
            case FcitxKey_Tab:
            case FcitxKey_ISO_Left_Tab:
            case FcitxKey_Return:
            case FcitxKey_space:
            case FcitxKey_Up:
            case FcitxKey_Down: return true;
            default: return false;
        }
    }

    static KeySym typeKeyForModeMenuHotkey(KeySym hotkeySym) {
        return isAppModeMenuReservedKey(hotkeySym) ? FcitxKey_f : hotkeySym;
    }

    static inline uintptr_t newMacroTable(const lotusMacroTable& macroTable) {
        const auto&        macros = *macroTable.macros;
        std::vector<char*> charArray;
        charArray.reserve((macros.size() * 2) + 1);
        for (const auto& keymap : macros) {
            // External C API doesn't use const, but doesn't modify data
            charArray.push_back(const_cast<char*>(keymap.key->data()));   //NOLINT
            charArray.push_back(const_cast<char*>(keymap.value->data())); //NOLINT
        }
        charArray.push_back(nullptr);
        return NewMacroTable(charArray.data());
    }

    static inline std::vector<std::string> convertToStringList(char** list) {
        std::vector<std::string> result;
        if (list != nullptr) {
            for (size_t i = 0; list[i] != nullptr; ++i) { //NOLINT
                result.emplace_back(list[i]);             //NOLINT
                free(list[i]);                            //NOLINT
            }
            free(list); //NOLINT
        }
        return result;
    }

    uintptr_t LotusEngine::macroTable() const {
        if (config_.inputMethod.value().empty()) {
            return 0;
        }
        return macroTableObject_.handle();
    }

    LotusEngine::LotusEngine(Instance* instance) : instance_(instance), factory_([this](InputContext& ic) { return new LotusState(this, &ic); }) { //NOLINT
        const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
        isGnome_            = (desktop != nullptr) && std::string(desktop).find("GNOME") != std::string::npos;
        // emptyCustomKeymap_.customKeymap is implicitly initialized to empty by fcitx::Option default value macro.
        startMonitoring();
        Init();
        {
            auto imNames = convertToStringList(GetInputMethodNames());
            imNames.push_back("Custom");
            imNames_ = std::move(imNames);
        }
        config_.inputMethod.annotation().setList(imNames_);

        auto& uiManager = instance_->userInterfaceManager();

#ifndef DISABLE_VERSION_ACTION
        versionAction_ = std::make_unique<SimpleAction>();
        versionAction_->setShortText("Lotus " LOTUS_VERSION_STRING);
        versionAction_->setLongText("Lotus Input Method v" LOTUS_VERSION_STRING);
        versionAction_->setIcon("help-about");
        uiManager.registerAction("lotus-version", versionAction_.get());
#endif

        charsetAction_ = std::make_unique<SimpleAction>();
        charsetAction_->setShortText(_("Charset"));
        charsetAction_->setIcon("character-set");
        uiManager.registerAction("lotus-charset", charsetAction_.get());
        charsetMenu_ = std::make_unique<Menu>();
        charsetAction_->setMenu(charsetMenu_.get());

        auto charsets = convertToStringList(GetCharsetNames());
        for (const auto& charset : charsets) {
            charsetSubAction_.emplace_back(std::make_unique<SimpleAction>());
            auto* action = charsetSubAction_.back().get();
            action->setShortText(charset);
            action->setCheckable(true);
            uiManager.registerAction(stringutils::concat(CharsetActionPrefix, charset), action);
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
        config_.outputCharset.annotation().setList(charsets);

        initToggleAction(spellCheckAction_, config_.spellCheck, "lotus-spellcheck", "tools-check-spelling", _("Enable Spell Check"), _("Spell Check"), uiManager);
        initToggleAction(macroAction_, config_.enableMacro, "lotus-macro", "document-edit", _("Enable Macro"), _("Macro"), uiManager);
        initToggleAction(capitalizeMacroAction_, config_.capitalizeMacro, "lotus-capitalizemacro", "format-text-uppercase", _("Capitalize Macro"), _("Capitalize Macro"),
                         uiManager);
        initToggleAction(autoNonVnRestoreAction_, config_.autoNonVnRestore, "lotus-autonvnrestore", "edit-undo", _("Auto Restore Keys With Invalid Words"),
                         _("Auto Non-VN Restore"), uiManager);
        initToggleAction(enableDictionaryAction_, config_.enableDictionary, "lotus-dictionary", "accessories-dictionary", _("Enable Custom Dictionary"), _("Custom Dictionary"),
                         uiManager);

        settingsAction_ = std::make_unique<SimpleAction>();
        settingsAction_->setShortText(_("Settings"));
        settingsAction_->setIcon("configure");
        connections_.emplace_back(settingsAction_->connect<SimpleAction::Activated>([](InputContext*) {
            if (fork() == 0) {
                execl(FCITX5_LOTUS_SETTINGS_PATH, FCITX5_LOTUS_SETTINGS_PATH, nullptr);
                _exit(1);
            }
        }));
        uiManager.registerAction("lotus-settings", settingsAction_.get());

#if LOTUS_USE_MODERN_FCITX_API
        std::string configDir = (StandardPaths::global().userDirectory(StandardPathsType::Config) / "fcitx5" / "conf").string();
#else
        std::string configDir = StandardPath::global().userDirectory(StandardPath::Type::Config) + "/fcitx5/conf";
#endif

        if (!std::filesystem::exists(configDir)) {
            std::filesystem::create_directories(configDir);
        }
        reloadConfig();
        instance_->inputContextManager().registerProperty("LotusState", &factory_);
        appRulesPath_ = configDir + "/lotus-app-rules.conf";
        loadAppRules();
        toggleActions_ = {
#ifndef DISABLE_VERSION_ACTION
            versionAction_.get(),
#endif
            charsetAction_.get(),          spellCheckAction_.get(),       macroAction_.get(),   capitalizeMacroAction_.get(),
            autoNonVnRestoreAction_.get(), enableDictionaryAction_.get(), settingsAction_.get()};
    }

    void LotusEngine::initToggleAction(std::unique_ptr<SimpleAction>& action, Option<bool>& option, const std::string& actionId, const std::string& iconName,
                                       const std::string& textLong, const std::string& textOnOff, UserInterfaceManager& uiManager) {
        action = std::make_unique<SimpleAction>();
        action->setShortText(textLong);
        action->setIcon(iconName);
        action->setCheckable(false);
        connections_.emplace_back(action->connect<SimpleAction::Activated>([this, &action, &option, textOnOff](InputContext* ic) {
            option.setValue(!option.value());
            saveConfig();
            refreshOption();
            updateAction(ic, action, option, textOnOff);
        }));
        uiManager.registerAction(actionId, action.get());
    }

    void LotusEngine::updateAction(InputContext* ic, std::unique_ptr<SimpleAction>& action, Option<bool>& option, const std::string& textOnOff) {
        action->setShortText((option.value() ? "✔ " : "✖ ") + textOnOff);
        if (ic != nullptr) {
            action->update(ic);
        }
    }

    LotusEngine::~LotusEngine() {
        stop_flag_monitor.store(true, std::memory_order_release);
        monitor_cv.notify_all();
        int fd = mouse_socket_fd.load(std::memory_order_acquire);
        if (fd >= 0) {
            shutdown(fd, SHUT_RDWR);
        }
        if (mouse_thread.joinable()) {
            mouse_thread.join();
        }
        if (monitor_thread.joinable()) {
            monitor_thread.join();
        }
        int old_fd = uinput_client_fd_.exchange(-1);
        if (old_fd != -1) {
            close(old_fd);
        }
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
        macroTableObject_.reset(newMacroTable(macroTables_));
        if (config_.enableDictionary.value()) {
#if LOTUS_USE_MODERN_FCITX_API
            auto fd = StandardPaths::global().open(StandardPathsType::PkgData, "lotus/vietnamese.cm.dict");
#else
            auto fd = StandardPath::global().open(StandardPath::Type::PkgData, "lotus/vietnamese.cm.dict", O_RDONLY);
#endif
            if (fd.isValid()) {
                dictionary_.reset(NewDictionary(fd.release()));
            }
        } else {
#if LOTUS_USE_MODERN_FCITX_API
            auto paths = StandardPaths::global().locateAll(StandardPathsType::PkgData, "lotus/vietnamese.cm.dict");
#else
            auto paths = StandardPath::global().locateAll(StandardPath::Type::PkgData, "lotus/vietnamese.cm.dict");
#endif
            for (const auto& p : paths) {
#if LOTUS_USE_MODERN_FCITX_API
                if (!isStartsWith(p.string(), "/home/")) {
                    auto fd = fcitx::UnixFD(::open(p.c_str(), O_RDONLY));
                    if (fd.isValid()) {
                        dictionary_.reset(NewDictionary(fd.release()));
#else
                if (!isStartsWith(p, "home/")) {
                    int fd = ::open(p.c_str(), O_RDONLY);
                    if (fd != -1) {
                        dictionary_.reset(NewDictionary(fd));
#endif
                        break;
                    }
                }
            }
        }
        loadAppRules();
        populateConfig();
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
        populateConfig();
    }

    void LotusEngine::populateConfig() {
        refreshEngine();
        refreshOption();
        updateCharsetAction(nullptr);
        updateAction(nullptr, spellCheckAction_, config_.spellCheck, _("Spell Check"));
        updateAction(nullptr, macroAction_, config_.enableMacro, _("Macro"));
        updateAction(nullptr, capitalizeMacroAction_, config_.capitalizeMacro, _("Capitalize Macro"));
        updateAction(nullptr, autoNonVnRestoreAction_, config_.autoNonVnRestore, _("Auto Non-VN Restore"));
        updateAction(nullptr, enableDictionaryAction_, config_.enableDictionary, _("Custom Dictionary"));
    }

    void LotusEngine::setSubConfig(const std::string& path, const RawConfig& config) {
        if (path == "custom_keymap") {
            customKeymap_.load(config, true);
            safeSaveAsIni(customKeymap_, CustomKeymapFile);
            refreshEngine();
        } else if (path == "lotus-macro") {
            macroTables_.load(config, true);
            safeSaveAsIni(macroTables_, MacroTableFile);
            macroTableObject_.reset(newMacroTable(macroTables_));
            refreshEngine();
        } else if (path == "app_rules") {
            appRulesTables_.load(config, true);
            {
                std::lock_guard<std::mutex> lock(appRulesMutex_);
                for (auto it = appRules_.begin(); it != appRules_.end();) {
                    if (!isStartsWith(it->first, "ctx_")) {
                        it = appRules_.erase(it);
                    } else {
                        ++it;
                    }
                }
                for (const auto& rule : *appRulesTables_.rules) {
                    appRules_[*rule.app] = static_cast<LotusMode>(*rule.mode);
                }
            }
            saveAppRules();
            refreshEngine();
        }
    }

    std::string LotusEngine::subMode(const InputMethodEntry& /*entry*/, InputContext& /*inputContext*/) {
        return *config_.inputMethod;
    }

    void LotusEngine::activate(const InputMethodEntry& /*entry*/, InputContextEvent& event) {
        auto*                    ic        = event.inputContext();
        const bool               surrvalid = ic->surroundingText().isValid();
        const bool               is_dbus   = getFrontendName(ic) == "dbus";
        static std::atomic<bool> mouseThreadStarted{false};
        if (!mouseThreadStarted.exchange(true))
            startMouseReset();

        auto& statusArea = event.inputContext()->statusArea();
        if (ic->capabilityFlags().test(CapabilityFlag::Preedit))
            instance_->inputContextManager().setPreeditEnabledByDefault(true);

        std::string appName = getProgramName(ic);
        LOTUS_INFO("App name: " + appName);

        const LotusMode targetMode = getAppRule(appName);
        LOTUS_INFO("Target mode: " + modeEnumToString(targetMode));

        updateCharsetAction(event.inputContext());

        setMode(targetMode, event.inputContext());

        auto* state = ic->propertyFor(&factory_);

        // Workaround for chromium wayland issue where suggestions cause a doubled
        // first character. Forwarding may prevent BS from being sent
        // to the client.
        //
        // Note that with chromium x11 we can't do anything to fixes this because
        // it not support surrounding text so can't know when it show suggestions
        //
        // TODO: Properly fixes instead ugly WA
        state->wa_chromium_flag = false;

        state->surrtp   = false;
        state->waitAck_ = false;
        if (*config_.fixUinputWithAck) {
            if (targetMode == LotusMode::Uinput || targetMode == LotusMode::UinputHC || targetMode == LotusMode::Smooth) {
#if __cplusplus >= 202002L
                std::ranges::transform(appName, appName.begin(), ::tolower);
#else
                std::transform(appName.begin(), appName.end(), appName.begin(), ::tolower);
#endif
                for (const auto& ackApp : ack_apps) {
                    if (appName.find(ackApp) != std::string::npos) {
                        if (is_dbus) {
                            state->waitAck_ = true;
                            LOTUS_INFO(std::string(ackApp) + " detected, waiting for ack");
                        }
                        state->wa_chromium_flag = true;
                        break;
                    }
                }
                for (const auto& _App : surrtp_apps) {
                    if (appName.find(_App) != std::string::npos) {
                        state->surrtp = true;
                        break;
                    }
                }
            }
        }
        if (event.type() == EventType::InputContextFocusIn && is_dbus && !surrvalid) {
            LOTUS_INFO("Skip clearAllBuffers");
        } else if (surrvalid && !state->oldPreBuffer_.empty() && (now_ms() - state->lastDeactivateTime_) < 100) {
            state->clearAllBuffers();
        }
        is_deleting_.store(false);
        needEngineReset.store(false);
        if (targetMode == LotusMode::Emoji) {
            state->updateEmojiPreedit();
        } else {
            ic->inputPanel().reset();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            ic->updatePreedit();
        }
        for (const auto& action : toggleActions_) {
            statusArea.addAction(StatusGroup::InputMethod, action);
        }
    }

    void LotusEngine::keyEvent(const InputMethodEntry& /*entry*/, KeyEvent& keyEvent) {
        auto* ic = keyEvent.inputContext();

        if (isSelectingAppMode_ && g_mouse_clicked.load(std::memory_order_acquire)) {
            closeAppModeMenu();
            ic->inputPanel().reset();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            auto* state = ic->propertyFor(&factory_);
            state->commitBuffer();
            state->reset();
        }

        if (isSelectingAppMode_) {
            if (keyEvent.isRelease())
                return;

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
                if (cursorIndex < 1 || cursorIndex >= totalSize) {
                    cursorIndex = 1;
                }

                int nextIndex = cursorIndex + delta;
                if (nextIndex < 1) {
                    nextIndex = totalSize - 1;
                } else if (nextIndex >= totalSize) {
                    nextIndex = 1;
                }

                menuList->setGlobalCursorIndex(nextIndex);
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                return true;
            };

            keyEvent.filterAndAccept();

            LotusMode selectedMode  = LotusMode::NoMode;
            bool      selectionMade = false;

            switch (keySym) {
                case FcitxKey_Tab:
                case FcitxKey_Down: {
                    if (moveCursor(1)) {
                        return;
                    }
                    break;
                }
                case FcitxKey_ISO_Left_Tab:
                case FcitxKey_Up: {
                    if (moveCursor(-1)) {
                        return;
                    }
                    break;
                }
                case FcitxKey_space:
                case FcitxKey_Return: {
                    if (menuList && !menuList->empty()) {
                        int selectedIndex = menuList->globalCursorIndex();
                        if (selectedIndex < 1 || selectedIndex >= menuList->totalSize()) {
                            selectedIndex = 1;
                        }
                        menuList->candidateFromAll(selectedIndex).select(ic);
                        return;
                    }
                    break;
                }
                case FcitxKey_1: {
                    selectedMode = LotusMode::Smooth;
                    break;
                }
                case FcitxKey_2: {
                    selectedMode = LotusMode::Uinput;
                    break;
                }
                case FcitxKey_3: {
                    selectedMode = LotusMode::UinputHC;
                    break;
                }
                case FcitxKey_4: {
                    selectedMode = LotusMode::SurroundingText;
                    break;
                }
                case FcitxKey_q: {
                    selectedMode = LotusMode::Preedit;
                    break;
                }
                case FcitxKey_w: {
                    selectedMode = LotusMode::Emoji;
                    break;
                }
                case FcitxKey_e: {
                    selectedMode = LotusMode::Off;
                    break;
                }
                case FcitxKey_r: {
                    selectedMode = modeStringToEnum(config_.mode.value());
                    break;
                }
                case FcitxKey_Escape: {
                    selectionMade = true;
                    break;
                }
                default: {
                    const auto& kl = *config_.modeMenuKey;
                    if (kl.size() == 1 && !kl[0].hasModifier()) {
                        std::string charStr = Key::keySymToUTF8(kl[0].sym());
                        if (!charStr.empty()) {
                            if (keySym == typeKeyForModeMenuHotkey(kl[0].sym())) {
                                isSelectingAppMode_ = false;
                                ic->inputPanel().reset();
                                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                                auto* state = ic->propertyFor(&factory_);
                                state->commitBuffer();
                                state->reset();
                                ic->commitString(charStr);
                                return;
                            }
                        }
                    }
                    break;
                }
            }

            if (selectedMode != LotusMode::NoMode) {
                LOTUS_INFO("Selected mode: " + modeEnumToString(selectedMode));
                if (selectedMode != LotusMode::Emoji) {
                    setAppRule(currentConfigureApp_, selectedMode);
                    if (!isStartsWith(currentConfigureApp_, "ctx_")) {
                        saveAppRules();
                    }
                }
                selectionMade = true;
            }

            if (selectionMade) {
                isSelectingAppMode_ = false;
                ic->inputPanel().reset();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                auto* state = ic->propertyFor(&factory_);

                if (selectedMode != LotusMode::NoMode) {
                    state->commitBuffer();
                    state->reset();
                    setMode(selectedMode, ic);
                    if (selectedMode == LotusMode::Emoji) {
                        state->updateEmojiPreedit();
                    }
                }
            }
            return;
        }

        if (!keyEvent.isRelease() && !config_.modeMenuKey->empty() && keyEvent.key().checkKeyList(*config_.modeMenuKey)) {
            LOTUS_INFO("Mode menu key pressed");
            currentConfigureApp_ = getProgramName(ic);
            g_mouse_clicked.store(false, std::memory_order_release);
            showAppModeMenu(ic);
            keyEvent.filterAndAccept();
            return;
        }
        auto* state = keyEvent.inputContext()->propertyFor(&factory_);
        state->keyEvent(keyEvent);
        const auto&  s       = ic->surroundingText();
        const auto&  text    = s.text();
        size_t       textLen = fcitx_utf8_strlen(text.c_str());
        unsigned int cursor  = s.cursor();
        if (textLen == static_cast<size_t>(cursor))
            realtextLen.store(static_cast<unsigned int>(textLen), std::memory_order_release);
    }

    void LotusEngine::reset(const InputMethodEntry& /*entry*/, InputContextEvent& event) {
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
        auto*      ic        = event.inputContext();
        auto*      state     = ic->propertyFor(&factory_);
        const bool surrvalid = ic->surroundingText().isValid();
        const bool is_dbus   = getFrontendName(ic) == "dbus";
        if (realMode == LotusMode::Preedit && event.type() != EventType::InputContextFocusOut) {
            state->commitBuffer();
        } else {
            if (event.type() == EventType::InputContextFocusOut && is_dbus && !surrvalid) {
                state->lastDeactivateTime_ = now_ms();
                LOTUS_INFO("Skip clearAllBuffers");
            } else {
                if (surrvalid && state->oldPreBuffer_.empty())
                    state->clearAllBuffers();
            }
            is_deleting_.store(false);
            needEngineReset.store(false);
            ic->inputPanel().reset();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            ic->updatePreedit();
        }
    }

    void LotusEngine::refreshEngine() {
        if (!factory_.registered())
            return;
        instance_->inputContextManager().foreach ([this](InputContext* ic) {
            auto* state = ic->propertyFor(&factory_);
            state->setEngine();
            if (ic->hasFocus())
                state->reset();
            return true;
        });
    }

    void LotusEngine::refreshOption() {
        if (!factory_.registered())
            return;
        instance_->inputContextManager().foreach ([this](InputContext* ic) {
            auto* state = ic->propertyFor(&factory_);
            state->setOption();
            if (ic->hasFocus())
                state->reset();
            return true;
        });
    }

    void LotusEngine::updateCharsetAction(InputContext* ic) {
        auto name = stringutils::concat(CharsetActionPrefix, *config_.outputCharset);
        for (const auto& action : charsetSubAction_) {
            action->setChecked(action->name() == name);
            if (ic != nullptr)
                action->update(ic);
        }
    }

    void LotusEngine::loadAppRules() {
        {
            std::lock_guard<std::mutex>                lock(appRulesMutex_);
            std::unordered_map<std::string, LotusMode> ctxRules;
            for (const auto& [app, mode] : appRules_) {
                if (isStartsWith(app, "ctx_")) {
                    ctxRules[app] = mode;
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

            std::unordered_map<std::string, LotusMode> tempRules;
            std::string                                line;
            while (std::getline(file, line)) {
                if (line.empty() || line[0] == '#')
                    continue;
                auto delimiterPos = line.find('=');
                if (delimiterPos != std::string::npos) {
                    std::string app  = line.substr(0, delimiterPos);
                    std::string mode = line.substr(delimiterPos + 1);
                    try {
                        tempRules[app] = static_cast<LotusMode>(std::stoi(mode));
                    } catch (const std::exception&) { LOTUS_WARN("Invalid mode value for app: " + app); }
                }
            }
            file.close();

            std::lock_guard<std::mutex> lock(appRulesMutex_);
            for (const auto& [app, mode] : tempRules) {
                appRules_[app] = mode;
            }
        };
        loadFromFile(appRulesPath_);

        std::lock_guard<std::mutex> lock(appRulesMutex_);
        std::vector<lotusAppRule>   rules;
        for (const auto& pair : appRules_) {
            if (pair.first.find("ctx_") == 0)
                continue;
            lotusAppRule rule;
            rule.app.setValue(pair.first);
            rule.mode.setValue(static_cast<int>(pair.second));
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
        file << "# 0 = Off, 1 = Uinput (Smooth), 2 = Uinput (Slow), 3 = Uinput (Hardcore), 4 = Surrounding Text, 5 = Preedit, 6 = Emoji Picker\n";
        std::lock_guard<std::mutex> lock(appRulesMutex_);
        for (const auto& pair : appRules_) {
            bool currentIsCtx = isStartsWith(pair.first, "ctx_");
            if (!currentIsCtx) {
                file << pair.first << "=" << static_cast<int>(pair.second) << "\n";
            }
        }
        file.close();
    }

    LotusMode LotusEngine::getAppRule(const std::string& appName) const {
        std::lock_guard<std::mutex> lock(appRulesMutex_);
        auto                        it = appRules_.find(appName);
        if (it != appRules_.end()) {
            return it->second;
        }
        return modeStringToEnum(config_.mode.value());
    }

    void LotusEngine::setAppRule(const std::string& appName, LotusMode mode) {
        auto rules = *appRulesTables_.rules;

        bool found = false;
        for (auto& rule : rules) {
            if (*rule.app == appName) {
                rule.mode.setValue(static_cast<int>(mode));
                found = true;
                break;
            }
        }

        if (!found) {
            lotusAppRule newRule;
            newRule.app.setValue(appName);
            newRule.mode.setValue(static_cast<int>(mode));
            rules.push_back(std::move(newRule));
        }

        {
            std::lock_guard<std::mutex> lock(appRulesMutex_);
            appRules_[appName] = mode;
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

        auto applyMode = [this, cleanup](LotusMode mode) {
            return [this, mode, cleanup](InputContext* ic) {
                if (mode != LotusMode::Emoji) {
                    setAppRule(currentConfigureApp_, mode);
                    if (!isStartsWith(currentConfigureApp_, "ctx_")) {
                        saveAppRules();
                    }
                }

                cleanup(ic);
                setMode(mode, ic);
                if (mode == LotusMode::Emoji) {
                    auto* state = ic->propertyFor(&factory_);
                    state->updateEmojiPreedit();
                }
            };
        };

        candidateList->append(std::make_unique<DisplayOnlyCandidateWord>(Text(_("App: ") + currentConfigureApp_)));
        candidateList->append(std::make_unique<AppModeCandidateWord>(getLabel(LotusMode::Smooth, _("[1] Uinput (Smooth)")), applyMode(LotusMode::Smooth)));
        candidateList->append(std::make_unique<AppModeCandidateWord>(getLabel(LotusMode::Uinput, _("[2] Uinput (Slow)")), applyMode(LotusMode::Uinput)));
        candidateList->append(std::make_unique<AppModeCandidateWord>(getLabel(LotusMode::UinputHC, _("[3] Uinput (Hardcore)")), applyMode(LotusMode::UinputHC)));
        candidateList->append(std::make_unique<AppModeCandidateWord>(getLabel(LotusMode::SurroundingText, _("[4] Surrounding Text")), applyMode(LotusMode::SurroundingText)));
        candidateList->append(std::make_unique<AppModeCandidateWord>(getLabel(LotusMode::Preedit, _("[q] Preedit")), applyMode(LotusMode::Preedit)));
        candidateList->append(std::make_unique<AppModeCandidateWord>(getLabel(LotusMode::Emoji, _("[w] Emoji Picker")), applyMode(LotusMode::Emoji)));
        candidateList->append(std::make_unique<AppModeCandidateWord>(getLabel(LotusMode::Off, _("[e] OFF")), applyMode(LotusMode::Off)));

        candidateList->append(std::make_unique<AppModeCandidateWord>(Text(_("[r] Default Typing")), [this, cleanup](InputContext* ic) {
            setMode(modeStringToEnum(config_.mode.value()), ic);
            cleanup(ic);
        }));

        {
            const auto& kl = *config_.modeMenuKey;
            if (kl.size() == 1 && !kl[0].hasModifier()) {
                std::string charStr = Key::keySymToUTF8(kl[0].sym());
                if (!charStr.empty()) {
                    KeySym      typeKeySym   = typeKeyForModeMenuHotkey(kl[0].sym());
                    std::string typeKeyLabel = Key::keySymToUTF8(typeKeySym);
                    std::string label        = "[" + typeKeyLabel + "] " + _("Type") + " " + charStr;
                    candidateList->append(std::make_unique<AppModeCandidateWord>(Text(label), [cleanup, charStr](InputContext* ic) {
                        cleanup(ic);
                        ic->commitString(charStr);
                    }));
                }
            }
        }

        int selectedIndex = 1;
        switch (realMode) {
            case LotusMode::Smooth: selectedIndex = 1; break;
            case LotusMode::Uinput: selectedIndex = 2; break;
            case LotusMode::UinputHC: selectedIndex = 3; break;
            case LotusMode::SurroundingText: selectedIndex = 4; break;
            case LotusMode::Preedit: selectedIndex = 5; break;
            case LotusMode::Emoji: selectedIndex = 6; break;
            case LotusMode::Off: selectedIndex = 7; break;
            default: selectedIndex = 1; break;
        }
        candidateList->setGlobalCursorIndex(selectedIndex);

        ic->inputPanel().reset();
        ic->inputPanel().setCandidateList(std::move(candidateList));
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    void LotusEngine::setMode(LotusMode mode, InputContext* ic) {
        realMode = mode;
        if (ic != nullptr) {
            ic->updateUserInterface(UserInterfaceComponent::StatusArea);
        }
    }

    std::string LotusEngine::subModeIconImpl(const InputMethodEntry& /*entry*/, InputContext& /*inputContext*/) {
        if (!*config_.useLotusIcons) {
            bool useBlack = *config_.useBlackDefaultIcons;
            switch (realMode) {
                case LotusMode::Off: return useBlack ? "fcitx-lotus-off-default-black" : "fcitx-lotus-off-default";
                case LotusMode::Emoji: return useBlack ? "fcitx-lotus-emoji-default-black" : "fcitx-lotus-emoji-default";
                default: return useBlack ? "fcitx-lotus-default-black" : "fcitx-lotus-default";
            }
        }
        switch (realMode) {
            case LotusMode::Off: return "fcitx-lotus-off";
            case LotusMode::Emoji: return "fcitx-lotus-emoji";
            default: return "fcitx-lotus";
        }
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
} // namespace fcitx
