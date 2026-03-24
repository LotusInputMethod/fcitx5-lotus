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
#include "ack-apps.h"
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
        startMonitoringOnce();
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
        initToggleAction(autoNonVnRestoreAction_, config_.autoNonVnRestore, "lotus-autonvnrestore", "edit-undo", _("Auto Restore Keys With Invalid Wwords"),
                         _("Auto Non-VN Restore"), uiManager);
        initToggleAction(enableDictionaryAction_, config_.enableDictionary, "lotus-dictionary", "accessories-dictionary", _("Enable Custom Dictionary"), _("Custom Dictionary"),
                         uiManager);

        settingsAction_ = std::make_unique<SimpleAction>();
        settingsAction_->setShortText(_("Settings"));
        settingsAction_->setIcon("configure");
        connections_.emplace_back(settingsAction_->connect<SimpleAction::Activated>([](InputContext*) {
            if (fork() == 0) {
                execlp("fcitx5-lotus-settings", "fcitx5-lotus-settings", nullptr);
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
        toggleActions_ = {
#ifndef DISABLE_VERSION_ACTION
            versionAction_.get(),
#endif
            charsetAction_.get(),          spellCheckAction_.get(),       macroAction_.get(),   capitalizeMacroAction_.get(),
            autoNonVnRestoreAction_.get(), enableDictionaryAction_.get(), settingsAction_.get()};

        emptyCustomKeymap_.customKeymap.setValue(std::vector<lotusKeymap>{});
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
            close(fd);
        }
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
            saveAppRules();
            refreshEngine();
        }
    }

    std::string LotusEngine::subMode(const InputMethodEntry& /*entry*/, InputContext& /*inputContext*/) {
        return *config_.inputMethod;
    }

    void LotusEngine::activate(const InputMethodEntry& entry, InputContextEvent& event) {
        FCITX_UNUSED(entry);
        auto*                    ic = event.inputContext();
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
        reloadConfig();
        updateCharsetAction(event.inputContext());

        setMode(targetMode, event.inputContext());

        auto* state = ic->propertyFor(&factory_);

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
                        state->waitAck_ = true;
                        LOTUS_INFO(ackApp + " detected, waiting for ack");
                        break;
                    }
                }
            }
        }

        state->clearAllBuffers();
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

    void LotusEngine::keyEvent(const InputMethodEntry& entry, KeyEvent& keyEvent) {
        FCITX_UNUSED(entry);
        auto* ic = keyEvent.inputContext();

        if (isSelectingAppMode_ && g_mouse_clicked.load(std::memory_order_relaxed)) {
            closeAppModeMenu();
            ic->inputPanel().reset();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            auto* state = ic->propertyFor(&factory_);
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
                state->reset();
                if (selectedMode != LotusMode::NoMode) {
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
            g_mouse_clicked.store(false, std::memory_order_relaxed);
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
            realtextLen = static_cast<unsigned int>(textLen);
    }

    void LotusEngine::reset(const InputMethodEntry& entry, InputContextEvent& event) {
        LOTUS_INFO("Reset engine");
        FCITX_UNUSED(entry);
        auto* state = event.inputContext()->propertyFor(&factory_);
        if (!state->isEmptyHistory() && event.type() != EventType::InputContextFocusOut) {
            return;
        }

        if (event.type() == EventType::InputContextFocusOut) {
            state->reset();
        }
    }

    void LotusEngine::deactivate(const InputMethodEntry& entry, InputContextEvent& event) {
        FCITX_UNUSED(entry);
        auto* ic    = event.inputContext();
        auto* state = ic->propertyFor(&factory_);
        if (realMode == LotusMode::Preedit && event.type() != EventType::InputContextFocusOut) {
            state->commitBuffer();
        } else {
            state->clearAllBuffers();
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
#if LOTUS_USE_MODERN_FCITX_API
        std::string configDir = (StandardPaths::global().userDirectory(StandardPathsType::Config) / "fcitx5" / "conf").string();
#else
        std::string configDir = StandardPath::global().userDirectory(StandardPath::Type::Config) + "/fcitx5/conf";
#endif
        std::ifstream file(configDir + "/lotus-app-rules.conf");
        if (!file.is_open()) {
            return;
        }
        std::vector<lotusAppRule> rules;
        std::string               line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            auto delimiterPos = line.find('=');
            if (delimiterPos != std::string::npos) {
                std::string  app  = line.substr(0, delimiterPos);
                std::string  mode = line.substr(delimiterPos + 1);
                lotusAppRule rule;
                rule.app.setValue(app);
                try {
                    rule.mode.setValue(std::stoi(mode));
                } catch (const std::exception& e) { rule.mode.setValue(0); }
                rules.push_back(std::move(rule));
            }
        }
        file.close();
        appRulesTables_.rules.setValue(std::move(rules));
    }

    void LotusEngine::saveAppRules() const {
#if LOTUS_USE_MODERN_FCITX_API
        std::string configDir = (StandardPaths::global().userDirectory(StandardPathsType::Config) / "fcitx5" / "conf").string();
#else
        std::string configDir = StandardPath::global().userDirectory(StandardPath::Type::Config) + "/fcitx5/conf";
#endif
        std::ofstream file(configDir + "/lotus-app-rules.conf");
        if (!file.is_open())
            return;

        file << "# Lotus Per-App Configuration\n";
        file << "# 0 = Off, 1 = Uinput (Smooth), 2 = Uinput (Slow), 3 = Uinput (Hardcore), 4 = Surrounding Text, 5 = Preedit, 6 = Emoji Picker\n";
        auto appRules = appRulesTables_.rules.value();
        for (const auto& pair : appRules) {
            if (!isStartsWith(pair.app.value(), "ctx_")) {
                file << pair.app.value() << "=" << static_cast<int>(pair.mode.value()) << "\n";
            }
        }
        file.close();
    }

    LotusMode LotusEngine::getAppRule(const std::string& appName) const {
        for (const auto& rule : *appRulesTables_.rules) {
            if (*rule.app == appName) {
                return static_cast<LotusMode>(*rule.mode);
            }
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

        appRulesTables_.rules.setValue(std::move(rules));
    }

    void LotusEngine::closeAppModeMenu() {
        isSelectingAppMode_ = false;
        g_mouse_clicked.store(false, std::memory_order_relaxed);
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

    std::string LotusEngine::subModeIconImpl(const InputMethodEntry& entry, InputContext& inputContext) {
        FCITX_UNUSED(entry);
        FCITX_UNUSED(inputContext);
        if (!*config_.useLotusIcons) {
            switch (realMode) {
                case LotusMode::Off: return "fcitx-lotus-off-default";
                case LotusMode::Emoji: return "fcitx-lotus-emoji-default";
                default: return "fcitx-lotus-default";
            }
        }
        switch (realMode) {
            case LotusMode::Off: return "fcitx-lotus-off";
            case LotusMode::Emoji: return "fcitx-lotus-emoji";
            default: return "fcitx-lotus";
        }
    }

    std::string LotusEngine::subModeLabelImpl(const InputMethodEntry& entry, InputContext& inputContext) {
        FCITX_UNUSED(entry);
        FCITX_UNUSED(inputContext);
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
