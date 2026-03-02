{
  description = "C++ development environment for OBCX";

  # Use the nixos-unstable channel for up-to-date packages,
  # or change to "github:NixOS/nixpkgs/nixos-23.11" for stable.
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      # Define the systems you want to support
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      # Helper function to generate attributes for each system
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };

          obcxDependencies = with pkgs; [
            boost
            fmt
            gtest
            nlohmann_json
            openssl
            spdlog
            sqlite
            tomlplusplus
            ftxui
            stdenv.cc.cc.lib
          ];
        in
        {
          default = pkgs.mkShell.override { stdenv = pkgs.clangStdenv; } {
            nativeBuildInputs = with pkgs; [
              cmake
              ninja
              lld
              gettext
              cmake-format
              ffmpeg
            ];

            buildInputs = obcxDependencies;

            shellHook = ''
              export NIX_ENFORCE_PURITY=0

              # Optional: Uncomment below if CLion still throws Wayland HeadlessExceptions
              # export DISPLAY=''${DISPLAY:-:0}
              # export WAYLAND_DISPLAY=''${WAYLAND_DISPLAY:-wayland-0}
            '';
          };
        }
      );
    };
}
