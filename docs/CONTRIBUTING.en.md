[English](CONTRIBUTING.en.md) | [Tiếng Việt](CONTRIBUTING.md)

# Contributing to fcitx5-lotus

Thank you for your interest in contributing to the fcitx5-lotus project! This document guides you on how to participate in project development.

## 📋 Table of Contents

- [📖 Deep Technical Documentation](#deep-technical-documentation)
- [⚙️ Getting Started](#getting-started)
  - [System Requirements](#system-requirements)
  - [Installation and Build](#installation-and-build)
  - [Testing Changes](#testing-changes)
- [🤝 Contribution Workflow](#contribution-workflow)
  - [1. Fork and Branch](#1-fork-and-branch)
  - [2. Make Changes & Commit](#2-make-changes--commit)
  - [3. Pull Request Process](#3-pull-request-process)
  - [Important Notes on Branching](#important-notes-on-branching)
- [📝 Rules & Standards](#rules--standards)
- [🐛 Bug Reporting & Feature Requests](#bug-reporting--feature-requests)
- [⚖️ License](#license)

---

## 📖 Deep Technical Documentation

Before you start making changes, please refer to the following documents to understand the Lotus architecture:

- **[Architecture Overview](ARCHITECTURE.en.md)**: Explains the C++/Go hybrid model, IPC, and server-client design.
- **[Detailed Input Modes](input_modes.en.md)**: Technical details about Smooth, Slow, Surrounding, Preedit...
- **[Settings Reference](settings_reference.en.md)**: Exhaustive list of configuration parameters within the engine.

---

## ⚙️ Getting Started

### System Requirements

- GCC or Clang with C++17 support
- CMake >= 3.16
- Go 1.20+ (for bamboo engine)
- Fcitx5 development headers
- Git

### Installation and Build

```bash
# Clone repository
git clone https://github.com/LotusInputMethod/fcitx5-lotus.git
cd fcitx5-lotus

# Initialize submodules (critical for bamboo core)
git submodule update --init --recursive

# Build the entire project
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=/usr/lib ..
make -j$(nproc)
sudo make install
```

#### Component-based Build (For Debugging)

If you are only editing the configuration UI:

- Run `python3 settings-gui/main.py` directly (requires `PySide6`).

If you are editing the Bamboo core (Go):

- Run `go build` within the `bamboo/` directory.

### Testing Changes

1. **Restart Fcitx5**: After `make install`, run `fcitx5 -r` to apply changes.
2. **Check Logs**: Use `journalctl -f` or check Fcitx5 logs to see DEBUG messages from Lotus.
3. **Uinput Server**: Ensure `fcitx5-lotus-server` is running if you are testing Smooth/Slow modes.

---

## 🤝 Contribution Workflow

### 1. Fork and Branch

1. **Fork** this repository on GitHub and clone your fork to your machine:

    ```bash
    git clone https://github.com/yourusername/fcitx5-lotus.git
    cd fcitx5-lotus
    git remote add upstream https://github.com/LotusInputMethod/fcitx5-lotus.git
    ```

2. **Create a new branch** from the `dev` branch:

    ```bash
    git checkout dev
    git pull upstream dev
    git checkout -b feature/feature-name # or fix/bug-name
    ```

### 2. Make Changes & Commit

- Write clean, readable code and follow the code style rules.
- Update relevant documentation if there are changes to features or configuration.
- Use clear and descriptive commit messages (`feat:`, `fix:`, `docs:`, ...):

    ```bash
    git commit -m "feat: add emoji support"
    ```

### 3. Pull Request Process

1. **Ensure clean code**: Use `clang-format` and double-check logic.
2. **Run tests**: Rebuild the code and verify stability.

    ```bash
    cd build && cmake .. && make
    ```

3. **Rebase with the dev branch**:

    ```bash
    git checkout dev && git pull upstream dev
    git checkout feature/feature-name
    git rebase dev
    ```

4. **Push and create PR**:

    ```bash
    git push origin feature/feature-name
    ```

    Create a PR on GitHub targeting the **`dev`** branch of upstream.

### Important Notes on Branching

#### IMPORTANT: ALL PRs MERGE INTO THE DEV BRANCH

**NEVER create a Pull Request into the `main` branch**

- The `main` branch only contains stable releases.
- All Pull Requests must target the `dev` branch.
- After passing all CI/CD tests and being reviewed by the maintainer, code will be merged into `dev`.
- When eligible, code will be merged from `dev` to `main` by the maintainer to bump the version.

#### Branch Structure

```text
main    ← Stable release (only maintainer merges)
  ↑
dev     ← Main development branch (all PRs merge here)
  ↑
feature/*, fix/*, hotfix/*  ← Personal branch for each PR
```

#### Merge Process

1. Developer creates PR to `dev`.
2. Code review by maintainer.
3. Merge into `dev`.
4. Test on `dev`.
5. When stable → merge `dev` → `main` (by maintainer).

---

## 📝 Rules & Standards

### Code of Conduct

All contributors must adhere to the [Contributor Code of Conduct](CODE_OF_CONDUCT.en.md) to build a healthy community.

### Code Style Rules

- Follow the [`.clang-format`](../.clang-format) file.
- Encouraged to create a hook for pre-commit to automatically format code before commit by creating a file `.git/hooks/pre-commit` with the following content:

```bash
#!/bin/bash
FILES=$(git diff --cached --name-only --diff-filter=ACMR | grep -E '\.(cpp|h)$')

if [ -n "$FILES" ]; then
    for file in $FILES; do
        clang-format -i "$file"
        git add "$file"
    done
fi
```

Then run the command: `chmod +x .git/hooks/pre-commit`

---

## 🐛 Bug Reporting & Feature Requests

### Bug Reporting

Please provide: version, OS, reproduction steps, logs (`fcitx5-diagnose`), and screenshots.

### Feature Requests

Clearly describe the feature, use case, and why it is necessary. Check if a similar request already exists.

---

## ⚖️ License

By contributing, you agree that your code will be licensed under the same license as the project (**GPL-3.0-or-later**).

---

Thank you for contributing!
