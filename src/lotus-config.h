/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-config.h
 * @brief Configuration definitions for fcitx5-lotus input method.
 */

#ifndef _FCITX5_LOTUS_CONFIG_H_
#define _FCITX5_LOTUS_CONFIG_H_

#include <cstdint>
#include <fcitx-config/configuration.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/stringutils.h>

namespace fcitx {

    /**
     * @brief Operating modes for the Lotus input method.
     */
    enum class LotusMode : std::uint8_t {
        Off             = 0,
        Smooth          = 1,
        Uinput          = 2,
        SurroundingText = 4,
        Preedit         = 5,
        Emoji           = 6,
        NoMode          = 7,
        Minecraft       = 8,
    };

    /**
     * @brief Converts LotusMode enum to display string.
     * @param mode The mode to convert.
     * @return Human-readable mode name.
     */
    inline std::string modeEnumToString(LotusMode mode) {
        switch (mode) {
            case LotusMode::Off: return "OFF";
            case LotusMode::Uinput: return "Uinput (Slow)";
            case LotusMode::SurroundingText: return "Surrounding Text";
            case LotusMode::Preedit: return "Preedit";
            case LotusMode::Emoji: return "Emoji Picker";
            case LotusMode::Smooth: return "Uinput (Smooth)";
            case LotusMode::Minecraft: return "Minecraft";
            default: return "";
        }
    }

    /**
     * @brief Converts mode string to LotusMode enum.
     * @param mode The string to parse.
     * @return Corresponding LotusMode value.
     */
    inline LotusMode modeStringToEnum(const std::string& mode) {
        static const std::unordered_map<std::string_view, LotusMode> modeMap = {
            {"OFF", LotusMode::Off},
            {"Uinput (Slow)", LotusMode::Uinput},
            {"Surrounding Text", LotusMode::SurroundingText},
            {"Preedit", LotusMode::Preedit},
            {"Emoji Picker", LotusMode::Emoji},
            {"Uinput (Smooth)", LotusMode::Smooth},
            {"Minecraft", LotusMode::Minecraft},
        };
        auto it = modeMap.find(mode);
        return it != modeMap.end() ? it->second : LotusMode::NoMode;
    }

    struct InputMethodConstrain;
    struct InputMethodAnnotation;

    using InputMethodOption = Option<std::string, InputMethodConstrain, DefaultMarshaller<std::string>, InputMethodAnnotation>;

    /**
     * @brief Annotation for string list options in configuration UI.
     */
    struct StringListAnnotation : public EnumAnnotation {
        /**
         * @brief Sets the string list.
         * @param list Vector of strings to set.
         */
        void setList(std::vector<std::string> list) {
            list_ = std::move(list);
        }

        /**
         * @brief Gets the string list.
         * @return Reference to the list.
         */
        const auto& list() {
            return list_;
        }

        /**
         * @brief Dumps description to config.
         * @param config Config to write to.
         */
        void dumpDescription(RawConfig& config) const {
            EnumAnnotation::dumpDescription(config);
            for (size_t i = 0; i < list_.size(); ++i) {
                config.setValueByPath("Enum/" + std::to_string(i), list_[i]);
            }
        }

      protected:
        std::vector<std::string> list_; // NOLINT
    };

    /**
     * @brief Annotation for input method selection with sub-config support.
     */
    struct InputMethodAnnotation : public StringListAnnotation {
        /**
         * @brief Dumps description with sub-config paths.
         * @param config Config to write to.
         */
        void dumpDescription(RawConfig& config) const {
            StringListAnnotation::dumpDescription(config);
            config.setValueByPath("LaunchSubConfig", "True");
        }
    };

    /**
     * @brief Annotation for time format list.
     */
    struct TimeFormatAnnotation : public StringListAnnotation {
        TimeFormatAnnotation() {
            list_ = {"%H:%M", "%H:%M:%S", "%I:%M:%S %p", "%I:%M %p", ""};
        }
    };

    /**
     * @brief Annotation for date format list.
     */
    struct DateFormatAnnotation : public StringListAnnotation {
        DateFormatAnnotation() {
            list_ = {"%d/%m/%Y", "%m/%d/%Y", "%Y-%m-%d", "%d/%m/%y", "%y-%m-%d", ""};
        }
    };

    /**
     * @brief Annotation for mode list with predefined options.
     */
    struct ModeListAnnotation : public StringListAnnotation {
        /**
         * @brief Initializes with default mode list.
         */
        ModeListAnnotation() {
            list_ = {_("Uinput (Smooth)"), _("Uinput (Slow)"), _("Minecraft"), _("Surrounding Text"), _("Preedit"), _("Emoji Picker"), _("OFF")};
        }
    };

    /**
     * @brief Constraint validator for input method options.
     */
    struct InputMethodConstrain {
        using Type = std::string;

        /**
         * @brief Constructs with option pointer.
         * @param option Pointer to input method option.
         */
        InputMethodConstrain(const InputMethodOption* option) : option_(option) {}

        /**
         * @brief Validates if name is in the allowed list.
         * @param name Name to check.
         * @return True if valid.
         */
        bool check(const std::string& name) const {
            const auto& list = option_->annotation().list();
            if (list.empty()) {
                return true;
            }
            return std::find(list.begin(), list.end(), name) != list.end();
        }

        /**
         * @brief Dumps description (no-op).
         * @param config Unused.
         */
        void dumpDescription(RawConfig& /*unused*/) const {}

      private:
        const InputMethodOption* option_;
    };

    FCITX_CONFIGURATION(lotusKeymap, Option<std::string> key{this, "Key", _("Key"), ""}; Option<std::string> value{this, "Value", _("Value"), ""};);

    FCITX_CONFIGURATION(lotusMacroTable,
                        OptionWithAnnotation<std::vector<lotusKeymap>, ListDisplayOptionAnnotation> macros{
                            this, "Macro", _("Macro"), {}, {}, {}, ListDisplayOptionAnnotation("Key")};);

    FCITX_CONFIGURATION(lotusCustomKeymap,
                        OptionWithAnnotation<std::vector<lotusKeymap>, ListDisplayOptionAnnotation> customKeymap{
                            this, "CustomKeymap", _("Custom Keymap"), {}, {}, {}, ListDisplayOptionAnnotation("Key")};);

    FCITX_CONFIGURATION(lotusAppRule, Option<std::string> app{this, "App", _("App"), ""}; Option<int> mode{this, "Mode", _("Mode"), 0};);
    FCITX_CONFIGURATION(lotusAppRules,
                        OptionWithAnnotation<std::vector<lotusAppRule>, ListDisplayOptionAnnotation> rules{
                            this, "Rules", _("Rules"), {}, {}, {}, ListDisplayOptionAnnotation("App")};);

    /**
     * @brief Main configuration structure for Lotus input method.
     */
    FCITX_CONFIGURATION(
        lotusConfig,

        OptionWithAnnotation<std::string, ModeListAnnotation> mode{this, "Mode", _("Mode"), "Uinput (Smooth)", {}, {}, ModeListAnnotation()};
        Option<std::string, InputMethodConstrain, DefaultMarshaller<std::string>, InputMethodAnnotation> inputMethod{
            this, "InputMethod", _("Input Method"), "Telex", InputMethodConstrain(&inputMethod), {}, InputMethodAnnotation()};
        OptionWithAnnotation<std::string, StringListAnnotation> outputCharset{this, "OutputCharset", _("Output Charset"), "Unicode", {}, {}, StringListAnnotation()};
        Option<bool> spellCheck{this, "SpellCheck", _("Enable Spell Check"), true}; Option<bool> enableMacro{this, "EnableMacro", _("Enable Macro"), true};
        Option<bool> capitalizeMacro{this, "CapitalizeMacro", _("Capitalize Macro"), true}; Option<bool> autoCapitalizeAfterPunctuation{
            this, "AutoCapitalizeAfterPunctuation", _("Auto capitalize after sentence-ending punctuation (. ! ? Enter) (experimental)"), false};
        Option<bool> doubleSpaceToPeriod{this, "DoubleSpaceToPeriod", _("Double Space to Period (experimental)"), false};
        Option<bool> w2u{this, "W2U", _("Type w to Produce ư"), true}; Option<bool> autoNonVnRestore{this, "AutoNonVnRestore", _("Auto Restore Keys With Invalid Words"), true};
        Option<bool>                                                                modernStyle{this, "ModernStyle", _("Use oà, uý (Instead Of òa, úy)"), true};
        Option<bool>                                                                freeMarking{this, "FreeMarking", _("Allow Type With More Freedom"), true};
        Option<bool> ddFreeStyle{this, "DdFreeStyle", _("Allow dd To Produce đ When Auto Restore Keys With Invalid Words Is On"), true};
        Option<bool> fixUinputWithAck{this, "FixUinputWithAck", _("Fix Uinput Mode With Ack"), false};
        Option<bool> useLotusIcons{this, "UseLotusIcons", _("Use Lotus Status Icons"), false};
        Option<bool> useBlackDefaultIcons{this, "UseBlackDefaultIcons", _("Use Black Default Icons"), false};
        Option<bool> enableDictionary{this, "EnableDictionary", _("Enable Custom Dictionary"), false};
        Option<bool> enableCustomKeymap{this, "EnableCustomKeymap", _("Enable Custom Keymap"), false};
        Option<bool> showModeSmooth{this, "ShowModeSmooth", _("Show Uinput (Smooth)"), true}; Option<bool> showModeUinput{this, "ShowModeUinput", _("Show Uinput (Slow)"), true};
        Option<bool>                                                                                       showModeMinecraft{this, "ShowModeMinecraft", _("Show Minecraft"), true};
        Option<bool> showModeSurroundingText{this, "ShowModeSurroundingText", _("Show Surrounding Text"), true};
        Option<bool> showModePreedit{this, "ShowModePreedit", _("Show Preedit"), true}; Option<bool> showModeEmoji{this, "ShowModeEmoji", _("Show Emoji Picker"), true};
        Option<bool> showModeOff{this, "ShowModeOff", _("Show OFF"), true}; Option<bool> showModeDefault{this, "ShowModeDefault", _("Show Default Typing"), true};
        OptionWithAnnotation<std::string, TimeFormatAnnotation> timeFormat{this, "TimeFormat", _("Time Format ($TIME in macro)"), "%H:%M", {}, {}, TimeFormatAnnotation()};
        OptionWithAnnotation<std::string, DateFormatAnnotation> dateFormat{this, "DateFormat", _("Date Format ($DATE in macro)"), "%d/%m/%Y", {}, {}, DateFormatAnnotation()};
        SubConfigOption                                         macroEditor{this, "MacroEditor", _("Macro"), "fcitx://config/addon/lotus/lotus-macro"};
        SubConfigOption                                         customKeymap{this, "CustomKeymap", _("Custom Keymap"), "fcitx://config/addon/lotus/custom_keymap"};
        SubConfigOption appRules{this, "AppRules", _("App Rules"), "fcitx://config/addon/lotus/app_rules"}; KeyListOption modeMenuKey{
            this, "ModeMenuKey", _("Mode Menu Hotkey"), {Key("grave")}, KeyListConstrain({KeyConstrainFlag::AllowModifierLess, KeyConstrainFlag::AllowModifierOnly})};);

} // namespace fcitx

#endif
