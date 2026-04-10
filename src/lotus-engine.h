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
#include "lotus.h"
#include <mutex>
#include <fcitx-config/iniparser.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

namespace fcitx {

    class CGoObject;
    class LotusState;

    /**
     * @brief Main engine class for Lotus input method.
     *
     * Handles input processing, configuration management, and UI actions.
     * Implements fcitx InputMethodEngine interface.
     */
    class LotusEngine final : public InputMethodEngineV2 {
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
        LotusEngine(Instance* instance);

        /**
         * @brief Destroys the engine and releases resources.
         */
        ~LotusEngine();

        // Rule of five
        LotusEngine(const LotusEngine&)            = delete;
        LotusEngine& operator=(const LotusEngine&) = delete;
        LotusEngine(LotusEngine&&)                 = delete;
        LotusEngine& operator=(LotusEngine&&)      = delete;

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
         * @brief Gets the dictionary handle.
         * @return CGo handle for the dictionary.
         */
        uintptr_t dictionary() const {
            return dictionary_.handle();
        }

        /**
         * @brief Gets the macro table handle.
         * @return CGo handle for the macro table.
         */
        uintptr_t macroTable() const;

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
        Instance*                                  instance_;
        lotusConfig                                config_;
        lotusCustomKeymap                          customKeymap_;
        lotusCustomKeymap                          emptyCustomKeymap_;

        lotusMacroTable                            macroTables_;
        CGoObject                                  macroTableObject_;
        lotusAppRules                              appRulesTables_;

        FactoryFor<LotusState>                     factory_;
        std::vector<std::string>                   imNames_;

        std::unique_ptr<SimpleAction>              charsetAction_;
        std::vector<std::unique_ptr<SimpleAction>> charsetSubAction_;
        std::unique_ptr<Menu>                      charsetMenu_;

        std::unique_ptr<SimpleAction>              spellCheckAction_;
        std::unique_ptr<SimpleAction>              macroAction_;
        std::unique_ptr<SimpleAction>              capitalizeMacroAction_;
        std::unique_ptr<SimpleAction>              autoNonVnRestoreAction_;
        std::unique_ptr<SimpleAction>              enableDictionaryAction_;
        std::unique_ptr<SimpleAction>              settingsAction_;
        std::vector<SimpleAction*>                 toggleActions_;
        std::vector<ScopedConnection>              connections_;
        CGoObject                                  dictionary_;
        std::unordered_map<std::string, LotusMode> appRules_;
        std::string                                appRulesPath_;
        bool                                       isSelectingAppMode_ = false;
        std::string                                currentConfigureApp_;
        FCITX_ADDON_DEPENDENCY_LOADER(emoji, instance_->addonManager());
        std::unique_ptr<EmojiLoader>          emojiLoader_;
        bool                                  isGnome_ = false;
        mutable std::mutex                    appRulesMutex_;
        std::unordered_map<KeySym, LotusMode> modeMenuMapping_;

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
        static void updateAction(InputContext* ic, std::unique_ptr<SimpleAction>& action, Option<bool>& option, const std::string& textOnOff);

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
         * @brief Shows the application mode selection menu.
         * @param ic Current input context.
         */
        void showAppModeMenu(InputContext* ic);

        /**
         * @brief Closes the application mode selection menu.
         */
        void closeAppModeMenu();

        /**
         * @brief Sets the current input mode.
         * @param mode The mode to set.
         * @param ic Current input context.
         */
        static void setMode(LotusMode mode, InputContext* ic);

        /**
         * @brief Get name of current program
         * @param ic Current input context.
         * @return Name of current program
         */
        static std::string getProgramName(InputContext* ic);
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
