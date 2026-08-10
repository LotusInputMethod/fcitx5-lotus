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
      <img src="https://img.shields.io/badge/all_contributors-13-orange.svg?style=flat-square" alt="All Contributors">
    </a>
    <a href="https://deepwiki.com/LotusInputMethod/fcitx5-lotus"><img src="https://deepwiki.com/badge.svg" alt="Ask DeepWiki"></a>
  </p>

  <p align="center">
    <a href="https://lotusinputmethod.github.io/#installation"><strong>Installation »</strong></a>
    <br />
    <br />
    <a href="https://github.com/LotusInputMethod/fcitx5-lotus/issues/new?template=bug_report.yml">Report Bug</a>
    &middot;
    <a href="https://github.com/LotusInputMethod/fcitx5-lotus/issues/new?template=feature_request.yml">Request Feature</a>
  </p>
</div>

<br />

This project is an optimized fork of [VMK input method](https://github.com/thanhpy2009/VMK). Sincere thanks to the author Thanh for laying the foundation for this input method.

## Installation & Usage Guide

To have the best experience and get the most out of insallation, configuration, and support for each Linux distribution (Arch, Debian, Ubuntu, Fedora, NixOS...), please visit the project website:

**[See the installation guide for more details](https://lotusinputmethod.github.io/#installation)**

---

## Compilation from source (for developers)

If you want to compile the input method from source to contribute or customize:

### Dependencies

- **Debian/Ubuntu:** `sudo apt-get install cmake extra-cmake-modules libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev libinput-dev libudev-dev g++ golang hicolor-icon-theme pkg-config libx11-dev fcitx5-modules-dev python3-qtpy python3-dbus acl`
- **Fedora/RHEL:** `sudo dnf install cmake extra-cmake-modules fcitx5-devel libinput-devel libudev-devel gcc-c++ golang hicolor-icon-theme systemd-devel libX11-devel python3-pyside6 python3-QtPy python3-dbus acl`
- **openSUSE:** `sudo zypper install cmake extra-cmake-modules fcitx5-devel libinput-devel systemd-devel gcc-c++ go hicolor-icon-theme systemd-devel libX11-devel udev python3-pyside6 python3-QtPy python3-dbus-python acl`
- **Arch:** `sudo pacman -S --needed cmake extra-cmake-modules gcc go git libx11 python make pkgconf acl fcitx5 libinput hicolor-icon-theme python-qtpy python-dbus`

### Install

```bash
git clone --recurse-submodules https://github.com/LotusInputMethod/fcitx5-lotus.git
cd fcitx5-lotus
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=/usr/lib . #LIBDIR will vary depending on the distro
make
sudo make install
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
      <td align="center" valign="top" width="25%"><a href="https://github.com/nhktmdzhg"><img src="https://avatars.githubusercontent.com/u/57983253?v=4?s=100" width="100px;" alt="Nguyen Hoang Ky"/><br /><sub><b>Nguyen Hoang Ky</b></sub></a><br /><a href="#blog-nhktmdzhg" title="Blogposts">📝</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=nhktmdzhg" title="Code">💻</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=nhktmdzhg" title="Documentation">📖</a> <a href="#projectManagement-nhktmdzhg" title="Project Management">📆</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/pulls?q=is%3Apr+reviewed-by%3Anhktmdzhg" title="Reviewed Pull Requests">👀</a></td>
      <td align="center" valign="top" width="25%"><a href="https://github.com/hthienloc"><img src="https://avatars.githubusercontent.com/u/148019203?v=4?s=100" width="100px;" alt="Huỳnh Thiện Lộc"/><br /><sub><b>Huỳnh Thiện Lộc</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/issues?q=author%3Ahthienloc" title="Bug reports">🐛</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=hthienloc" title="Documentation">📖</a> <a href="#design-hthienloc" title="Design">🎨</a> <a href="#translation-hthienloc" title="Translation">🌍</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=hthienloc" title="Code">💻</a></td>
      <td align="center" valign="top" width="25%"><a href="https://github.com/justanoobcoder"><img src="https://avatars.githubusercontent.com/u/57614330?v=4?s=100" width="100px;" alt="Nguyễn Hồng Hiệp"/><br /><sub><b>Nguyễn Hồng Hiệp</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=justanoobcoder" title="Documentation">📖</a></td>
      <td align="center" valign="top" width="25%"><a href="https://github.com/Miho1254"><img src="https://avatars.githubusercontent.com/u/83270073?v=4?s=100" width="100px;" alt="Đặng Quang Hiển"/><br /><sub><b>Đặng Quang Hiển</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=Miho1254" title="Documentation">📖</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=Miho1254" title="Code">💻</a></td>
    </tr>
    <tr>
      <td align="center" valign="top" width="25%"><a href="https://github.com/Zebra2711"><img src="https://avatars.githubusercontent.com/u/89755535?v=4?s=100" width="100px;" alt="Zebra2711"/><br /><sub><b>Zebra2711</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/issues?q=author%3AZebra2711" title="Bug reports">🐛</a> <a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=Zebra2711" title="Code">💻</a></td>
      <td align="center" valign="top" width="25%"><a href="https://github.com/ductrantrong"><img src="https://avatars.githubusercontent.com/u/96020037?v=4?s=100" width="100px;" alt="ductrantrong"/><br /><sub><b>ductrantrong</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=ductrantrong" title="Code">💻</a></td>
      <td align="center" valign="top" width="25%"><a href="https://github.com/hien-ngo29"><img src="https://avatars.githubusercontent.com/u/98635550?v=4?s=100" width="100px;" alt="Ngo Phu Hien"/><br /><sub><b>Ngo Phu Hien</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=hien-ngo29" title="Code">💻</a></td>
      <td align="center" valign="top" width="25%"><a href="https://github.com/minhtrancccp"><img src="https://avatars.githubusercontent.com/u/33189614?v=4?s=100" width="100px;" alt="Minh Tran"/><br /><sub><b>Minh Tran</b></sub></a><br /><a href="#platform-minhtrancccp" title="Packaging/porting to new platform">📦</a></td>
    </tr>
    <tr>
      <td align="center" valign="top" width="25%"><a href="https://github.com/hieutran21198"><img src="https://avatars.githubusercontent.com/u/87953912?v=4?s=100" width="100px;" alt="Trần Minh Hiếu (Cirius)"/><br /><sub><b>Trần Minh Hiếu (Cirius)</b></sub></a><br /><a href="#platform-hieutran21198" title="Packaging/porting to new platform">📦</a></td>
      <td align="center" valign="top" width="25%"><a href="https://github.com/namtao"><img src="https://avatars.githubusercontent.com/u/48555338?v=4?s=100" width="100px;" alt="namtao"/><br /><sub><b>namtao</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=namtao" title="Code">💻</a></td>
      <td align="center" valign="top" width="25%"><a href="https://github.com/minhdaolesoez"><img src="https://avatars.githubusercontent.com/u/70090530?v=4?s=100" width="100px;" alt="Dao Le Minh"/><br /><sub><b>Dao Le Minh</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=minhdaolesoez" title="Code">💻</a></td>
      <td align="center" valign="top" width="25%"><a href="https://github.com/xoaiPro235"><img src="https://avatars.githubusercontent.com/u/145014259?v=4?s=100" width="100px;" alt="Mai Văn Lực"/><br /><sub><b>Mai Văn Lực</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=xoaiPro235" title="Code">💻</a></td>
    </tr>
    <tr>
      <td align="center" valign="top" width="25%"><a href="https://github.com/0353618151hao-png"><img src="https://avatars.githubusercontent.com/u/259352926?v=4?s=100" width="100px;" alt="nothing"/><br /><sub><b>nothing</b></sub></a><br /><a href="https://github.com/LotusInputMethod/fcitx5-lotus/commits?author=0353618151hao-png" title="Code">💻</a></td>
    </tr>
  </tbody>
  <tfoot>
    <tr>
      <td align="center" size="13px" colspan="4">
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

<a href="https://github.com/LotusInputMethod/fcitx5-lotus">
  <img
    src="https://lotusinputmethod.github.io/starcharts.svg"
    alt="Star History Chart"
  />
</a>
