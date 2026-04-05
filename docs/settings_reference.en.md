# Settings Reference

This document provides a detailed explanation of the options available in the Lotus configuration interface.

## General Settings

| Setting | Meaning | Internal Behavior |
| :--- | :--- | :--- |
| **Input Method** | Select Telex, VNI, or VIQR. | Switches the parser within the Bamboo core (Go). |
| **Input Mode** | Smooth, Surrounding, Preedit, etc. | Changes the commit mechanism (Uinput vs Fcitx API). See [Input Modes](input_modes.md). |
| **Free Mark Placement** | Allows placing tone marks anywhere in a word. | Disables tone validity checks in the Bamboo core. |
| **Shorthand** | Enable/disable personal shorthand. | Looks up substitution tables before processing keys. |

## Advanced Settings

| Setting | Meaning | Internal Behavior |
| :--- | :--- | :--- |
| **Use uinput server** | Enables communication with `fcitx5-lotus-server`. | Opens a Unix Domain Socket to send key commands. |
| **Auto-Capitalization** | Automatically capitalizes the first letter of a sentence. | Analyzes Surrounding Text for periods/newlines. |
| **Show Key Overlay** | Displays the last typed key on screen. | Emits signals to the overlay client via socket. |
| **Minecraft Mode** | Optimizes Backspace for Minecraft Java. | Modifies timing and Backspace repetition methods. |

## Key Bindings

| Shortcut | Function |
| :--- | :--- |
| **Ctrl + `** | Opens the quick mode switch menu. |
| **Nalt + L** | Quickly toggle the input engine ON/OFF. |
| **Shift** | (Optional) Toggle between Vietnamese and English. |

---

*Note: Some settings may require restarting Fcitx5 or Lotus Server to take effect.*
