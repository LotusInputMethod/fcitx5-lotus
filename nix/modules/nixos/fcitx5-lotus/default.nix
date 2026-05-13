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

  legacyUsers = optional (cfg.user != null && cfg.user != "") cfg.user;
  effectiveUsers = unique (legacyUsers ++ cfg.users);

  invalidUsers = filter (
    user: user == "" || user == "multi-user" || (builtins.match "[A-Za-z_][A-Za-z0-9_-]*" user) == null
  ) effectiveUsers;
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

    # Backward compatible with the old module API, but no longer defaults to "".
    user = mkOption {
      type = types.nullOr types.str;
      default = null;
      example = "alice";
      description = ''
        Backward-compatible single user to start the Lotus server for.

        Prefer `services.fcitx5-lotus.users` for NixOS multi-user hosts, or enable
        `services.fcitx5-lotus` from Home Manager for per-user/session setup.
      '';
    };

    users = mkOption {
      type = types.listOf types.str;
      default = [ ];
      example = [
        "alice"
        "bob"
      ];
      description = ''
        Users to start system-level fcitx5-lotus-server instances for.

        This is useful when the host wants to manage Lotus entirely from NixOS.
        If you use the Home Manager module for each desktop user, leave this empty
        and only keep `services.fcitx5-lotus.enable = true` at the NixOS level for
        package, udev and input-method integration.
      '';
    };
  };

  config = mkIf cfg.enable {
    assertions = [
      {
        assertion = invalidUsers == [ ];
        message = "services.fcitx5-lotus.users/user must contain real login users, not empty string, `multi-user`, or invalid usernames.";
      }
    ];

    i18n.inputMethod.fcitx5.addons = [ cfg.package ];

    users.users.uinput_proxy = {
      isSystemUser = true;
      group = "input";
    };

    services.udev.packages = [ cfg.package ];
    systemd.packages = [ cfg.package ];

    systemd.targets.multi-user.wants = map (user: "fcitx5-lotus-server@${user}.service") effectiveUsers;
  };
}
