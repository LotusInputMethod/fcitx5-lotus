{
  lib,
  stdenv,
  acl,
  buildGoModule,
  cmake,
  fcitx5,
  fetchFromGitHub,
  gettext,
  go,
  hicolor-icon-theme,
  kdePackages,
  libinput,
  libx11,
  pkg-config,
  python3,
  qt6,
  udev,
}:
stdenv.mkDerivation rec {
  pname = "fcitx5-lotus";
  version = "3.5.0";

  src = fetchFromGitHub {
    owner = "LotusInputMethod";
    repo = "fcitx5-lotus";
    rev = "v${version}";
    fetchSubmodules = true;
    hash = "sha256-aGkX2eOdOT/Jr7VK854qUN98+6EXO5GDUgEjE4bwZds=";
  };

  nativeBuildInputs = [
    cmake
    kdePackages.extra-cmake-modules
    gettext
    go
    hicolor-icon-theme
    pkg-config
    qt6.wrapQtAppsHook
  ];

  buildInputs = [
    acl
    fcitx5
    libinput
    libx11
    (python3.withPackages (
      ps: with ps; [
        pyqt6
        dbus-python
        qtpy
      ]
    ))
    qt6.qtbase
    udev
  ];

  vendorDir =
    (buildGoModule {
      pname = "fcitx5-lotus-go-modules";
      inherit version src;
      modRoot = "bamboo";
      vendorHash = "sha256-HjVMGil4bNMTFifxFYtHELdkeKhrumHGrde4msbxvJc=";
    }).goModules;

  preConfigure = ''
    export GOCACHE=$TMPDIR/go-cache
    export GOPATH=$TMPDIR/go

    rm -rf bamboo/vendor
    cp -r $vendorDir bamboo/vendor
  '';

  cmakeFlags = [
    "-DGO_FLAGS=-mod=vendor"
  ];

  # change checking exe_path logic to make it work on NixOS since executable files on NixOS are not located in /usr/bin
  postPatch = ''
    substituteInPlace src/lotus-monitor.cpp \
      --replace-fail 'strcmp(exe_path, "/usr/bin/fcitx5-lotus-server") == 0' \
                '(strncmp(exe_path, "/nix/store/", 11) == 0 && strlen(exe_path) >= 24 && strcmp(exe_path + strlen(exe_path) - 24, "/bin/fcitx5-lotus-server") == 0)'
    substituteInPlace server/lotus-server.cpp \
      --replace-fail 'strcmp(exe_path, "/usr/bin/fcitx5") == 0' \
                '(strncmp(exe_path, "/nix/store/", 11) == 0 && strlen(exe_path) >= 11 && strcmp(exe_path + strlen(exe_path) - 11, "/bin/fcitx5") == 0)'
  '';

  postInstall = ''
    substituteInPlace $out/lib/udev/rules.d/99-lotus.rules \
      --replace-fail "/usr/bin/setfacl" "${acl}/bin/setfacl"
    substituteInPlace $out/lib/systemd/system/fcitx5-lotus-server@.service \
      --replace-fail "/usr/bin/setfacl" "${acl}/bin/setfacl"
    substituteInPlace $out/lib/systemd/system/fcitx5-lotus-server@.service \
      --replace-fail "/usr/bin/fcitx5-lotus-server" "$out/bin/fcitx5-lotus-server"
  '';

  postFixup = ''
    patchShebangs $out/share/fcitx5-lotus/settings-gui
    wrapQtApp $out/bin/fcitx5-lotus-settings
  '';

  meta = with lib; {
    description = "Fcitx5 Lotus input method for Vietnamese typing";
    license = licenses.gpl3;
    platforms = platforms.linux;
  };
}
