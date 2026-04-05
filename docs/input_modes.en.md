# Detailed Input Modes

- **[Settings Reference](settings_reference.en.md)**: Detailed explanation of options in the configuration UI.

Lotus provides several input modes to ensure compatibility across different Linux applications. Each mode handles text processing differently.

## 1. Smooth Mode - Recommended

- **Technique**: Uses `uinput` to send actual Backspace and character keycodes at the kernel level.
- **Pros**: Extremely smooth performance, no character duplication or lost keys in complex apps (Games, Electron, Terminals).
- **Requirements**: Requires `fcitx5-lotus-server` to be running with `/dev/uinput` access.

## 2. Slow Mode

- **Technique**: Similar to Smooth Mode but introduces small delays between Backspace and Commit commands.
- **Use Case**: Designed for legacy applications or low-end systems where Smooth Mode is too fast for the app to process deletions correctly.

## 3. Surrounding Mode

- **Technique**: Uses the standard Fcitx5 `GetSurroundingText` protocol. The engine queries the application for text around the cursor to determine how many characters to delete.
- **Pros**: Does not require root or uinput permissions.
- **Cons**: Only works in applications with robust Surrounding Text support (Modern GTK/Qt, Chrome). Fails in games and some terminals.

## 4. Preedit Mode

- **Technique**: Displays the text in a temporary buffer (underlined) before it is committed to the application via Enter or Space.
- **Use Case**: Used when other modes fail, or when the user wants precise control over each word before committing.

## 5. Minecraft Mode

- **Technique**: A specialized variant of Smooth Mode optimized for Minecraft (Java Edition) to prevent input drops during chat or command entry.

## 6. Emoji Mode

- **Technique**: Shorthand mode for quick emoji insertion.

---

### Quick Comparison

| Mode | Performance | Compatibility | Requirements |
| :--- | :--- | :--- | :--- |
| **Smooth** | Very High | Broad (Games/Apps) | uinput-server |
| **Surrounding** | High | Modern Apps (GTK/Qt) | None |
| **Preedit** | Medium | All | None |
| **Slow** | Low | Legacy Apps | uinput-server |
