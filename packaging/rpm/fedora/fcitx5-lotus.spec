Name:           fcitx5-lotus
Version:        3.5.6
Release:        1
Summary:        Vietnamese input method for fcitx5
License:        GPL-3.0-or-later
URL:            https://github.com/LotusInputMethod/fcitx5-lotus
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  extra-cmake-modules
BuildRequires:  gcc-c++
BuildRequires:  gettext-devel
BuildRequires:  glibc-devel
BuildRequires:  cmake(Fcitx5Core)
BuildRequires:  libinput-devel
BuildRequires:  systemd-rpm-macros
BuildRequires:  pkgconfig(libudev)
BuildRequires:  libX11-devel

BuildRequires:  golang
BuildRequires:  python3
BuildRequires:  librsvg2-tools
BuildRequires:  hicolor-icon-theme
BuildRequires:  breeze-icon-theme

%{?systemd_requires}
Requires:       fcitx5-data
Requires:       fcitx5
Requires:       python3-QtPy
Requires:       (python3-pyqt6 or python3-pyside6)
Requires:       python3-dbus
Requires:       hicolor-icon-theme
Requires:       acl

%description
Vietnamese input method for fcitx5

%prep
%setup -q

%build
%cmake
%cmake_build

%install
%cmake_install
%find_lang %{name}

%files -f %{name}.lang
%{_datadir}/licenses/%{name}/GPL-3.0-or-later.txt
%{_datadir}/licenses/%{name}/LGPL-2.1-or-later.txt

%dir %{_datadir}/licenses/%{name}
%dir %{_modulesloaddir}
%{_bindir}/fcitx5-lotus-server
%{_bindir}/fcitx5-lotus-settings

%{_libdir}/fcitx5/liblotus.so

%{_modulesloaddir}/fcitx5-lotus.conf
%{_unitdir}/fcitx5-lotus-server@.service
%{_sysusersdir}/lotus.conf
%{_udevrulesdir}/99-lotus.rules

%{_datadir}/fcitx5/addon/lotus.conf
%{_datadir}/fcitx5/inputmethod/lotus.conf

%{_datadir}/fcitx5/lotus/
%{_datadir}/fcitx5-lotus/
%{_datadir}/applications/org.fcitx.Fcitx5.Addon.Lotus.Settings.desktop

%{_datadir}/icons/hicolor/scalable/apps/*fcitx-lotus*.svg
%{_datadir}/icons/hicolor/scalable/status/fcitx-lotus*.svg
%{_datadir}/icons/hicolor/*/status/fcitx-lotus*.png
%{_datadir}/icons/breeze/status/*/fcitx-lotus*.svg
%{_datadir}/icons/breeze-dark/status/*/fcitx-lotus*.svg

%{_datadir}/metainfo/org.fcitx.Fcitx5.Addon.Lotus.metainfo.xml

%post
%systemd_post fcitx5-lotus-server@.service

if [ $1 -eq 1 ]; then
    echo "--- Cấu hình Lotus ---"
    echo "Hướng dẫn sau cài đặt:"
    echo "1. Kích hoạt Server cho user của bạn:"
    echo "   sudo systemctl enable --now fcitx5-lotus-server@\$(whoami).service"
    echo ""
    echo "2. Cấu hình Fcitx5:"
    echo "   - Mở 'Fcitx5 Configuration', thêm bộ gõ Lotus"
    echo ""
    echo "3. Lưu ý cho Wayland (KDE):"
    echo "   - Hãy chọn 'Fcitx 5' trong phần Virtual Keyboard của hệ thống."
    echo "------------------------------------------------"
elif [ $1 -eq 2 ]; then
    echo "--- Cấu hình Lotus ---"
    echo "Hướng dẫn sau cập nhật:"
    echo "1. Khởi động lại Server cho user của bạn:"
    echo "   sudo systemctl restart fcitx5-lotus-server@\$(whoami).service"
    echo ""
    echo "2. Cấu hình Fcitx5:"
    echo "   - Mở 'Fcitx5 Configuration', nhấn restart để khởi động lại."
fi


%preun
%systemd_preun fcitx5-lotus-server@.service

%postun
%systemd_postun_with_restart fcitx5-lotus-server@.service

%changelog
* Sat Aug 29 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com> - 3.5.6-1
- Add surrtext as option
