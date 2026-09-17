{
  description = "C++ development environment for OBCX";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    clang-p2996 = {
      url = "github:Bloomberg/clang-p2996/p2996";
      flake = false;
    };
  };

  outputs =
    {
      nixpkgs,
      clang-p2996,
      ...
    }:
    let
      linuxSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      darwinSystems = [ "aarch64-darwin" ];
      supportedSystems = linuxSystems ++ darwinSystems;

      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      pkgsFor = system: import nixpkgs { inherit system; };
    in
    {
      formatter = forAllSystems (
        system:
        let
          pkgs = pkgsFor system;
        in
        pkgs.writeShellApplication {
          name = "treefmt";
          runtimeInputs = [
            pkgs.clang-tools
            pkgs.treefmt
          ];
          text = ''
            exec ${pkgs.treefmt}/bin/treefmt "$@"
          '';
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = pkgsFor system;
          clangP2996Source = clang-p2996 // {
            passthru = {
              owner = "Bloomberg";
              repo = "clang-p2996";
              rev = clang-p2996.rev or "p2996";
            };
          };
          # Rebuild clangd from Bloomberg's LLVM 21 fork so it understands
          # the C++26 reflection syntax used by the GCC 16 build.
          clangP2996BasePackages = pkgs.llvmPackages_21.override {
            monorepoSrc = clangP2996Source;
            version = "21.0.0";
          };
          clangP2996Packages = clangP2996BasePackages.overrideScope (
            final: prev: {
              libllvm =
                (prev.libllvm.override {
                  buildLlvmPackages = final;
                }).overrideAttrs
                  {
                    # The fork's llvm-exegesis CPU-pinning tests are host-sensitive
                    # and are not required for the clangd development tool.
                    doCheck = false;
                  };
              libclang = prev.libclang.override {
                buildLlvmPackages = final;
                libllvm = final.libllvm;
              };
            }
          );
          clangTools = clangP2996Packages.clang-tools;
          mkDevShell =
            { includeClangTools }:
            if pkgs.stdenv.isDarwin then
              pkgs.mkShell {
                packages =
                  with pkgs;
                  [
                    cmake
                    ninja
                    git
                    pkg-config
                    cmake-format
                    doxygen
                    ffmpeg
                    openspec
                    treefmt
                  ]
                  ++ pkgs.lib.optional includeClangTools clangTools;

                shellHook = ''
                  echo "OBCX macOS shell: tooling only; native builds require Linux GCC 16.1+ reflection."
                  export CC=clang
                  export CXX=clang++
                '';
              }
            else
              let
                stdenv = pkgs.gcc16Stdenv;
                # clangd must query the same GCC 16 wrapper that generated
                # compile_commands.json; otherwise it falls back to an older
                # libstdc++ search path where the C++26 <meta> header is absent.
                clangToolsWithGccQuery = pkgs.symlinkJoin {
                  name = "obcx-clang-tools-${clangP2996Packages.clang.version}";
                  paths = [ clangTools ];
                  nativeBuildInputs = [ pkgs.makeWrapper ];
                  postBuild = ''
                    rm -f "$out/bin/clangd"
                    makeWrapper ${clangTools}/bin/clangd "$out/bin/clangd" \
                      --add-flags "--query-driver=${stdenv.cc}/bin/g++,${stdenv.cc.cc}/bin/g++"
                  '';
                };
                obcxDependencies = with pkgs; [
                  boost
                  brotli
                  curl
                  fmt
                  zlib
                  gtest
                  nlohmann_json
                  openspec
                  openssl
                  spdlog
                  sqlite
                  tomlplusplus
                  ftxui
                  libxml2
                  re2
                  zstd
                  liburing
                  stdenv.cc.cc.lib
                ];
              in
              pkgs.mkShell.override { inherit stdenv; } {
                nativeBuildInputs =
                  with pkgs;
                  [
                    cmake
                    ninja
                    git
                    gcc16
                    binutils
                    pkg-config
                    cmake-format
                    doxygen
                    ffmpeg
                    perf
                    treefmt
                  ]
                  ++ pkgs.lib.optional includeClangTools clangToolsWithGccQuery;

                buildInputs = obcxDependencies;

                shellHook = ''
                  export CC=gcc
                  export CXX=g++
                '';
              };
        in
        {
          default = mkDevShell { includeClangTools = true; };
          # CI needs GCC reflection support, not the source-built LLVM/clangd.
          ci = mkDevShell { includeClangTools = false; };
        }
      );
    };
}
