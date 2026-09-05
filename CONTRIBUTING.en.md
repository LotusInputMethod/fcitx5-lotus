[English](CONTRIBUTING.en.md) | [Tiếng Việt](CONTRIBUTING.md)

# Contributing to fcitx5-lotus

Thank you for your interest in contributing to the fcitx5-lotus project! This document guides you on how to participate in project development.

## 📋 Table of Contents

- [Getting Started](#getting-started)
- [Contribution Workflow](#contribution-workflow)
- [Code of Conduct](#code-of-conduct)
- [Code Style Rules](#code-style-rules)
- [Pull Request Process](#pull-request-process)
- [Important Notes on Branching](#important-notes-on-branching)
- [Bug Reporting](#bug-reporting)
- [Feature Requests](#feature-requests)

## Getting Started

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

# Initialize submodules
git submodule update --init --recursive

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Contribution Workflow

### 1. Fork repository

Fork this repository on GitHub and clone your fork to your machine.

```bash
git clone https://github.com/yourusername/fcitx5-lotus.git
cd fcitx5-lotus
git remote add upstream https://github.com/LotusInputMethod/fcitx5-lotus.git
```

### 2. Create a new branch

Always create a new branch for each feature or bug fix:

```bash
git checkout dev
git pull upstream dev
git checkout -b feature/feature-name
# or
git checkout -b fix/bug-name
```

### 3. Make changes

- Write clean, readable code
- Follow code style rules (see below)
- Update documentation if needed

### 4. Commit changes

Use clear and descriptive commit messages:

```
feat: add emoji support

- Add emoji_data.h
- Update generate_emoji_header.py
```

## Code of Conduct

All members contributing to this project must adhere to the [Contributor Code of Conduct](CODE_OF_CONDUCT.en.md) to build an open, friendly, and healthy community.

## Code Style Rules

- **C/C++**: Follow the [`.clang-format`](.clang-format) file in the repository.
- **Python (`settings-gui`)**: Follow `ruff` linting and formatting standards (`ruff check` and `ruff format`).
- Encouraged to create a `pre-commit` hook to automatically format code before commit by creating `.git/hooks/pre-commit` with the following content:

```bash
#!/bin/bash
# Format C/C++
CPP_FILES=$(git diff --cached --name-only --diff-filter=ACMR \vert{} grep -E '\.(cpp\vert{}h)$')
if [ -n "$CPP_FILES" ]; then
    for file in $CPP_FILES; do
        clang-format -i "$file"
        git add "$file"
    done
fi

# Lint & Format Python
PY_FILES=$(git diff --cached --name-only --diff-filter=ACMR \vert{} grep -E '^settings-gui/.*\.py$')
if [ -n "$PY_FILES" ]; then
    ruff check --fix settings-gui
    ruff format settings-gui
    for file in $PY_FILES; do
        git add "$file"
    done
fi
```

Then run the command: `chmod +x .git/hooks/pre-commit`

## Pull Request Process

### 1. Ensure clean code with `clang-format` and `ruff`

```bash
# C/C++
clang-format -i src/*.cpp src/*.h

# Python
ruff check --fix settings-gui
ruff format settings-gui
```

### 2. Run tests

```bash
# Build C++ code
cd build
cmake .. && make
```

### 3. Rebase with the dev branch

```bash
git checkout dev
git pull upstream dev
git checkout feature/feature-name
git rebase dev
```

### 4. Push and create PR

```bash
git push origin feature/feature-name
```

Create a Pull Request on GitHub with:

- Clear descriptive title
- Detailed description of changes
- Link to related issue (if any)
- Screenshots for UI changes

## Important Notes on Branching

### IMPORTANT: ALL PRs MERGE INTO THE DEV BRANCH

**NEVER create a Pull Request into the `main` branch**

- The `main` branch only contains stable releases and is only merged by the maintainer
- All Pull Requests must target the `dev` branch
- After passing all CI/CD tests and being reviewed by the maintainer, code will be merged into `dev`
- When eligible, code will be merged from `dev` to `main` by the maintainer to bump the version

### Branch Structure

```
main    ← Stable release (only maintainer merges)
  ↑
dev     ← Main development branch (all PRs merge here)
  ↑
feature/*, fix/*, hotfix/*  ← Personal branch for each PR
```

### Merge Process

1. Developer creates PR to `dev`
2. Code review by maintainer
3. Merge into `dev`
4. Test on `dev`
5. When stable → merge `dev` → `main` (by maintainer)

## Bug Reporting

When reporting a bug, please provide:

- fcitx5-lotus version
- Operating system and version
- Steps to reproduce the bug
- Log or screenshot (if any) (Log via `fcitx5-diagnose` command)
- Environment (desktop environment, terminal, etc.)

## Feature Requests

Before proposing a new feature:

- Check if the feature already exists
- Search for similar issues
- Clearly describe the feature and use case
- Explain why this feature is necessary

## License

By contributing, you agree that your code will be licensed under the same license as the project (GPL-3.0-or-later).

---

Thank you for contributing!
