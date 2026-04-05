# Settings Reference

This document provides a detailed explanation of the options available in the Lotus configuration interface.

## General Settings

| Setting | Meaning | Internal Behavior |
| :--- | :--- | :--- |
| **Input Method** | Select Telex, VNI, or VIQR. | Switches the parser within the Bamboo core (Go). |
| **Input Mode** | Smooth, Surrounding, Preedit, etc. | Changes the commit mechanism (Uinput vs Fcitx API). See [Input Modes](input_modes.en.md). |
| **Free Mark Placement** | Allows placing tone marks anywhere in a word. | Disables tone validity checks in the Bamboo core. |
| **Shorthand** | Enable/disable personal shorthand. | Looks up substitution tables before processing keys (labeled **Enable Macro** in UI). |

## Advanced Settings

| Setting | Meaning | Internal Behavior |
| :--- | :--- | :--- |
| **Auto-Capitalization** | Automatically capitalizes the first letter of a sentence. | Analyzes Surrounding Text for periods/newlines. |
| **Minecraft Mode** | Optimizes Backspace for Minecraft Java. | Modifies timing and Backspace repetition methods. |

## Key Bindings

| Shortcut | Function |
| :--- | :--- |
| **Ctrl + `** | Opens the quick mode switch menu. |
| **Alt + L** | Quickly toggle the input engine ON/OFF. |
| **Shift** | (Optional) Toggle between Vietnamese and English. |

---

*Note: Some settings may require restarting Fcitx5 or Lotus Server to take effect.*
