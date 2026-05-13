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
    enable = mkEnableOption "Fcitx5 Lotus per-user server";

    package = mkOption {
      type = types.package;
      default = fcitx5-lotus;
      defaultText = literalExpression "inputs.self.packages.\${pkgs.stdenv.hostPlatform.system}.fcitx5-lotus";
      description = "The fcitx5-lotus package to use.";
    };

    user = mkOption {
      type = types.str;
      default = config.home.username;
      defaultText = literalExpression "config.home.username";
      description = "User name passed to fcitx5-lotus-server.";
    };

    serviceTarget = mkOption {
      type = types.str;
      default = "graphical-session.target";
      example = "default.target";
      description = "User systemd target that should start the Lotus server.";
    };
  };

  config = mkIf cfg.enable {
    assertions = [
      {
        assertion =
          cfg.user != ""
          && cfg.user != "multi-user"
          && (builtins.match "[A-Za-z_][A-Za-z0-9_-]*" cfg.user) != null;
        message = "services.fcitx5-lotus.user must be a real login user, not empty string, `multi-user`, or an invalid username.";
      }
    ];

    home.packages = [ cfg.package ];

    systemd.user.services.fcitx5-lotus-server = {
      Unit = {
        Description = "Fcitx5 Lotus Server for ${cfg.user}";
        After = [ cfg.serviceTarget ];
        PartOf = [ cfg.serviceTarget ];
      };

      Service = {
        ExecStart = "${cfg.package}/bin/fcitx5-lotus-server -u ${cfg.user}";
        Restart = "on-failure";
        RestartSec = 2;
        NoNewPrivileges = true;
        PrivateTmp = true;
      };

      Install = {
        WantedBy = [ cfg.serviceTarget ];
      };
    };
  };
}
