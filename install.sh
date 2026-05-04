#!/bin/bash

# fcitx5-lotus "Universal" Installation Script
# This script automates dependency installation, build, and setup.

set -e

# Lotus Color Scheme (Pinks/Magentas)
LOTUS_PINK='\033[38;5;213m'
LOTUS_DARK_PINK='\033[38;5;197m'
LOTUS_PURPLE='\033[38;5;171m'
NC='\033[0m' # No Color
BOLD='\033[1m'

echo -e "${BOLD}${LOTUS_PINK}==> fcitx5-lotus Universal Installer 🪷${NC}"

# Root check
if [ "$(id -u)" = "0" ]; then
    echo -e "${LOTUS_DARK_PINK}Error:${NC} Do not run as root. The script will use sudo when needed."
    exit 1
fi

# Detect Distribution
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    LIKE=$ID_LIKE
else
    OS=$(uname -s)
    NAME=$OS
fi

echo -e "${LOTUS_PINK}==>${NC} Detected System: ${BOLD}$NAME${NC}"

# Function to check if a package is installed
is_installed() {
    case "$OS" in
        debian|ubuntu|pop|linuxmint)
            dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "ok installed"
            ;;
        fedora|nobara|rhel|opensuse*|suse)
            rpm -q --whatprovides "$1" &> /dev/null
            ;;
        arch|manjaro|endeavouros)
            pacman -Qq "$1" &> /dev/null
            ;;
        void)
            xbps-query -l "$1" &> /dev/null
            ;;
        *)
            return 1 # Assume not installed for unknown distros
            ;;
    esac
}

# Dependency Mapping
# Format: "generic_name:package_name"
DEPS_DEBIAN=("cmake:cmake" "extra-cmake-modules:extra-cmake-modules" "fcitx5-dev:libfcitx5core-dev" "fcitx5-config-dev:libfcitx5config-dev" "fcitx5-utils-dev:libfcitx5utils-dev" "libinput-dev:libinput-dev" "libudev-dev:libudev-dev" "g++:g++" "go:golang" "git:git" "icon-theme:hicolor-icon-theme" "pkg-config:pkg-config" "x11-dev:libx11-dev" "fcitx5-modules-dev:fcitx5-modules-dev" "python3:python3")
DEPS_FEDORA=("cmake:cmake" "extra-cmake-modules:extra-cmake-modules" "fcitx5-devel:fcitx5-devel" "libinput-devel:libinput-devel" "gcc-c++:gcc-c++" "go:golang" "git:git" "icon-theme:hicolor-icon-theme" "systemd-devel:systemd-devel" "libX11-devel:libX11-devel" "python3:python3")
DEPS_OPENSUSE=("cmake:cmake" "extra-cmake-modules:extra-cmake-modules" "fcitx5-devel:fcitx5-devel" "libinput-devel:libinput-devel" "gcc-c++:gcc-c++" "go:go" "git:git" "icon-theme:hicolor-icon-theme" "libX11-devel:libX11-devel" "udev:udev" "python3:python3")
DEPS_ARCH=("cmake:cmake" "extra-cmake-modules:extra-cmake-modules" "fcitx5:fcitx5" "libinput:libinput" "gcc:gcc" "go:go" "git:git" "icon-theme:hicolor-icon-theme" "libx11:libx11" "python:python")
DEPS_VOID=("cmake:cmake" "extra-cmake-modules:extra-cmake-modules" "fcitx5-devel:libfcitx5-devel" "libinput-devel:libinput-devel" "udev-devel:eudev-libudev-devel" "gcc:gcc" "go:go" "git:git" "gettext:gettext-devel" "pkg-config:pkg-config" "icon-theme:hicolor-icon-theme" "libx11-devel:libx11-devel" "python3:python3")

check_and_install_deps() {
    local missing_deps=()
    local deps_list=()

    case "$OS" in
        debian|ubuntu|pop|linuxmint) deps_list=("${DEPS_DEBIAN[@]}") ;;
        fedora|nobara|rhel)          deps_list=("${DEPS_FEDORA[@]}") ;;
        opensuse*|suse)             deps_list=("${DEPS_OPENSUSE[@]}") ;;
        arch|manjaro|endeavouros)    deps_list=("${DEPS_ARCH[@]}") ;;
        void)                        deps_list=("${DEPS_VOID[@]}") ;;
        *) return 0 ;;
    esac

    for entry in "${deps_list[@]}"; do
        local pkg="${entry#*:}"
        if ! is_installed "$pkg"; then
            missing_deps+=("$pkg")
        fi
    done

    if [ ${#missing_deps[@]} -eq 0 ]; then
        echo -e "${LOTUS_PINK}==>${NC} All build dependencies are already installed."
        return 0
    fi

    echo -e "${LOTUS_PINK}==>${NC} Missing dependencies: ${BOLD}${missing_deps[*]}${NC}"
    read -p "Do you want to install them now? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        case "$OS" in
            debian|ubuntu|pop|linuxmint) sudo apt-get update && sudo apt-get install -y "${missing_deps[@]}" ;;
            fedora|nobara|rhel)          sudo dnf install -y "${missing_deps[@]}" ;;
            opensuse*|suse)             sudo zypper install -y "${missing_deps[@]}" ;;
            arch|manjaro|endeavouros)    sudo pacman -S --needed --noconfirm "${missing_deps[@]}" ;;
            void)                        sudo xbps-install -Sy "${missing_deps[@]}" ;;
        esac
    else
        echo -e "${LOTUS_DARK_PINK}Warning:${NC} Some dependencies are missing. Build might fail."
    fi
}

# Run dependency check
check_and_install_deps

# Initialize submodules
if [ ! -f "bamboo/bamboo-core/bamboo.go" ]; then
    echo -e "${LOTUS_PINK}==>${NC} Initializing submodules..."
    git submodule update --init --recursive
fi

# Create build directory
echo -e "${LOTUS_PINK}==>${NC} Creating build directory..."
mkdir -p build
cd build

# Detect LIBDIR
LIBDIR="/usr/lib"
if [ -d "/usr/lib64" ] && [[ "$OS" =~ (fedora|rhel|opensuse|arch) ]]; then
    LIBDIR="/usr/lib64"
fi

# Configure
echo -e "${LOTUS_PINK}==>${NC} Configuring with CMake..."
EXTRA_ARGS=""
if [ "$OS" == "void" ]; then
    EXTRA_ARGS="-DINSTALL_RUNIT=ON -DRUNIT_SV_DIR=/etc/sv"
fi

cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR="$LIBDIR" $EXTRA_ARGS ..

# Build
echo -e "${LOTUS_PINK}==>${NC} Building..."
make -j$(nproc)

echo -e "${LOTUS_PINK}==>${NC} Build successful!"

# Install
read -p "Do you want to install now? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    sudo make install
    
    # Post-install: Setup uinput group and user if needed
    echo -e "${LOTUS_PINK}==>${NC} Running post-installation setup..."
    sudo groupadd -f input || true
    if ! id "uinput_proxy" &>/dev/null; then
        sudo useradd -M -g input -s /usr/bin/nologin -d / uinput_proxy || true
    fi
    
    # Reload udev rules
    if [ -d /etc/udev/rules.d ]; then
        echo -e "${LOTUS_PINK}==>${NC} Reloading udev rules..."
        sudo udevadm control --reload-rules || true
        sudo udevadm trigger || true
    fi

    echo -e "${LOTUS_PINK}==>${NC} ${BOLD}Installation complete! 🌸${NC}"

    # Suggest restart
    echo -e "${LOTUS_PURPLE}==>${NC} To apply changes, Fcitx5 needs to be restarted."
    read -p "Do you want to restart Fcitx5 now? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo -e "${LOTUS_PINK}==>${NC} Restarting Fcitx5..."
        if command -v fcitx5 &> /dev/null; then
            # Use --replace (-r) to safely replace the existing instance
            fcitx5 -r -d > /dev/null 2>&1 &
            echo -e "${LOTUS_PINK}==>${NC} Fcitx5 restarted!"
        else
            echo -e "${LOTUS_DARK_PINK}Note:${NC} fcitx5 command not found. Please restart it manually."
        fi
    fi
else
    echo -e "${LOTUS_PINK}==>${NC} Installation skipped. You can find the binaries in the 'build' directory."
fi
