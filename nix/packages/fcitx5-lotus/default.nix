{
  lib,
  stdenv,
  acl,
  cmake,
  dbus,
  fcitx5,
  fetchFromGitHub,
  gettext,
  gobject-introspection,
  gtk4,
  hicolor-icon-theme,
  kdePackages,
  libadwaita,
  libevdev,
  libinput,
  libnotify,
  libxcb,
  nix-update-script,
  pkg-config,
  python3,
  udev,
  wrapGAppsHook4,
  xvfb-run,
}:

let
  pythonEnv = python3.withPackages (
    ps: with ps; [
      dbus-python
      pygobject3
    ]
  );
in
stdenv.mkDerivation (finalAttrs: {
  pname = "fcitx5-lotus";
  version = "7.0.0";

  src = fetchFromGitHub {
    owner = "LotusInputMethod";
    repo = "fcitx5-lotus";
    tag = "v${finalAttrs.version}";
    hash = "sha256-lvhAesno4sM1Ap966gkQHh20pCJbAUJtAvbjVv8/oio=";
  };

  passthru = {
    updateScript = nix-update-script { };
  };

  nativeBuildInputs = [
    cmake
    gettext
    gobject-introspection
    hicolor-icon-theme
    kdePackages.extra-cmake-modules
    pkg-config
    wrapGAppsHook4
  ];

  nativeCheckInputs = [
    dbus
    xvfb-run
  ];

  buildInputs = [
    acl
    dbus
    fcitx5
    gtk4
    kdePackages.extra-cmake-modules
    libadwaita
    libevdev
    libinput
    libnotify
    libxcb
    pythonEnv
    udev
  ];

  strictDeps = true;

  __structuredAttrs = true;

  dontWrapGApps = true;

  doCheck = true;

  cmakeFlags = [
    "-DLOTUS_ALT_EXECUTABLE_PREFIX=/nix/store/"
    "-DLOTUS_SETFACL_EXECUTABLE=${acl}/bin/setfacl"
    (lib.cmakeBool "BUILD_TESTING" finalAttrs.finalPackage.doCheck)
  ];

  postFixup = ''
    patchShebangs $out/share/fcitx5-lotus/settings-gui
    wrapGApp $out/bin/fcitx5-lotus-settings \
      --prefix XDG_DATA_DIRS : "${hicolor-icon-theme}/share"
  '';

  meta = {
    description = "Vietnamese input method engine for Fcitx5";
    homepage = "https://github.com/LotusInputMethod/fcitx5-lotus";
    license = with lib.licenses; [
      gpl3Plus
    ];
    maintainers = with lib.maintainers; [
      justanoobcoder
    ];
    platforms = lib.platforms.linux;
  };
})
