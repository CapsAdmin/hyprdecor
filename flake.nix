{
  description = "Window decorations plugin for Hyprland";

  inputs = {
    hyprland.url = "github:hyprwm/Hyprland";
    nixpkgs.follows = "hyprland/nixpkgs";
    systems.follows = "hyprland/systems";
  };

  outputs = {
    self,
    hyprland,
    nixpkgs,
    systems,
    ...
  }: let
    inherit (nixpkgs) lib;
    eachSystem = lib.genAttrs (import systems);

    pkgsFor = eachSystem (system:
      import nixpkgs {
        localSystem.system = system;
        overlays = [hyprland.overlays.hyprland-packages];
      });
  in {
    packages = eachSystem (system: let
      pkgs = pkgsFor.${system};
    in {
      default = self.packages.${system}.hyprdecor;
      hyprdecor = pkgs.hyprlandPlugins.mkHyprlandPlugin {
        pluginName = "hyprdecor";
        version = "0.1";
        src = ./.;

        inherit (pkgs.hyprland) nativeBuildInputs;
        buildInputs = [];

        meta = with lib; {
          homepage = "https://github.com/CapsAdmin/hyprdecor";
          description = "Hyprland customizable window decorations";
          license = licenses.bsd3;
          platforms = platforms.linux;
        };
      };
      
      hyprdecor-debug = pkgs.hyprlandPlugins.mkHyprlandPlugin {
        pluginName = "hyprdecor";
        version = "0.1-debug";
        src = ./.;

        inherit (pkgs.hyprland) nativeBuildInputs;
        buildInputs = [];
        
        cmakeFlags = ["-DCMAKE_BUILD_TYPE=Debug"];
        dontStrip = true;

        meta = with lib; {
          homepage = "https://github.com/CapsAdmin/hyprdecor";
          description = "Hyprland customizable window decorations (debug build)";
          license = licenses.bsd3;
          platforms = platforms.linux;
        };
      };
    });

    apps = eachSystem (system: {
      default = {
        type = "app";
        program = toString (pkgsFor.${system}.writeShellScript "run-hyprland" ''
          # Build the plugin first
          ${pkgsFor.${system}.nix}/bin/nix build ${self}#hyprdecor --out-link ./result
          
          # Run Hyprland with the test config
          export GDK_SCALE=1
          export QT_SCALE_FACTOR=1
          ${hyprland.packages.${system}.hyprland}/bin/Hyprland --config ./hyprland.conf
        '');
      };
      
      debug = {
        type = "app";
        program = toString (pkgsFor.${system}.writeShellScript "debug-hyprland" ''
          # Build the plugin with debug symbols (as 'result' so the config works)
          ${pkgsFor.${system}.nix}/bin/nix build ${self}#hyprdecor-debug --out-link ./result
          
          # Find the real Hyprland binary (unwrapped)
          HYPR_BIN="${hyprland.packages.${system}.hyprland}/bin/Hyprland"
          
          if file "$HYPR_BIN" | grep -q "script"; then
            WRAPPED="$(dirname "$HYPR_BIN")/.Hyprland-wrapped"
            if [ -f "$WRAPPED" ]; then
              echo "Detected Nix wrapper, using: $WRAPPED"
              HYPR_BIN="$WRAPPED"
            fi
          fi
          
          echo "Starting GDB with: $HYPR_BIN"
          export GDK_SCALE=1
          export QT_SCALE_FACTOR=1
          ${pkgsFor.${system}.gdb}/bin/gdb -ex run --args "$HYPR_BIN" --config ./hyprland.conf
        '');
      };
    });

    devShells = eachSystem (system: {
      default = pkgsFor.${system}.mkShell.override {stdenv = pkgsFor.${system}.gcc14Stdenv;} {
        name = "hyprdecor";
        buildInputs = with pkgsFor.${system}; [
          hyprland.packages.${system}.hyprland
        ];
        inputsFrom = [hyprland.packages.${system}.hyprland];
      };
    });
  };
}
