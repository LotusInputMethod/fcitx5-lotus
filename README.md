[English](README.en.md) | [Tiếng Việt](README.md)

<a id="readme-top"></a>

<div align="center">
  <a href="https://lotusinputmethod.github.io/">
    <img src="data/fcitx-lotus-README.svg" alt="Logo" width="80" height="80">
  </a>

<h2 align="center">Fcitx5 Lotus</h2>

<p align="center">
    <b>Bộ gõ tiếng Việt đơn giản, hiệu năng cao cho Linux</b>
    <br />
    <a href="https://lotusinputmethod.github.io/"><strong>Khám phá trang chủ »</strong></a>
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
    <a href="#cài-đặt"><strong>Cài đặt »</strong></a>
    <br />
    <br />
    <a href="https://github.com/LotusInputMethod/fcitx5-lotus/issues/new?template=bug_report.yml">Báo lỗi</a>
    ·
    <a href="https://github.com/LotusInputMethod/fcitx5-lotus/issues/new?template=feature_request.yml">Yêu cầu tính năng</a>
  </p>
</div>

<br />

Dự án này là bản fork được tối ưu hóa từ [bộ gõ VMK](https://github.com/thanhpy2009/VMK). Chân thành cảm ơn tác giả Thành đã đặt nền móng cho bộ gõ này.

<details>
  <summary><b>Mục lục</b></summary>
  <ol>
    <li><a href="#cài-đặt">Cài đặt</a></li>
    <li><a href="#bật-bộ-gõ">Bật bộ gõ</a></li>
    <li><a href="#hướng-dẫn-sử-dụng">Hướng dẫn sử dụng</a></li>
    <li><a href="#gỡ-cài-đặt">Gỡ cài đặt</a></li>
    <li><a href="#đóng-góp">Đóng góp</a></li>
    <li><a href="#giấy-phép">Giấy phép</a></li>
  </ol>
</details>

---

<a id="cài-đặt"></a>

## 📦 Cài đặt

<sub>💡 Bấm vào phần bên cạnh badge để mở rộng hướng dẫn.</sub>

<details>
<summary><a href="#cài-đặt"><img src="https://img.shields.io/badge/Arch_Linux-1793D1?style=for-the-badge&logo=arch-linux&logoColor=white" alt="Arch Linux" height="25"></a></summary>
<br>

Hiện tại AUR có 3 gói cài đặt để bạn lựa chọn:

| Gói                | Mô tả                              |
| ------------------ | ---------------------------------- |
| `fcitx5-lotus`     | Build từ mã nguồn release ổn định  |
| `fcitx5-lotus-bin` | Dùng binary đã build sẵn           |
| `fcitx5-lotus-git` | Build từ danh sách commit mới nhất |

Cài đặt bằng `yay`:

```bash
# Cú pháp: yay -S <tên-gói>
yay -S fcitx5-lotus
```

Hoặc `paru`:

```bash
# Cú pháp: paru -S <tên-gói>
paru -S fcitx5-lotus
```

</details>

<details>
<summary><a href="#cài-đặt"><img src="https://img.shields.io/badge/Debian-D70A53?style=for-the-badge&logo=debian&logoColor=white" alt="Debian" height="25"></a></summary>
<br>

```bash
# Tự động nhận diện codename và cài đặt
CODENAME=$(grep '^VERSION_CODENAME=' /etc/os-release | cut -d'=' -f2)
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://fcitx5-lotus.pages.dev/pubkey.gpg | sudo gpg --dearmor -o /etc/apt/keyrings/fcitx5-lotus.gpg
echo "deb [signed-by=/etc/apt/keyrings/fcitx5-lotus.gpg] https://fcitx5-lotus.pages.dev/apt/$CODENAME $CODENAME main" | sudo tee /etc/apt/sources.list.d/fcitx5-lotus.list
sudo apt update && sudo apt install fcitx5-lotus
```

</details>

<details>
<summary><a href="#cài-đặt"><img src="https://img.shields.io/badge/Ubuntu-E95420?style=for-the-badge&logo=ubuntu&logoColor=white" alt="Ubuntu" height="25"></a></summary>
<br>

```bash
# Tự động nhận diện codename và cài đặt
CODENAME=$(grep '^UBUNTU_CODENAME=' /etc/os-release | cut -d'=' -f2)
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://fcitx5-lotus.pages.dev/pubkey.gpg | sudo gpg --dearmor -o /etc/apt/keyrings/fcitx5-lotus.gpg
echo "deb [signed-by=/etc/apt/keyrings/fcitx5-lotus.gpg] https://fcitx5-lotus.pages.dev/apt/$CODENAME $CODENAME main" | sudo tee /etc/apt/sources.list.d/fcitx5-lotus.list
sudo apt update && sudo apt install fcitx5-lotus
```

</details>

<details>
<summary><a href="#cài-đặt"><img src="https://img.shields.io/badge/Fedora-51A2DA?style=for-the-badge&logo=fedora&logoColor=white" alt="Fedora" height="25"></a></summary>
<br>

```bash
# Tự động nhận diện phiên bản và cài đặt
RELEASEVER=$(grep '^VERSION_ID=' /etc/os-release | cut -d'=' -f2)
sudo rpm --import https://fcitx5-lotus.pages.dev/pubkey.gpg
sudo dnf config-manager addrepo --from-repofile=https://fcitx5-lotus.pages.dev/rpm/fedora/fcitx5-lotus-$RELEASEVER.repo
sudo dnf install fcitx5-lotus
```

</details>

<details>
<summary><a href="#cài-đặt"><img src="https://img.shields.io/badge/openSUSE-73BA42?style=for-the-badge&logo=opensuse&logoColor=white" alt="openSUSE" height="25"></a></summary>
<br>

```bash
# Thêm repo và cài đặt (Tumbleweed)
sudo rpm --import https://fcitx5-lotus.pages.dev/pubkey.gpg
sudo zypper addrepo https://fcitx5-lotus.pages.dev/rpm/opensuse/fcitx5-lotus-tumbleweed.repo
sudo zypper refresh
sudo zypper install fcitx5-lotus
```

</details>

<details>
<summary><a href="#cài-đặt"><img src="https://img.shields.io/badge/NixOS-5277C3?style=for-the-badge&logo=nixos&logoColor=white" alt="NixOS" height="25"></a></summary>
<br>

Thêm input của fcitx5-lotus vào `flake.nix`:

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

Bật fcitx5-lotus service trong `configuration.nix`:

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
    user = "your_username"; # Sửa thành tên user của bạn
  };
}
```

Rebuild lại system để cài đặt.

</details>

<details>
<summary><a href="#cài-đặt"><img src="https://img.shields.io/badge/GitHub_Releases-181717?style=for-the-badge&logo=github&logoColor=white" alt="GitHub Releases" height="25"></a></summary>
<br>

Tải file `.deb` hoặc `.rpm` trực tiếp từ [GitHub Releases](https://github.com/LotusInputMethod/fcitx5-lotus/releases/latest):

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
<summary><a href="#cài-đặt"><img src="https://img.shields.io/badge/Source-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" height="25"></a></summary>
<br>

> **KHUYẾN CÁO QUAN TRỌNG:**
>
> Vui lòng **KHÔNG** sử dụng cách này nếu distro của bạn đã được hỗ trợ thông qua **Cloudflare Pages**.
>
> Việc biên dịch thủ công đòi hỏi bạn phải hiểu rõ về cấu trúc thư mục của hệ thống. Nếu bạn gặp lỗi "Not Available" hoặc thiếu thư viện khi cài theo cách này trên các distro phổ biến (Ubuntu/Fedora...), hãy quay lại dùng Cloudflare Pages để đảm bảo tính ổn định và tự động cập nhật.

### Yêu cầu hệ thống

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

### Biên dịch và cài đặt

```bash
# Clone repository
git clone https://github.com/LotusInputMethod/fcitx5-lotus.git
cd fcitx5-lotus

# Biên dịch
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=/usr/lib . # Tùy vào distro mà LIBDIR sẽ khác nhau
make

# Cài đặt (cần quyền root)
sudo make install

# Hoặc cài đặt vào thư mục tùy chỉnh
sudo make install PREFIX=/usr/local
```

</details>

---

<a id="bật-bộ-gõ"></a>

## ⚙️ Bật bộ gõ

Sau khi cài đặt xong, bạn cần thực hiện các bước sau để bật bộ gõ fcitx5-lotus:

> **💡 Tự động hoá:** Nếu bạn cài qua **Debian/Ubuntu** (`.deb`), post-install script sẽ tự động thực hiện các bước 1–3 bên dưới. Sau khi cài đặt, bạn chỉ cần làm tiếp từ bước 4.

### 1. Bật Lotus Server

Server giúp bộ gõ tương tác với hệ thống tốt hơn (đặc biệt là gửi phím xóa và sửa lỗi).

<details open>
<summary><b>Bash / Zsh</b></summary>

```bash
# Bật và khởi động service (tự động fix lỗi thiếu user systemd nếu có)
sudo systemctl enable --now fcitx5-lotus-server@$(whoami).service || \
(sudo systemd-sysusers && sudo systemctl enable --now fcitx5-lotus-server@$(whoami).service)
```

</details>

<details>
<summary><b>Fish shell</b></summary>

```fish
# Bật và khởi động service (tự động fix lỗi thiếu user systemd nếu có)
sudo systemctl enable --now fcitx5-lotus-server@(whoami).service; or begin
    sudo systemd-sysusers; and sudo systemctl enable --now fcitx5-lotus-server@(whoami).service
end
```

</details>

Kiểm tra status (nếu thấy active (running) là OK):

```bash
systemctl status fcitx5-lotus-server@$(whoami).service
```

### 2. Thiết lập biến môi trường

Bộ gõ sẽ không hoạt động nếu thiếu các biến này.

<details open>
<summary><b>Bash</b></summary>

```bash
# Thêm cấu hình vào ~/.bash_profile
cat <<EOF >> ~/.bash_profile
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
export SDL_IM_MODULE=fcitx
export GLFW_IM_MODULE=ibus
EOF
```

</details>

<details>
<summary><b>Zsh</b></summary>

```bash
# Thêm cấu hình vào ~/.zprofile
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
# Thêm cấu hình vào ~/.config/fish/config.fish
echo 'if status is-login
    set -Ux GTK_IM_MODULE fcitx
    set -Ux QT_IM_MODULE fcitx
    set -Ux XMODIFIERS "@im=fcitx"
    set -gx SDL_IM_MODULE fcitx
    set -gx GLFW_IM_MODULE ibus
end' >> ~/.config/fish/config.fish
```

</details>

Log out và log in để áp dụng thay đổi.

<details>
<summary><b>Nếu bạn vẫn chưa gõ được sau khi log out</b></summary>
<br>

Nếu cấu hình tại `~/.bash_profile`, `~/.zprofile` hay `.config/fish/config.fish` không hoạt động, bạn có thể thử thiết lập tại `/etc/environment` để áp dụng cho toàn bộ hệ thống:

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

> **Lưu ý:** Cần khởi động lại máy sau khi thiết lập.

</details>

### 3. Tắt bộ gõ cũ (IBus)

Nếu máy bạn đang dùng IBus, hãy tắt nó đi trước khi chuyển sang Fcitx5 để tránh xung đột.

```bash
# Tắt IBus
killall ibus-daemon || ibus exit
```

> **Lưu ý:** Hãy tắt autostart của IBus (thường là `ibus-daemon` hoặc `ibus`) để tránh xung đột. Tốt nhất là gỡ cài đặt IBus nếu không sử dụng.

### 4. Thêm Fcitx5 vào Autostart

| DE / WM        | Hướng dẫn chi tiết                                                                                                           |
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
| **i3 / Sway**  | Thêm `exec --no-startup-id fcitx5 -d` vào file config (`~/.config/i3/config` hoặc `~/.config/sway/config`)                   |
| **Hyprland**   | Thêm `exec-once = fcitx5 -d` vào `~/.config/hypr/hyprland.conf`                                                              |

</details>

### 5. Cấu hình Fcitx5

Sau khi đã log out và log in lại:

1. Mở **Fcitx5 Configuration** (tìm trong menu ứng dụng hoặc chạy `fcitx5-configtool`).
2. Tìm **Lotus** ở cột bên phải.
3. Nhấn mũi tên **<** để thêm nó sang cột bên trái.
4. Apply.

<details>
  <summary><b>Cấu hình thêm cho Wayland (KDE, Hyprland, Chromium-based, Electron)</b></summary>

- **KDE Plasma:** _System Settings_ → _Keyboard_ → _Virtual Keyboard_ → Chọn **Fcitx 5**.
- **Hyprland:** Thêm dòng sau vào `~/.config/hypr/hyprland.conf`:

```ini
permission = fcitx5-lotus-server, keyboard, allow
```

- **Chromium-based/Electron:** Thêm flags sau vào ứng dụng cần chạy:
  `--enable-features=UseOzonePlatform --ozone-platform=wayland --enable-wayland-ime --wayland-text-input-version=3`

</details>

<details>
   <summary><b>Cấu hình thêm cho Kanata</b></summary>

Thêm dòng sau vào file `~/.config/kanata/kanata.kbd`

```common-lisp
(defcfg
  ...
  linux-dev-names-exclude ("Lotus-Uinput-Server")
  ...
)
```

</details>

---

<a id="hướng-dẫn-sử-dụng"></a>

## 📖 Hướng dẫn sử dụng

### 1. Tùy chỉnh bộ gõ

Bạn có thể tùy chỉnh các thông số của bộ gõ qua 2 cách sau:

**Cách 1: Menu chuột phải (Quick Settings)**

Nhấp chuột phải vào biểu tượng Lotus trên system tray để mở nhanh các tùy chỉnh:

| Tùy chọn                | Mô tả                                                   | Mặc định |
| :---------------------- | :------------------------------------------------------ | :------- |
| **Charset**             | Chọn bảng mã.                                           | Unicode  |
| **Spell Check**         | Bật/tắt kiểm tra lỗi chính tả tiếng Việt.               | Bật      |
| **Macro**               | Bật/tắt gõ tắt.                                         | Bật      |
| **Capitalize Macro**    | Bật/tắt gõ tắt chữ hoa.                                 | Bật      |
| **Auto non-VN restore** | Bật/tắt tự động khôi phục với từ không phải tiếng Việt. | Bật      |

**Cách 2: Menu cấu hình Lotus - Fcitx (Advanced Settings)**

Nhấp chuột phải vào biểu tượng Lotus hoặc icon Fcitx -> **Input Method Settings** (Cấu hình bộ gõ) -> Chọn **Lotus** -> Nhấn biểu tượng **Configure** (bánh răng) ở giữa.

Tại đây bạn có thể tùy chỉnh chi tiết:

- **Gõ tắt / Macro:** Nhấn bánh răng cạnh dòng _Macro_ để quản lý danh sách gõ tắt toàn cục.
- **Keymap tùy chỉnh:** Nhấn bánh răng dòng _Custom Keymap_ để tự định nghĩa phím. Chọn kiểu gõ **Custom** để áp dụng.
- **Phím tắt Menu:** Đổi phím nóng mở menu chế độ gõ (mặc định là `` ` ``) tại dòng _Mode menu hotkey_.

| Tùy chọn                                                                  | Mô tả                                                         | Mặc định        |
| :------------------------------------------------------------------------ | :------------------------------------------------------------ | :-------------- |
| **Mode**                                                                  | Chọn chế độ gõ.                                               | Uinput (Smooth) |
| **Input Method**                                                          | Chọn kiểu gõ.                                                 | Telex           |
| **Use oà, uý (Instead Of òa, úy)**                                        | Bật/tắt kiểu đặt dấu thanh hiện đại.                          | Bật             |
| **Allow Type With More Freedom**                                          | Bật/tắt bỏ dấu tự do.                                         | Bật             |
| **Allow dd To Produce đ When Auto Restore Keys With Invalid Words Is On** | Bật/tắt cho phép "dd" tạo "đ" khi dùng _Auto non-VN restore_. | Bật             |
| **Fix Uinput Mode With Ack**                                              | Khuyên dùng cho ứng dụng Chromium (Chrome, Brave, Edge, ...). | Tắt             |
| **Use Lotus Status Icons**                                                | Dùng icon Lotus thay vì icon V/E mặc định.                    | Tắt             |

### 2. Menu chuyển chế độ gõ

Khi con trỏ đang ở trong ô nhập liệu (có thể gõ văn bản), nhấn phím `` ` `` (hoặc phím tắt bạn đã tuỳ chỉnh ở trên) để mở menu chọn chế độ gõ; bạn có thể dùng chuột hoặc phím tắt để chọn chế độ mong muốn.

| Chế độ                |   Phím tắt    | Mô tả                                                                                                                                |
| :-------------------- | :-----------: | :----------------------------------------------------------------------------------------------------------------------------------- |
| **Uinput (Smooth)**   |     **1**     | Chế độ mặc định, phản hồi nhanh.<br>**Tối ưu:** ứng dụng có tốc độ xử lý input cao.                                                  |
| **Uinput (Slow)**     |     **2**     | Tương tự Uinput (Smooth) nhưng tốc độ gửi phím chậm hơn.<br>**Tối ưu:** ứng dụng có tốc độ xử lý input thấp _(ví dụ: Libre Office)_. |
| **Uinput (Hardcore)** |     **3**     | Biến thể của Uinput (Smooth).<br>**Tối ưu:** ứng dụng Windows qua Wine.                                                              |
| **Surrounding Text**  |     **4**     | Cho phép sửa dấu trên văn bản đã gõ, hoạt động mượt. <br> **Tối ưu:** ứng dụng Qt/GTK.                                               |
| **Preedit**           |     **Q**     | Hiển thị gạch chân khi gõ. <br> **Tối ưu:** hầu hết ứng dụng.                                                                        |
| **Emoji Picker**      |     **W**     | Tìm kiếm và nhập Emoji (hỗ trợ fuzzy search).                                                                                        |
| **OFF**               |     **E**     | Tắt bộ gõ.                                                                                                                           |
| **Default Typing**    |     **R**     | Chế độ gõ mặc định được cấu hình tại tuỳ chọn _Typing mode_.                                                                         |
| **Type X**            | **X** / **F** | Gõ phím X (nếu phím để bật menu được thiết lập trong Fcitx5 configuration là phím đơn, ví dụ như phím X).                            |

Bộ gõ sẽ tự động lưu chế độ gõ đã dùng gần nhất cho từng ứng dụng và khôi phục cấu hình đó khi bạn mở lại chúng.

### 3. Chỉnh sửa nhanh (Quick Phrase)

Lotus tương thích với tính năng **Quick Phrase** có sẵn của Fcitx5 (mặc định dùng `Super` + `` ` ``). Bạn có thể nhập nhanh:

- **LaTeX**: Ký tự toán học (vd: `\alpha` → `α`).
- **Emoji Code**: Biểu tượng cảm xúc (vd: `:smile:` → `😄`).
- **Emoji**: Các đoạn văn bản mẫu hoặc Kaomoji (vd: `smile` → `(・∀・)`).

### 4. Đặt lại trạng thái đang gõ

Nhấp chuột hoặc chạm touchpad trong khi gõ sẽ tự động đặt lại trạng thái đang gõ, ngăn chặn hiện tượng dính ký tự giữa các từ.

---

<a id="gỡ-cài-đặt"></a>

## 🗑️ Gỡ cài đặt

<sub>💡 Bấm vào phần bên cạnh badge để mở rộng hướng dẫn.</sub>

<details>
<summary><a href="#gỡ-cài-đặt"><img src="https://img.shields.io/badge/Arch_Linux-1793D1?style=for-the-badge&logo=arch-linux&logoColor=white" alt="Arch Linux" height="25"></a></summary>
<br>

Bạn có thể dùng `pacman` (khuyên dùng), `yay` hoặc `paru` để gỡ cài đặt:

```bash
sudo pacman -Rns fcitx5-lotus
```

```bash
yay -Rns fcitx5-lotus
```

```bash
paru -Rns fcitx5-lotus
```

> **Lưu ý:** Các file config ở `$HOME` sẽ được giữ lại.

</details>

<details>
<summary><a href="#gỡ-cài-đặt"><img src="https://img.shields.io/badge/Debian-D70A53?style=for-the-badge&logo=debian&logoColor=white" alt="Debian" height="25"></a></summary>
<br>

```bash
sudo apt remove fcitx5-lotus
sudo rm /etc/apt/sources.list.d/fcitx5-lotus.list
sudo rm /etc/apt/keyrings/fcitx5-lotus.gpg
sudo apt update
```

</details>

<details>
<summary><a href="#gỡ-cài-đặt"><img src="https://img.shields.io/badge/Ubuntu-E95420?style=for-the-badge&logo=ubuntu&logoColor=white" alt="Ubuntu" height="25"></a></summary>
<br>

```bash
sudo apt remove fcitx5-lotus
sudo rm /etc/apt/sources.list.d/fcitx5-lotus.list
sudo rm /etc/apt/keyrings/fcitx5-lotus.gpg
sudo apt update
```

</details>

<details>
<summary><a href="#gỡ-cài-đặt"><img src="https://img.shields.io/badge/Fedora-51A2DA?style=for-the-badge&logo=fedora&logoColor=white" alt="Fedora" height="25"></a></summary>
<br>

```bash
sudo dnf remove fcitx5-lotus
sudo rm /etc/yum.repos.d/fcitx5-lotus-*.repo
```

</details>

<details>
<summary><a href="#gỡ-cài-đặt"><img src="https://img.shields.io/badge/openSUSE-73BA42?style=for-the-badge&logo=opensuse&logoColor=white" alt="openSUSE" height="25"></a></summary>
<br>

```bash
sudo zypper remove fcitx5-lotus
sudo zypper removerepo fcitx5-lotus
```

</details>

<details>
<summary><a href="#gỡ-cài-đặt"><img src="https://img.shields.io/badge/NixOS-5277C3?style=for-the-badge&logo=nixos&logoColor=white" alt="NixOS" height="25"></a></summary>
<br>

Xóa (hoặc comment) dòng `services.fcitx5-lotus` và `inputs` trong file config, sau đó rebuild lại system. NixOS sẽ tự dọn dẹp.

</details>

<details>
<summary><a href="#gỡ-cài-đặt"><img src="https://img.shields.io/badge/GitHub_Releases-181717?style=for-the-badge&logo=github&logoColor=white" alt="GitHub Releases" height="25"></a></summary>
<br>

Gỡ cài đặt tùy theo distro bạn đang sử dụng:

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
<summary><a href="#gỡ-cài-đặt"><img src="https://img.shields.io/badge/Source-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" height="25"></a></summary>
<br>

Vào lại thư mục source code đã build và chạy:

```bash
sudo make uninstall
```

</details>

### 🧹 Dọn dẹp hệ thống (Tùy chọn)

Nếu muốn xóa hoàn toàn dữ liệu gõ tắt và cấu hình:

```bash
rm -rf ~/.local/share/fcitx5/lotus
rm -rf ~/.config/fcitx5/conf/lotus.conf
```

---

<a id="đóng-góp"></a>

## 🤝 Đóng góp

Đóng góp là điều làm cho cộng đồng mã nguồn mở trở thành một nơi tuyệt vời để học hỏi, truyền cảm hứng và sáng tạo. Mọi đóng góp của bạn đều được **đánh giá cao**.

Vui lòng xem hướng dẫn chi tiết [tại đây](CONTRIBUTING.md) để biết cách tham gia phát triển dự án, quy trình Pull Request, quy tắc code style và **quy tắc ứng xử**.

Đừng quên tặng dự án một ⭐! Cảm ơn bạn rất nhiều!

---

## Những người đóng góp ✨

Cảm ơn những con người tuyệt vời ([chú thích emoji](https://allcontributors.org/docs/en/emoji-key)):

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

Dự án này tuân thủ cấu trúc của [all-contributors](https://github.com/all-contributors/all-contributors). Mọi đóng góp đều được hoan nghênh!

---

<a id="giấy-phép"></a>

## 📃 Giấy phép

Dự án được phân phối dưới giấy phép GNU General Public License v3. Xem [`LICENSE`](LICENSE) để biết thêm chi tiết.

---

## ✨ Lịch sử sao

<a href="https://star-history.com/#LotusInputMethod/fcitx5-lotus&Date">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=LotusInputMethod/fcitx5-lotus&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=LotusInputMethod/fcitx5-lotus&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=LotusInputMethod/fcitx5-lotus&type=date&legend=top-left" />
 </picture>
</a>
