# fcitx5-lotus System Architecture

This document describes the technical architecture of the Lotus input method, covering the integration of C++, Go, and IPC (Inter-Process Communication) mechanisms.

## 1. Component Overview

Lotus is designed as a hybrid model to leverage C++'s performance for input method framework integration and Go's powerful language processing capabilities.

| Component | Language | Role |
| :--- | :--- | :--- |
| **Fcitx5 Addon** | C++ | Primary component integrated into the Fcitx5 framework, managing state and UI. |
| **Bamboo Core** | Go | Vietnamese processing engine using the [Bamboo](https://github.com/LotusInputMethod/bamboo-core) library. |
| **CGO Wrapper** | C++/Go | Bridge allowing C++ to call processing functions from Go. |
| **Lotus Server** | C++ | A standalone service managing the `/dev/uinput` device for key simulation (Smooth/Slow modes). |

## 2. Key Event Flow

When a user presses a key, the following process occurs:

1. **Fcitx5 Addon**: Receives the key event from the Fcitx5 framework.
2. **Bamboo Core (via CGO)**: The addon sends the key to the Go core to check for tone marks or functional keys.
3. **Logic Processing**:
    - If Vietnamese: The Go core returns the processed text string.
    - If not: The key is returned to the system for normal processing.
4. **Output**: Depending on the input mode:
    - **Surrounding/Preedit**: Uses standard Fcitx5 APIs to delete and insert text.
    - **Smooth/Slow**: Sends commands via Unix Domain Socket to `fcitx5-lotus-server`.
5. **Lotus Server**: Receives the command and writes directly to `/dev/uinput` to generate actual Backspace and character events.

## 3. IPC (Inter-Process Communication)

Lotus utilizes two primary types of communication:

### CGO (C + Go)

Used to embed the Vietnamese processing library directly into the input engine. This ensures maximum processing speed without network or socket latency.

### Unix Domain Sockets

Used between the Addon (Client) and `fcitx5-lotus-server` (Server).

- **Socket Address**: Uses a Linux **abstract socket address** (e.g., `@lotussocket-<username>-kb_socket`). This avoids managing socket files on disk and ensures automatic cleanup when processes terminate.
- **Purpose**: Smooth mode requires write access to `/dev/uinput`. For security, only the Server runs with elevated permissions (or in the `uinput` group), while the Addon runs with standard user permissions.

## 4. Important Directories

- `src/`: Contains the C++ source code for the fcitx5 engine.
- `bamboo/`: Contains the Go code and CGO export files (`bamboo/bamboo-c.go`).
- `server/`: Contains the source code for the uinput server.
- `settings-gui/`: Configuration interface written in Python/PySide6.

---

*This document is maintained to help developers gain a deep understanding of how Lotus works.*
