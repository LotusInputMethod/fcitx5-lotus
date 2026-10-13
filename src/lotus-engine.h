/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-engine.h
 * @brief Main engine implementation for fcitx5-lotus Vietnamese input method.
 */

#ifndef _FCITX5_LOTUS_ENGINE_H_
#define _FCITX5_LOTUS_ENGINE_H_

#include "lotus-config.h"
#include "emoji.h"
#include "dictionary.h"
#include "macro_table.h"
#include <array>
#include <mutex>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#include <fcitx-config/iniparser.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx-utils/event.h>
#include "lotus-export.h"

namespace fcitx {

    class LotusState;

    /**
     * @brief One mode name accepted in Mode Order.
     */
    struct ModeNameEntry {
        std::string_view         name; ///< Name used in Mode Order.
        std::optional<LotusMode> mode; ///< Mode the name resolves to, or nothing for the synthetic entry.
    };

    /**
     * @brief Mode Order names, in the fallback order used when a name is not listed.
     */
    inline constexpr std::array<ModeNameEntry, 9> kModeNames{
        {
            {.name = "Smooth", .mode = LotusMode::Smooth},
            {.name = "Minecraft", .mode = LotusMode::Minecraft},
            {.name = "Select", .mode = LotusMode::Select},
            {.name = "SurroundingText", .mode = LotusMode::SurroundingText},
            {.name = "Preedit", .mode = LotusMode::Preedit},
            {.name = "Emoji", .mode = LotusMode::Emoji},
            {.name = "Off", .mode = LotusMode::Off},
            {.name = "SuperSmooth", .mode = LotusMode::SuperSmooth},
            {.name = "Default", .mode = std::nullopt},
        },
    };

    /**
     * @brief One mode resolved from Mode Order, in menu order.
     */
    struct ModeOrderEntry {
        LotusMode mode;      ///< Mode of the entry.
        KeySym    key;       ///< Shortcut key that selects the entry.
        bool      visible;   ///< Whether the entry is shown.
        bool      isDefault; ///< Whether the entry is the synthetic Default Typing one.
    };

    /**
     * @brief Whether a mode is shown in the mode menu and in the cycle list.
     * @param config Current configuration.
     * @param mode Mode to query.
     * @return True when the mode is visible.
     */
    bool modeVisible(const lotusConfig& config, LotusMode mode);

    /**
     * @brief Shortcut key that selects a mode from the mode menu.
     * @param config Current configuration.
     * @param mode Mode to query.
     * @return The shortcut key, or FcitxKey_None when the mode has none.
     */
    KeySym modeShortcut(const lotusConfig& config, LotusMode mode);

    /**
     * @brief Label of a menu entry, taken from the translated enum names.
     * @param entry Menu entry.
     * @return Translated label of the entry.
     */
    std::string modeLabel(const ModeOrderEntry& entry);

    /**
     * @brief Resolves the modes in Mode Order order, then the names missing from it.
     * @param config Current configuration.
     * @return The menu entries in menu order.
     */
    std::vector<ModeOrderEntry> modeOrderEntries(const lotusConfig& config);

    /**
     * @brief Parses a shortcut string from the configuration into its key symbol.
     * @param shortcut Shortcut text, as stored in the configuration.
     * @return Key symbol of the shortcut.
     */
    KeySym shortcutKey(const std::string& shortcut);

    /**
     * @brief Whether a key is reserved by the mode menu.
     * @param sym Key symbol to test.
     * @param config Current configuration.
     * @return True when the key is reserved.
     */
    bool isAppModeMenuReservedKey(KeySym sym, const lotusConfig& config);

    /**
     * @brief Key symbol that triggers the "type the hotkey character" menu entry.
     * @param hotkeySym Key symbol of the mode menu hotkey.
     * @param config Current configuration.
     * @return Key symbol used to type the hotkey character.
     */
    KeySym typeKeyForModeMenuHotkey(KeySym hotkeySym, const lotusConfig& config);

    /**
     * @brief Builds the macro table of the engine from the macro table configuration.
     * @param macroTable Macro table read from the configuration file.
     * @return Shared macro table; empty when the configuration has no macro.
     */
    std::shared_ptr<const lotus::MacroTable> buildMacroTable(const lotusMacroTable& macroTable);

    /**
     * @brief Main engine class for Lotus input method.
     *
     * Handles input processing, configuration management, and UI actions.
     * Implements fcitx InputMethodEngine interface.
     */
    class LOTUS_EXPORT LotusEngine final : public InputMethodEngineV2 {
      public:
        /**
         * @brief Gets the fcitx instance.
         * @return Pointer to the fcitx instance.
         */
        Instance* instance() const {
            return instance_;
        }

        /**
         * @brief Constructs the Lotus engine.
         * @param instance Pointer to fcitx instance.
         */
        explicit LotusEngine(Instance* instance);

        /**
         * @brief Destroys the engine and releases resources.
         */
        ~LotusEngine() override;

        // Rule of five
        LotusEngine(const LotusEngine&)            = delete;
        LotusEngine& operator=(const LotusEngine&) = delete;
        LotusEngine(LotusEngine&&)                 = delete;
        LotusEngine& operator=(LotusEngine&&)      = delete;

        /**
         * @brief Get the resolved rule (mode + delay overrides) for an application.
         * @param appName Application/program name.
         * @return The stored rule, or the global mode with zeroed delay overrides.
         */
        LotusAppRuleSetting getAppRuleSetting(const std::string& appName) const;

        /**
         * @brief Activates the input method for an input context.
         * @param entry Input method entry.
         * @param event Activation event.
         */
        void activate(const InputMethodEntry& entry, InputContextEvent& event) override;

        /**
         * @brief Deactivates the input method.
         * @param entry Input method entry.
         * @param event Deactivation event.
         */
        void deactivate(const InputMethodEntry& entry, InputContextEvent& event) override;

        /**
         * @brief Processes key events.
         * @param entry Input method entry.
         * @param keyEvent The key event to process.
         */
        void keyEvent(const InputMethodEntry& entry, KeyEvent& keyEvent) override;

        /**
         * @brief Resets the input method state.
         * @param entry Input method entry.
         * @param event Reset event.
         */
        void reset(const InputMethodEntry& entry, InputContextEvent& event) override;

        /**
         * @brief Reloads configuration from disk.
         */
        void reloadConfig() override;

        /**
         * @brief Gets the current configuration.
         * @return Pointer to configuration object.
         */
        const Configuration* getConfig() const override {
            return &config_;
        }

        /**
         * @brief Gets a sub-configuration by path.
         * @param path Sub-config path.
         * @return Pointer to sub-configuration.
         */
        const Configuration* getSubConfig(const std::string& path) const override;

        /**
         * @brief Applies configuration changes.
         * @param config New configuration.
         */
        void setConfig(const RawConfig& config) override;

        /**
         * @brief Applies sub-configuration changes.
         * @param path Sub-config path.
         * @param config New sub-configuration.
         */
        void setSubConfig(const std::string& path, const RawConfig& config) override;

        /**
         * @brief Gets the current sub-mode label.
         * @param entry Input method entry.
         * @param inputContext Current input context.
         * @return Mode label string.
         */
        std::string subMode(const InputMethodEntry& entry, InputContext& inputContext) override;

        /**
         * @brief Sets the current sub-mode icon.
         * @param entry Input method entry.
         * @param inputContext Current input context.
         * @return Icon name string.
         */
        std::string subModeIconImpl(const InputMethodEntry& entry, InputContext& inputContext) override;

        /**
         * @brief Sets the current sub-mode label.
         * @param entry Input method entry.
         * @param inputContext Current input context.
         * @return Label string.
         */
        std::string subModeLabelImpl(const InputMethodEntry& entry, InputContext& inputContext) override;

        /**
         * @brief Gets the current configuration.
         * @return Reference to lotus configuration.
         */
        const auto& config() const {
            return config_;
        }

        /**
         * @brief Gets the custom keymap configuration.
         * @return Reference to custom keymap.
         */
        const lotusCustomKeymap& customKeymap() const;

        /**
         * @brief Gets the dictionary shared with every input context.
         * @return Shared dictionary (null when no dictionary file was loaded).
         */
        const std::shared_ptr<const lotus::Dictionary>& dictionary() const;

        /**
         * @brief Gets the macro table shared with every input context.
         * @return Shared macro table.
         */
        const std::shared_ptr<const lotus::MacroTable>& macroTable() const;

        /**
         * @brief Gets the emoji loader.
         * @return Reference to emoji loader instance.
         */
        EmojiLoader& emojiLoader() {
            if (!emojiLoader_) {
                emojiLoader_ = std::make_unique<EmojiLoader>(&instance_->addonManager());
            }
            return *emojiLoader_;
        }

      private:
        Instance*                                            instance_;
        lotusConfig                                          config_;
        lotusCustomKeymap                                    customKeymap_;
        lotusCustomKeymap                                    emptyCustomKeymap_;

        lotusMacroTable                                      macroTables_;
        std::shared_ptr<const lotus::MacroTable>             macroTable_;
        lotusAppRules                                        appRulesTables_;

        FactoryFor<LotusState>                               factory_;
        std::vector<std::string>                             imNames_;

        std::unique_ptr<SimpleAction>                        charsetAction_;
        std::vector<std::unique_ptr<SimpleAction>>           charsetSubAction_;
        std::unique_ptr<Menu>                                charsetMenu_;

        std::unique_ptr<SimpleAction>                        spellCheckAction_;
        std::unique_ptr<SimpleAction>                        macroAction_;
        std::unique_ptr<SimpleAction>                        capitalizeMacroAction_;
        std::unique_ptr<SimpleAction>                        autoNonVnRestoreAction_;
        std::unique_ptr<SimpleAction>                        enableDictionaryAction_;
        std::unique_ptr<SimpleAction>                        settingsAction_;
        std::vector<SimpleAction*>                           toggleActions_;
        std::vector<ScopedConnection>                        connections_;
        std::shared_ptr<const lotus::Dictionary>             dictionary_;
        std::unordered_map<std::string, LotusAppRuleSetting> appRules_;
        std::string                                          appRulesPath_;
        bool                                                 isSelectingAppMode_ = false;
        std::string                                          currentConfigureApp_;
        std::unique_ptr<EventSourceTime>                     cycleModeNotificationTimer_;
        static constexpr uint64_t                            CYCLE_MODE_NOTIFICATION_TIMEOUT_USEC = 800000; // 800ms in microseconds
        FCITX_ADDON_DEPENDENCY_LOADER(emoji, instance_->addonManager());
        std::unique_ptr<EmojiLoader>          emojiLoader_;
        bool                                  isGnome_ = false;
        mutable std::mutex                    appRulesMutex_;
        std::unordered_map<KeySym, LotusMode> modeMenuMapping_;
        std::unique_ptr<EventSourceIO>        mouseEventSource_;

        /**
         * @brief (Re)loads the custom dictionary from disk into the Go engine handle.
         */
        void loadDictionary();

        /**
         * @brief Refreshes the bamboo engine with current settings.
         */
        void refreshEngine();

        /**
         * @brief Refreshes engine options from configuration.
         */
        void refreshOption();

        /**
         * @brief Saves current configuration to disk.
         */
        void saveConfig() {
            safeSaveAsIni(config_, "conf/lotus.conf");
        }

        /**
         * @brief Initialize toggle action
         * @param action The action to initialize
         * @param option The option to toggle
         * @param actionId The action ID
         * @param iconName The icon name
         * @param textLong The long text
         * @param textOnOff The text to display when on/off
         * @param uiManager The UI manager
         * 
        */
        void initToggleAction(std::unique_ptr<SimpleAction>& action, Option<bool>& option, const std::string& actionId, const std::string& iconName, const std::string& textLong,
                              const std::string& textOnOff, UserInterfaceManager& uiManager);

        /**
         * @brief Update toggle action
         * @param ic The input context
         * @param action The action to update
         * @param option The option to toggle
         * @param textOnOff The text to display when on/off
         */
        static void updateAction(InputContext* ic, SimpleAction& action, Option<bool>& option, const std::string& textOnOff);

        /**
         * @brief Updates the charset action UI.
         * @param ic Current input context.
         */
        void updateCharsetAction(InputContext* ic);

        /**
         * @brief Populates input method names from bamboo core.
         */
        void populateConfig();

        /**
         * @brief Loads application-specific mode rules.
         */
        void loadAppRules();

        /**
         * @brief Saves application-specific mode rules.
         */
        void saveAppRules() const;

        /**
         * @brief Get application-specific mode rules
         * @return Current application-specific mode rules
        */

        LotusMode getAppRule(const std::string& appName) const;

        /**
         * @brief Set application-specific mode rules
         * @param appName The application name
         * @param mode The mode to set
         */
        void setAppRule(const std::string& appName, LotusMode mode);

        /**
         * @brief Handles the key event while the application mode menu is open.
         * @param keyEvent The key event.
         * @return True when the event was consumed by the menu.
         */
        bool handleAppModeMenu(KeyEvent& keyEvent);

        /**
         * @brief Handles the cycle mode hotkey.
         * @param ic Current input context.
         * @param keyEvent The key event.
         * @return True when the event was consumed by the hotkey.
         */
        bool handleCycleModeHotkey(InputContext* ic, KeyEvent& keyEvent);

        /**
         * @brief Handles the mode menu hotkey.
         * @param ic Current input context.
         * @param keyEvent The key event.
         * @return True when the event was consumed by the hotkey.
         */
        bool handleModeMenuHotkey(InputContext* ic, KeyEvent& keyEvent);

        /**
         * @brief Stores the surrounding text length when the cursor sits at the end of the text.
         * @param ic Current input context.
         */
        static void syncRealtextLen(InputContext* ic);

        /**
         * @brief Collects the visible modes in Mode Order order, skipping duplicates.
         * @return The enabled modes in the order they are cycled through.
         */
        std::vector<LotusMode> enabledModesInOrder() const;

        /**
         * @brief Shows the application mode selection menu.
         * @param ic Current input context.
         */
        void showAppModeMenu(InputContext* ic);

        /**
         * @brief Shows a brief notification of the cycled mode.
         * @param mode The mode that was cycled to.
         * @param ic Current input context.
         */
        void showCycleModeNotification(LotusMode mode, InputContext* ic);

        /**
         * @brief Closes the application mode selection menu.
         */
        void closeAppModeMenu();

        /**
         * @brief Sets the current input mode.
         * @param mode The mode to set.
         * @param ic Current input context.
         */
        void setMode(LotusMode mode, InputContext* ic);

        /**
         * @brief Get name of current program
         * @param ic Current input context.
         * @return Name of current program
         */
        static std::string getProgramName(InputContext* ic);

        /**
         * @brief Detects if the system is in dark mode.
         * @return true if dark mode, false if light mode or detection failed.
         */
        static bool isDarkMode();

        /**
         * @brief Removes any per-app rule for appName so the app follows the
         *        global Mode again.
         * @param appName Name of application.
         */
        void clearAppRule(const std::string& appName);

        /**
         * @brief Connects and sets up IO watcher on Fcitx EventLoop for mouse click events.
         */
        void setupMouseWatcher();
    };

    /**
     * @brief Factory class for creating LotusEngine instances.
     */
    class LotusFactory : public AddonFactory {
      public:
        /**
         * @brief Creates a new LotusEngine instance.
         * @param manager Pointer to addon manager.
         * @return New engine instance.
         */
        AddonInstance* create(AddonManager* manager) override {
            registerDomain("fcitx5-lotus", FCITX_INSTALL_LOCALEDIR);
            return new LotusEngine(manager->instance()); // NOLINT
        }
    };

} // namespace fcitx

#endif // _FCITX5_LOTUS_ENGINE_H_
