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
            brotli
            fmt
            zlib
            gtest
            nlohmann_json
            openssl
            spdlog
            sqlite
            tomlplusplus
            ftxui
            libxml2
            re2
            zstd
            stdenv.cc.cc.lib
          ];
        in
        {
          default = pkgs.mkShell.override { stdenv = pkgs.clangStdenv; } {
            nativeBuildInputs = with pkgs; [
              perf
              clang-tools
              cmake
              ninja
              llvmPackages.bintools
              pkg-config
              gettext
              cmake-format
              ffmpeg
            ];

            buildInputs = obcxDependencies;

            shellHook = '''';
          };
        }
      );
    };
}
