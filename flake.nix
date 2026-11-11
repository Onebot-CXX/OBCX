{
  description = "C++ development environment for OBCX";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
      ];

      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      pkgsFor =
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          stdenv = pkgs.gcc16Stdenv;
          # Keep tools on the binary cache while sharing libstdc++ across the
          # ABI-facing dependencies and package-provided workspace shells.
          gccLibraries = rec {
            gtest = pkgs.gtest.override { inherit stdenv; };
            gbenchmark = pkgs.gbenchmark.override { inherit stdenv gtest; };
            catch2_3 = pkgs.catch2_3.override { inherit stdenv; };
            abseil-cpp = pkgs.abseil-cpp.override {
              abseil-cpp_202608 =
                (pkgs.abseil-cpp_202608.override { inherit stdenv gtest; }).overrideAttrs
                  (old: {
                    # GCC does not add the SDK framework include directory itself.
                    env = (old.env or { }) // {
                      NIX_CFLAGS_COMPILE = "-F${pkgs.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks";
                    };
                  });
            };
            boost =
              (pkgs.boost.override {
                inherit stdenv;
                # OBCX uses Asio headers, Thread, and Program_options, not Context.
                extraB2Args = [
                  "--with-thread"
                  "--with-program_options"
                ];
              }).overrideAttrs
                (old: {
                  preConfigure =
                    builtins.replaceStrings
                      [ "using clang-darwin" "-rpath $out/lib/" ]
                      [ "using gcc" "-Wl,-rpath,$out/lib/" ]
                      old.preConfigure;
                  buildPhase = builtins.replaceStrings [ "--without-python" ] [ "" ] old.buildPhase;
                  installPhase = builtins.replaceStrings [ "--without-python" ] [ "" ] old.installPhase;
                });
            fmt = (pkgs.fmt.override { inherit stdenv; }).overrideAttrs (old: {
              # GCC's Darwin isnan raises FE_INVALID for signaling NaNs.
              preCheck = (old.preCheck or "") + ''
                export GTEST_FILTER=-float_test.isnan
              '';
            });
            spdlog = pkgs.spdlog.override { inherit stdenv fmt catch2_3; };
            tomlplusplus = pkgs.tomlplusplus.override { inherit stdenv; };
            ftxui = pkgs.ftxui.override { inherit stdenv gtest gbenchmark; };
            re2 = pkgs.re2.override {
              inherit
                stdenv
                abseil-cpp
                gtest
                gbenchmark
                ;
            };
          };
        in
        pkgs // pkgs.lib.optionalAttrs (system == "aarch64-darwin") gccLibraries;
      # `nix fmt` always carries clang-format. Dev shells carry it only with the
      # other clang tools, so the CI shell does not grow a clang-tools closure.
      mkTreefmt =
        pkgs: includeClangTools:
        pkgs.writeShellApplication {
          name = "treefmt";
          runtimeInputs = [
            pkgs.nixfmt
            pkgs.taplo
            pkgs.treefmt
          ]
          ++ pkgs.lib.optional includeClangTools pkgs.clang-tools;
          text = ''
            exec ${pkgs.treefmt}/bin/treefmt "$@"
          '';
        };
    in
    {
      formatter = forAllSystems (system: mkTreefmt (pkgsFor system) true);

      devShells = forAllSystems (
        system:
        let
          pkgs = pkgsFor system;
          clangTools = pkgs.clang-tools;
          # The launcher resolves packages.toml before evaluation. An explicit
          # impure input is needed because local packages are not tracked here.
          workspaceInputs =
            let
              raw = builtins.getEnv "OBCX_NIX_SHELL_INPUTS";
              selection = builtins.fromJSON raw;
            in
            if raw == "" then
              throw "Use scripts/nix_develop.py to enter an OBCX workspace shell (requires --impure)."
            else if selection.system != system then
              throw "OBCX_NIX_SHELL_INPUTS system does not match this Nix shell."
            else
              map (
                entry:
                let
                  path = builtins.toPath entry.path;
                  shell = import path { inherit pkgs; };
                in
                if !pkgs.lib.isDerivation shell then
                  throw "${entry.id}: shell.nix must return a mkShell derivation."
                else
                  # Only dependencies are inherited, not actor hooks or variables.
                  builtins.removeAttrs shell [ "shellHook" ]
              ) selection.shells;
          # Pure flake evaluation cannot see ignored workspace files. Resolve
          # them at shell entry, including nix develop --command and direnv.
          autoWorkspaceHook =
            { includeClangTools }:
            ''
              obcx_resolve_workspace() {
                local root="$PWD"
                while [[ "$root" != / ]]; do
                  if [[ -f "$root/flake.nix" && -f "$root/cmake/package_tool.py" ]]; then
                    break
                  fi
                  root="$(dirname "$root")"
                done
                if [[ -e "$root/packages.toml" || -L "$root/packages.toml" ]]; then
                  if ! ${pkgs.python3}/bin/python3 ${./.}/scripts/nix_develop.py \
                    --workspace "$root/packages.toml" \
                    --cache "$root/build/actor-dev/package-state/sources" \
                    --mode development --network allow \
                    --nix-system ${system} \
                    --shell ${if includeClangTools then "workspace" else "workspace-ci"} \
                    --print-env; then
                    echo "OBCX: failed to load workspace Nix dependencies." >&2
                    return 1
                  fi
                fi
              }
              obcx_workspace_environment="$(obcx_resolve_workspace)" || exit 1
              eval "$obcx_workspace_environment"
              unset obcx_workspace_environment
              unset -f obcx_resolve_workspace
            '';
          mkDevShell =
            {
              includeClangTools,
              packageShells,
              autoWorkspace,
            }:
            let
              stdenv = pkgs.gcc16Stdenv;
              # nixpkgs marks perf and liburing unavailable on some systems.
              whenAvailable = pkg: pkgs.lib.optional (pkg.meta.available or false) pkg;
              # clangd must query the same GCC 16 wrapper that generated
              # compile_commands.json; otherwise it falls back to an older
              # libstdc++ search path where the C++26 <meta> header is absent.
              clangToolsWithGccQuery = pkgs.symlinkJoin {
                name = "obcx-clang-tools-${clangTools.version}";
                paths = [ clangTools ];
                nativeBuildInputs = [ pkgs.makeWrapper ];
                postBuild = ''
                  rm -f "$out/bin/clangd"
                  makeWrapper ${clangTools}/bin/clangd "$out/bin/clangd" \
                    --add-flags "--query-driver=${stdenv.cc}/bin/g++,${stdenv.cc.cc}/bin/g++"
                '';
              };
              obcxDependencies =
                with pkgs;
                [
                  boost
                  brotli
                  curl
                  fmt
                  zlib
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
                  # Optional diagnostic builds need jemalloc.pc and jeprof.
                  # CMake links it only when profiling is explicitly enabled.
                  jemalloc
                  stdenv.cc.cc.lib
                ]
                ++ whenAvailable pkgs.liburing;
            in
            pkgs.mkShell.override { inherit stdenv; } {
              inputsFrom = packageShells;
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
                ]
                ++ pkgs.lib.optionals pkgs.stdenv.hostPlatform.isLinux [
                  patchelf
                ]
                ++ whenAvailable pkgs.perf
                ++ [ (mkTreefmt pkgs includeClangTools) ]
                ++ pkgs.lib.optional includeClangTools clangToolsWithGccQuery;

              buildInputs = obcxDependencies;

              shellHook = ''
                ${pkgs.lib.optionalString autoWorkspace (autoWorkspaceHook {
                  inherit includeClangTools;
                })}
                export CC=gcc
                export CXX=g++
                ${pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isDarwin ''
                  # Host Homebrew LLVM flags would link its libunwind/libc++ into GCC builds.
                  unset LDFLAGS CPPFLAGS
                ''}
              '';
            };
        in
        {
          default = mkDevShell {
            includeClangTools = true;
            packageShells = [ ];
            autoWorkspace = true;
          };
          # CI needs GCC reflection support, not clang tooling.
          ci = mkDevShell {
            includeClangTools = false;
            packageShells = [ ];
            autoWorkspace = true;
          };
        }
        // pkgs.lib.optionalAttrs (builtins.getEnv "OBCX_NIX_SHELL_INPUTS" != "") {
          workspace = mkDevShell {
            includeClangTools = true;
            packageShells = workspaceInputs;
            autoWorkspace = false;
          };
          workspace-ci = mkDevShell {
            includeClangTools = false;
            packageShells = workspaceInputs;
            autoWorkspace = false;
          };
        }
      );
    };
}
