{
  lib,
  stdenv,
  fetchFromGitHub,
  cmake,
  extra-cmake-modules,
  pkg-config,
  go,
  gettext,
  hicolor-icon-theme,
  fcitx5,
  libinput,
  libx11,
  udev,
  qt6,
  python3,
}:
stdenv.mkDerivation rec {
  pname = "fcitx5-lotus";
  version = "1.5.1";

  src = fetchFromGitHub {
    owner = "LotusInputMethod";
    repo = "fcitx5-lotus";
    rev = "v${version}";
    fetchSubmodules = true;
    sha256 = "sha256-+h1cpxB1g7J/wh5CqggYK8l08rn1pXQxYZPf8DsRKYM=";
  };

  nativeBuildInputs = [
    cmake
    extra-cmake-modules
    pkg-config
    go
    gettext
    hicolor-icon-theme
    qt6.wrapQtAppsHook
    (python3.withPackages (ps: with ps; [
      pyside6
      dbus-python
    ]))
  ];

  buildInputs = [
    fcitx5
    libinput
    libx11
    udev
    qt6.qtbase
  ];

  preConfigure = ''
    export GOCACHE=$TMPDIR/go-cache
    export GOPATH=$TMPDIR/go

    cd bamboo
    go mod vendor
    cd ..
  '';

  cmakeFlags = [
    "-DCMAKE_INSTALL_PREFIX=${placeholder "out"}"
    "-DCMAKE_INSTALL_LIBDIR=lib"
    "-DGO_FLAGS=-mod=vendor"
  ];

  # change checking exe_path logic to make it work on NixOS since executable files on NixOS are not located in /usr/bin
  postPatch = ''
    substituteInPlace src/lotus-monitor.cpp \
      --replace-fail 'strcmp(exe_path, "/usr/bin/fcitx5-lotus-server") == 0' \
                '(strncmp(exe_path, "/nix/store/", 22) == 0 && strlen(exe_path) >= 22 && strcmp(exe_path + strlen(exe_path) - 22, "/bin/fcitx5-lotus-server") == 0)'
    substituteInPlace server/lotus-server.cpp \
      --replace-fail 'strcmp(exe_path, "/usr/bin/fcitx5") == 0' \
                '(strncmp(exe_path, "/nix/store/", 11) == 0 && strlen(exe_path) >= 11 && strcmp(exe_path + strlen(exe_path) - 11, "/bin/fcitx5") == 0)'
  '';

  postInstall = ''
    if [ -d "$out/lib/systemd/system" ]; then
      substituteInPlace $out/lib/systemd/system/fcitx5-lotus-server@.service \
        --replace-fail "/usr/bin/fcitx5-lotus-server" "$out/bin/fcitx5-lotus-server"
    fi
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
