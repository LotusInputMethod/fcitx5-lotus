[English](README.en.md) | [Tiếng Việt](README.md)

<a id="readme-top"></a>

<div align="center">
  <a href="https://lotusinputmethod.github.io/">
    <img src="data/fcitx-lotus-README.svg" alt="Logo" width="80" height="80">
  </a>

  <h2 align="center">Fcitx5 Lotus</h2>

  <p align="center">
    <b>A simple, high-performance Vietnamese input method for Linux</b>
    <br />
    <a href="https://lotusinputmethod.github.io/"><strong>Discover the homepage »</strong></a>
    <br />
    <br />
    <a href="https://github.com/LotusInputMethod/fcitx5-lotus/releases">
      <img src="https://img.shields.io/github/v/release/LotusInputMethod/fcitx5-lotus?style=flat&color=success" alt="Release">
    </a>
    <a href="https://github.com/LotusInputMethod/fcitx5-lotus/blob/main/LICENSE">
      <img src="https://img.shields.io/github/license/LotusInputMethod/fcitx5-lotus?style=flat&color=blue" alt="License">
    </a>
    <a href="https://lotusinputmethod.github.io/">
      <img src="https://img.shields.io/badge/website-live-brightgreen?style=flat&logo=firefox&logoColor=white" alt="Website">
    </a>
    <a href="https://github.com/LotusInputMethod/fcitx5-lotus/stargazers">
      <img src="https://img.shields.io/github/stars/LotusInputMethod/fcitx5-lotus?style=flat&color=yellow" alt="Stars">
    </a>
    <a href="https://github.com/LotusInputMethod/fcitx5-lotus/network/members">
      <img src="https://img.shields.io/github/forks/LotusInputMethod/fcitx5-lotus?style=flat&color=orange" alt="Forks">
    </a>
    <a href="https://github.com/LotusInputMethod/fcitx5-lotus/issues">
      <img src="https://img.shields.io/github/issues/LotusInputMethod/fcitx5-lotus?style=flat&color=red" alt="Issues">
    </a>
    <a href="#contributors-">
      <img src="https://img.shields.io/badge/all_contributors-5-orange.svg?style=flat-square" alt="All Contributors">
    </a>
    <a href="https://deepwiki.com/LotusInputMethod/fcitx5-lotus"><img src="https://deepwiki.com/badge.svg" alt="Ask DeepWiki"></a>
  </p>

  <p align="center">
    <a href="#installation"><strong>Installation »</strong></a>
    <br />
    <br />
    <a href="https://github.com/LotusInputMethod/fcitx5-lotus/issues/new?template=bug_report.yml">Report Bug</a>
    &middot;
    <a href="https://github.com/LotusInputMethod/fcitx5-lotus/issues/new?template=feature_request.yml">Request Feature</a>
  </p>
</div>

<br />

This project is an optimized fork of [VMK input method](https://github.com/thanhpy2009/VMK). Sincere thanks to the author Thanh for laying the foundation for this input method.

<details>
  <summary><b>Table of Contents</b></summary>
  <ol>
    <li><a href="#installation">Installation</a></li>
    <li><a href="#enable-input-method">Enable Input Method</a></li>
    <li><a href="#usage-guide">Usage Guide</a></li>
    <li><a href="#uninstallation">Uninstallation</a></li>
    <li><a href="#contributing">Contributing</a></li>
    <li><a href="#license">License</a></li>
  </ol>
</details>

---

<a id="installation"></a>

## 📦 Installation

<sub>💡 Click beside the badge to expand the instructions.</sub>

<details>
<summary><a href="#installation"><img src="https://img.shields.io/badge/Arch_Linux-1793D1?style=for-the-badge&logo=arch-linux&logoColor=white" alt="Arch Linux" height="25"></a></summary>
<br>

Currently, AUR has 3 installation packages to choose from:

| Package            | Description                           |
| ------------------ | ------------------------------------- |
| `fcitx5-lotus`     | Build from stable release source code |
| `fcitx5-lotus-bin` | Use pre-built binary                  |
| `fcitx5-lotus-git` | Build from the latest commit list     |

Install with `yay`:

```bash
# Syntax: yay -S <package-name>
yay -S fcitx5-lotus
```

Or `paru`:

```bash
# Syntax: paru -S <package-name>
paru -S fcitx5-lotus
```

</details>

<details>
<summary><a href="#installation"><img src="https://img.shields.io/badge/Debian-D70A53?style=for-the-badge&logo=debian&logoColor=white" alt="Debian" height="25"></a></summary>
<br>

```bash
# Auto-detect codename and install
CODENAME=$(grep '^VERSION_CODENAME=' /etc/os-release | cut -d'=' -f2)
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://fcitx5-lotus.pages.dev/pubkey.gpg | sudo gpg --dearmor -o /etc/apt/keyrings/fcitx5-lotus.gpg
echo "deb [signed-by=/etc/apt/keyrings/fcitx5-lotus.gpg] https://fcitx5-lotus.pages.dev/apt/$CODENAME $CODENAME main" | sudo tee /etc/apt/sources.list.d/fcitx5-lotus.list
sudo apt update && sudo apt install fcitx5-lotus
```

</details>

<details>
<summary><a href="#installation"><img src="https://img.shields.io/badge/Ubuntu-E95420?style=for-the-badge&logo=ubuntu&logoColor=white" alt="Ubuntu" height="25"></a></summary>
<br>

```bash
# Auto-detect codename and install
CODENAME=$(grep '^UBUNTU_CODENAME=' /etc/os-release | cut -d'=' -f2)
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://fcitx5-lotus.pages.dev/pubkey.gpg | sudo gpg --dearmor -o /etc/apt/keyrings/fcitx5-lotus.gpg
echo "deb [signed-by=/etc/apt/keyrings/fcitx5-lotus.gpg] https://fcitx5-lotus.pages.dev/apt/$CODENAME $CODENAME main" | sudo tee /etc/apt/sources.list.d/fcitx5-lotus.list
sudo apt update && sudo apt install fcitx5-lotus
```

</details>

<details>
<summary><a href="#installation"><img src="https://img.shields.io/badge/Fedora-51A2DA?style=for-the-badge&logo=fedora&logoColor=white" alt="Fedora" height="25"></a></summary>
<br>

```bash
# Auto-detect version and install
RELEASEVER=$(grep '^VERSION_ID=' /etc/os-release | cut -d'=' -f2)
sudo rpm --import https://fcitx5-lotus.pages.dev/pubkey.gpg
sudo dnf config-manager addrepo --from-repofile=https://fcitx5-lotus.pages.dev/rpm/fedora/fcitx5-lotus-$RELEASEVER.repo
sudo dnf install fcitx5-lotus
```

</details>

<details>
<summary><a href="#installation"><img src="https://img.shields.io/badge/openSUSE-73BA42?style=for-the-badge&logo=opensuse&logoColor=white" alt="openSUSE" height="25"></a></summary>
<br>

```bash
# Add repo and install (Tumbleweed)
sudo rpm --import https://fcitx5-lotus.pages.dev/pubkey.gpg
sudo zypper addrepo https://fcitx5-lotus.pages.dev/rpm/opensuse/fcitx5-lotus-tumbleweed.repo
sudo zypper refresh
sudo zypper install fcitx5-lotus
```

</details>

<details>
<summary><a href="#installation"><img src="https://img.shields.io/badge/NixOS-5277C3?style=for-the-badge&logo=nixos&logoColor=white" alt="NixOS" height="25"></a></summary>
<br>

Add fcitx5-lotus input to `flake.nix`:

```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    fcitx5-lotus = {
      url = "github:LotusInputMethod/fcitx5-lotus";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = {
    self,
  ...
}
```

Enable fcitx5-lotus service in `configuration.nix`:

```nix
{
  inputs,
  ...
}: {
  imports = [
    inputs.fcitx5-lotus.nixosModules.fcitx5-lotus
  ];

  services.fcitx5-lotus = {
    enable = true;
    user = "your_username"; # Change to your username
  };
}
```

Rebuild the system to install.

</details>

<details>
<summary><a href="#installation"><img src="https://img.shields.io/badge/GitHub_Releases-181717?style=for-the-badge&logo=github&logoColor=white" alt="GitHub Releases" height="25"></a></summary>
<br>

Download `.deb` or `.rpm` files directly from [GitHub Releases](https://github.com/LotusInputMethod/fcitx5-lotus/releases/latest):

```bash
# Debian/Ubuntu
sudo dpkg -i fcitx5-lotus_*.deb
```

```bash
# Fedora / openSUSE
sudo rpm -i fcitx5-lotus-*.rpm
```

</details>

<details>
<summary><a href="#installation"><img src="https://img.shields.io/badge/Source-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" height="25"></a></summary>
<br>

> **IMPORTANT RECOMMENDATION:**
>
> Please **DO NOT** use this method if your distro is already supported via **Cloudflare Pages**.
>
> Manual compilation requires a good understanding of the system directory structure. If you encounter "Not Available" errors or missing libraries when installing this way on popular distros (Ubuntu/Fedora...), please return to using Cloudflare Pages for stability and automatic updates.

### System Requirements

- **Debian/Ubuntu**

```bash
sudo apt-get install cmake extra-cmake-modules libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev libinput-dev libudev-dev g++ golang hicolor-icon-theme pkg-config libx11-dev libfcitx5-qt6-dev qt6-base-dev fcitx5-modules-dev
```

- **Fedora/RHEL**

```bash
sudo dnf install cmake extra-cmake-modules fcitx5-devel libinput-devel libudev-devel gcc-c++ golang hicolor-icon-theme systemd-devel libX11-devel fcitx5-qt-devel
```

- **openSUSE**

```bash
sudo zypper install cmake extra-cmake-modules fcitx5-devel libinput-devel systemd-devel gcc-c++ go hicolor-icon-theme systemd-devel libX11-devel udev fcitx5-qt-devel
```

### Compilation and Installation

```bash
# Clone repository
git clone https://github.com/LotusInputMethod/fcitx5-lotus.git
cd fcitx5-lotus

# Compile
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=/usr/lib . # LIBDIR will vary depending on the distro
make

# Install (root privileges required)
sudo make install

# Or install to a custom directory
sudo make install PREFIX=/usr/local
```

</details>

---

<a id="enable-input-method"></a>

## ⚙️ Enable Input Method

After installation, you need to follow these steps to enable the fcitx5-lotus input method:

> **💡 Automated:** If you install via **Debian/Ubuntu** (`.deb`), the post-install script will automatically perform steps 1–3 below. After installation, you only need to proceed from step 4.

### 1. Enable Lotus Server

The server helps the input method interact better with the system (especially sending backspace and fixing errors).

<details>
<summary><b>Bash / Zsh</b></summary>

```bash
# Enable and start the service (automatically fixes missing systemd user errors if any)
sudo systemctl enable --now fcitx5-lotus-server@$(whoami).service || \
(sudo systemd-sysusers && sudo systemctl enable --now fcitx5-lotus-server@$(whoami).service)
```

</details>

<details>
<summary><b>Fish shell</b></summary>

```fish
# Enable and start the service (automatically fixes missing systemd user errors if any)
sudo systemctl enable --now fcitx5-lotus-server@(whoami).service; or begin
    sudo systemd-sysusers; and sudo systemctl enable --now fcitx5-lotus-server@(whoami).service
end
```

</details>

<p><b>Check status (if you see <span style="color: green;">active (running)</span>, it's OK):</b></p>

```bash
systemctl status fcitx5-lotus-server@$(whoami).service
```

### 2. Set Environment Variables

The input method will not work without these variables.

<details open>
<summary><b>Bash</b></summary>

```bash
# Add the configuration to ~/.bash_profile
cat <<EOF >> ~/.bash_profile
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
export SDL_IM_MODULE=fcitx
export GLFW_IM_MODULE=ibus
EOF
```

</details>

<details open>
<summary><b>Zsh</b></summary>

```bash
# Add the configuration to ~/.zprofile
cat <<EOF >> ~/.zprofile
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
export SDL_IM_MODULE=fcitx
export GLFW_IM_MODULE=ibus
EOF
```

</details>

<details>
<summary><b>Fish shell</b></summary>

```fish
# Add configuration to ~/.config/fish/config.fish
echo 'if status is-login
    set -Ux GTK_IM_MODULE fcitx
    set -Ux QT_IM_MODULE fcitx
    set -Ux XMODIFIERS "@im=fcitx"
    set -gx SDL_IM_MODULE fcitx
    set -gx GLFW_IM_MODULE ibus
end' >> ~/.config/fish/config.fish
```

</details>

Log out and log in to apply changes.

<details>
<summary><b>If you still cannot type after logging out</b></summary>
<br>

If the configuration in `~/.bash_profile`, `~/.zprofile` or `.config/fish/config.fish` doesn't work, you can try setting it in `/etc/environment` to apply to the entire system:

<details open>
<summary><b>Bash/Zsh</b></summary>

```bash
cat <<EOF | sudo tee -a /etc/environment
GTK_IM_MODULE=fcitx
QT_IM_MODULE=fcitx
XMODIFIERS=@im=fcitx
SDL_IM_MODULE=fcitx
GLFW_IM_MODULE=ibus
EOF
```

</details>

<details open>
<summary><b>Fish shell</b></summary>

```fish
echo "GTK_IM_MODULE=fcitx
QT_IM_MODULE=fcitx
XMODIFIERS=@im=fcitx
SDL_IM_MODULE=fcitx
GLFW_IM_MODULE=ibus" | sudo tee -a /etc/environment
```

</details>

> **Note:** A system restart is required after setup.

</details>

### 3. Turn off old input method (IBus)

If your machine is using IBus, turn it off before switching to Fcitx5 to avoid conflicts.

```bash
# Turn off IBus
killall ibus-daemon || ibus exit
```

> **Note:** Turn off IBus autostart (usually `ibus-daemon` or `ibus`) to avoid conflicts. It's best to uninstall IBus if not in use.

### 4. Add Fcitx5 to Autostart

| DE / WM        | Detailed Instructions                                                                                                        |
| :------------- | :--------------------------------------------------------------------------------------------------------------------------- |
| **GNOME**      | _GNOME Tweaks_ → _Startup Applications_ → Add → `Fcitx 5`                                                                    |
| **KDE Plasma** | _System Settings_ → _Autostart_ → Add... → Add Application... → `Fcitx 5`                                                    |
| **Xfce**       | _Settings_ → _Session and Startup_ → _Application Autostart_ → Add → `Fcitx 5`                                               |
| **Cinnamon**   | _System Settings_ → _Startup Applications_ → `+` → Choose application → `Fcitx 5`                                            |
| **MATE**       | _Control Center_ → _Startup Applications_ → Add (Name: `Fcitx 5`, Command: `fcitx5`)                                         |
| **Pantheon**   | _System Settings_ → _Applications_ → _Startup_ → _Add Startup App..._ → `Fcitx 5`                                            |
| **Budgie**     | _Budgie Desktop Settings_ → _Autostart_ → `+` → Add application → `Fcitx 5`                                                  |
| **LXQt**       | _LXQt Configuration Center_ → _Session Settings_ → _Autostart_ → _LXQt Autostart_ → Add (Name: `Fcitx 5`, Command: `fcitx5`) |
| **COSMIC**     | _COSMIC Settings_ → _Applications_ → _Startup Applications_ → Add app → `Fcitx 5`                                            |
| **i3 / Sway**  | Add `exec --no-startup-id fcitx5 -d` to the config file (`~/.config/i3/config` or `~/.config/sway/config`)                   |
| **Hyprland**   | Add `exec-once = fcitx5 -d` to `~/.config/hypr/hyprland.conf`                                                                |

</details>

### 5. Configure Fcitx5

After logging out and logging in again:

1. Open **Fcitx5 Configuration** (find in application menu or run `fcitx5-configtool`).
2. Find **Lotus** in the right column.
3. Click the **<** arrow to add it to the left column.
4. Apply.

<details>
  <summary><b>Additional configuration for Wayland (KDE, Hyprland Chromium-based, Electron)</b></summary>

- **KDE Plasma:** _System Settings_ → _Keyboard_ → _Virtual Keyboard_ → Select **Fcitx 5**.
- **Hyprland:** Add the following line to `~/.config/hypr/hyprland.conf`:

```ini
permission = fcitx5-lotus-server, keyboard, allow
```

- **Chromium-based/Electron:** Add the following flags to the application to be run:
  `--enable-features=UseOzonePlatform --ozone-platform=wayland --enable-wayland-ime --wayland-text-input-version=3`

</details>

<details>
  <summary><b>Additional configurattion for Kanata</b></summary>

Add this line to file `~/.config/kanata/kanata.kbd`

```common-lisp
(defcfg
  ...
  linux-dev-names-exclude ("Lotus-Uinput-Server")
  ...
)
```

</details>

---

<a id="usage-guide"></a>

## 📖 Usage guide

### 1. Customize input method

You can customize the input method settings in two ways:

**Option 1: Right-click Menu (Quick Settings)**

Right-click the Lotus icon on the system tray to quickly toggle settings:

| Option                  | Description                                           | Default |
| :---------------------- | :---------------------------------------------------- | :------ |
| **Charset**             | Select character set.                                 | Unicode |
| **Spell Check**         | Enable/disable Vietnamese spell checking.             | On      |
| **Macro**               | Enable/disable shorthand typing.                      | On      |
| **Capitalize Macro**    | Enable/disable uppercase shorthand typing.            | On      |
| **Auto non-VN restore** | Enable/disable auto-restore for non-Vietnamese words. | On      |

</details>

**Option 2: Lotus - Fcitx Configuration Menu (Advanced Settings)**

Right-click the Lotus or Fcitx icon -> **Input Method Settings** -> Select **Lotus** -> Click the **Configure** (gear icon) in the middle.

Advanced options available here:

- **Macro:** Click the gear icon next to the _Macro_ row to manage the global shorthand list.
- **Custom Keymap:** Click the gear icon on the _Custom Keymap_ row to customize your keymap. Select **Custom** input method to apply.
- **Menu Hotkey:** Change the key to open the mode menu (default is `` ` ``) at _Mode menu hotkey_.

| Option                                                                    | Description                                                                    | Default         |
| :------------------------------------------------------------------------ | :----------------------------------------------------------------------------- | :-------------- |
| **Mode**                                                                  | Select typing mode.                                                            | Uinput (Smooth) |
| **Input Method**                                                          | Select input method.                                                           | Telex           |
| **Use oà, uý (Instead Of òa, úy)**                                        | Enable/disable modern tone mark placement style.                               | On              |
| **Allow Type With More Freedom**                                          | Enable/disable free tone marking.                                              | On              |
| **Allow dd To Produce đ When Auto Restore Keys With Invalid Words Is On** | Enable/disable allowing "dd" to produce "đ" when _Auto non-VN restore_ is On   | On              |
| **Fix Uinput Mode With Ack**                                              | Recommended when using Chromium-based applications (Chrome, Brave, Edge, ...). | Off             |
| **Use Lotus Status Icons**                                                | Enable/disable using Lotus icons instead of the default V/E icons.             | Off             |

### 2. Typing mode menu

In any application, press the **`** key (or the hotkey you have configured above) to open the typing mode selection menu, where you can use the mouse or shortcuts to select.

| Mode                  |   Shortcut    | Description                                                                                                                                         |
| :-------------------- | :-----------: | :-------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Uinput (Smooth)**   |     **1**     | Default mode, fast response.<br>**Optimal:** applications with high input processing speed.                                                         |
| **Uinput (Slow)**     |     **2**     | Similar to Uinput (Smooth) but with a slower key-sending speed.<br>**Optimal:** applications with low input processing speed _(e.g., LibreOffice)_. |
| **Uinput (Hardcore)** |     **3**     | A variant of Uinput (Smooth).<br>**Optimal:** Windows applications via Wine.                                                                        |
| **Surrounding Text**  |     **4**     | Allows editing marks on typed text, works smoothly. <br> **Optimal:** Qt/GTK applications.                                                          |
| **Preedit**           |     **Q**     | Displays underline while typing. <br> **Optimal:** most applications.                                                                               |
| **Emoji Picker**      |     **W**     | Search and input Emojis (fuzzy search supported).                                                                                                   |
| **OFF**               |     **E**     | Turn off input method.                                                                                                                              |
| **Default Typing**    |     **R**     | Default typing mode configured in the _Typing mode_ option.                                                                                         |
| **Type X**            | **X** / **F** | Type the key X (if the key to turn on the menu is configured in Fcitx5 configuration is a single key, e.g. X).                                      |

The input method automatically saves the most recently used typing mode for each application and restores that configuration when you reopen them.

### 3. Quick Phrase

Lotus is compatible with Fcitx5’s built-in **Quick Phrase** (default shortcut `Super` + `` ` ``). You can quickly input:

- **LaTeX**: Math symbols (e.g., `\alpha` → `α`).
- **Emoji Code**: Emoji characters (e.g., `:smile:` → `😄`).
- **Emoji**: Kaomoji-style phrases (e.g., `smile` → `(・∀・)`).

### 4. Reset typing state

Clicking the mouse or touching the touchpad while typing will automatically reset the typing state, preventing character sticking between words.

---

<a id="uninstallation"></a>

## 🗑️ Uninstallation

<sub>💡 Click beside the badge to expand the instructions.</sub>

<details>
<summary><a href="#uninstallation"><img src="https://img.shields.io/badge/Arch_Linux-1793D1?style=for-the-badge&logo=arch-linux&logoColor=white" alt="Arch Linux" height="25"></a></summary>
<br>

You can use `pacman` (recommended), `yay`, or `paru` to uninstall:

```bash
sudo pacman -Rns fcitx5-lotus
```

```bash
yay -Rns fcitx5-lotus
```

```bash
paru -Rns fcitx5-lotus
```

> **Note:** Config files in `$HOME` will be kept.

</details>

<details>
<summary><a href="#uninstallation"><img src="https://img.shields.io/badge/Debian-D70A53?style=for-the-badge&logo=debian&logoColor=white" alt="Debian" height="25"></a></summary>
<br>

```bash
sudo apt remove fcitx5-lotus
sudo rm /etc/apt/sources.list.d/fcitx5-lotus.list
sudo rm /etc/apt/keyrings/fcitx5-lotus.gpg
sudo apt update
```

</details>

<details>
<summary><a href="#uninstallation"><img src="https://img.shields.io/badge/Ubuntu-E95420?style=for-the-badge&logo=ubuntu&logoColor=white" alt="Ubuntu" height="25"></a></summary>
<br>

```bash
sudo apt remove fcitx5-lotus
sudo rm /etc/apt/sources.list.d/fcitx5-lotus.list
sudo rm /etc/apt/keyrings/fcitx5-lotus.gpg
sudo apt update
```

</details>

<details>
<summary><a href="#uninstallation"><img src="https://img.shields.io/badge/Fedora-51A2DA?style=for-the-badge&logo=fedora&logoColor=white" alt="Fedora" height="25"></a></summary>
<br>

```bash
sudo dnf remove fcitx5-lotus
sudo rm /etc/yum.repos.d/fcitx5-lotus-*.repo
```

</details>

<details>
<summary><a href="#uninstallation"><img src="https://img.shields.io/badge/openSUSE-73BA42?style=for-the-badge&logo=opensuse&logoColor=white" alt="openSUSE" height="25"></a></summary>
<br>

```bash
sudo zypper remove fcitx5-lotus
sudo zypper removerepo fcitx5-lotus
```

</details>

<details>
<summary><a href="#uninstallation"><img src="https://img.shields.io/badge/NixOS-5277C3?style=for-the-badge&logo=nixos&logoColor=white" alt="NixOS" height="25"></a></summary>
<br>

Delete (or comment out) the `services.fcitx5-lotus` and `inputs` lines in the config file, then rebuild the system. NixOS will clean up automatically.

</details>

<details>
<summary><a href="#uninstallation"><img src="https://img.shields.io/badge/GitHub_Releases-181717?style=for-the-badge&logo=github&logoColor=white" alt="GitHub Releases" height="25"></a></summary>
<br>

Uninstall based on your distribution:

```bash
# Debian / Ubuntu
sudo apt remove fcitx5-lotus
```

```bash
# Fedora
sudo dnf remove fcitx5-lotus
```

```bash
# openSUSE
sudo zypper remove fcitx5-lotus
```

</details>

<details>
<summary><a href="#uninstallation"><img src="https://img.shields.io/badge/Source-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" height="25"></a></summary>
<br>

Go back to the built source code directory and run:

```bash
sudo make uninstall
```

</details>

### 🧹 System Cleanup (Optional)

If you want to completely remove shorthand data and configuration:

```bash
rm -rf ~/.local/share/fcitx5/lotus
rm -rf ~/.config/fcitx5/conf/lotus.conf
```

---

<a id="contributing"></a>

## 🤝 Contributing

Contributions are what make the open-source community such an amazing place to learn, inspire, and create. Any contribution you make is **greatly appreciated**.

Please see the detailed guide [here](CONTRIBUTING.en.md) for how to participate in project development, the Pull Request process, code style rules, and **code of conduct**.

Don't forget to give the project a ⭐! Thank you very much!

---

<a id="license"></a>

## 📃 License

The project is distributed under the GNU General Public License v3. See [`LICENSE`](LICENSE) for details.

---

## Contributors ✨

Thanks goes to these wonderful people ([emoji key](https://allcontributors.org/docs/en/emoji-key)):

<!-- ALL-CONTRIBUTORS-LIST:START - Do not remove or modify this section -->
<!-- prettier-ignore-start -->
<!-- markdownlint-disable -->
<table>
  <tbody>
    <tr>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/nhktmdzhg"><img src="https://avatars.githubusercontent.com/u/57983253?v=4?s=100" width="100px;" alt="Nguyen Hoang Ky"/><br /><sub><b>Nguyen Hoang Ky</b></sub></a><br /><a href="#blog-nhktmdzhg" title="Blogposts">📝</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=nhktmdzhg" title="Code">💻</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=nhktmdzhg" title="Documentation">📖</a> <a href="#projectManagement-nhktmdzhg" title="Project Management">📆</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/pulls?q=is%3Apr+reviewed-by%3Anhktmdzhg" title="Reviewed Pull Requests">👀</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/hthienloc"><img src="https://avatars.githubusercontent.com/u/148019203?v=4?s=100" width="100px;" alt="Huỳnh Thiện Lộc"/><br /><sub><b>Huỳnh Thiện Lộc</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/issues?q=author%3Ahthienloc" title="Bug reports">🐛</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=hthienloc" title="Documentation">📖</a> <a href="#design-hthienloc" title="Design">🎨</a> <a href="#translation-hthienloc" title="Translation">🌍</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=hthienloc" title="Code">💻</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/justanoobcoder"><img src="https://avatars.githubusercontent.com/u/57614330?v=4?s=100" width="100px;" alt="Nguyễn Hồng Hiệp"/><br /><sub><b>Nguyễn Hồng Hiệp</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=justanoobcoder" title="Documentation">📖</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/Miho1254"><img src="https://avatars.githubusercontent.com/u/83270073?v=4?s=100" width="100px;" alt="Đặng Quang Hiển"/><br /><sub><b>Đặng Quang Hiển</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=Miho1254" title="Documentation">📖</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=Miho1254" title="Code">💻</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/Zebra2711"><img src="https://avatars.githubusercontent.com/u/89755535?v=4?s=100" width="100px;" alt="Zebra2711"/><br /><sub><b>Zebra2711</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/issues?q=author%3AZebra2711" title="Bug reports">🐛</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=Zebra2711" title="Code">💻</a></td>
    </tr>
  </tbody>
  <tfoot>
    <tr>
      <td align="center" size="13px" colspan="7">
        <img src="https://raw.githubusercontent.com/all-contributors/all-contributors-cli/1b8533af435da9854653492b1327a23a4dbd0a10/assets/logo-small.svg">
          <a href="https://all-contributors.js.org/docs/en/bot/usage">Add your contributions</a>
        </img>
      </td>
    </tr>
  </tfoot>
</table>

<!-- markdownlint-restore -->
<!-- prettier-ignore-end -->

<!-- ALL-CONTRIBUTORS-LIST:END -->

This project follows the [all-contributors](https://github.com/all-contributors/all-contributors) specification. Contributions of any kind welcome!

---

## ✨ Star History

<a href="https://star-history.com/#LotusInputMethod/fcitx5-lotus&Date">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=LotusInputMethod/fcitx5-lotus&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=LotusInputMethod/fcitx5-lotus&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=LotusInputMethod/fcitx5-lotus&type=date&legend=top-left" />
 </picture>
</a>
