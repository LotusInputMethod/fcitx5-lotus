{
  config,
  lib,
  inputs,
  pkgs,
  ...
}:
with lib;
let
  cfg = config.services.fcitx5-lotus;
  fcitx5-lotus = inputs.self.packages.${pkgs.stdenv.hostPlatform.system}.fcitx5-lotus;
in
{
  options.services.fcitx5-lotus = {
    enable = mkEnableOption "Fcitx5 Lotus integration";

    package = mkOption {
      type = types.package;
      default = fcitx5-lotus;
      defaultText = literalExpression "inputs.self.packages.\${pkgs.stdenv.hostPlatform.system}.fcitx5-lotus";
      description = "The fcitx5-lotus package to install.";
    };
  };

  config = mkIf cfg.enable {
    i18n.inputMethod.fcitx5.addons = [ cfg.package ];

    users.users.uinput_proxy = {
      isSystemUser = true;
      group = "input";
    };

    services.udev.packages = [ cfg.package ];
    systemd.packages = [ cfg.package ];

    systemd.targets.multi-user.wants = [ "fcitx5-lotus-server.service" ];
  };
}
