{
  description = "C++ development environment for OBCX";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
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
              throw "OBCX workspace platform does not match the Nix shell system."
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
                    --mode development --network deny \
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
            if pkgs.stdenv.hostPlatform.isDarwin then
              pkgs.mkShell {
                inputsFrom = packageShells;
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
                  name = "obcx-clang-tools-${clangTools.version}";
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
                    perf
                    treefmt
                  ]
                  ++ pkgs.lib.optional includeClangTools clangToolsWithGccQuery;

                buildInputs = obcxDependencies;

                shellHook = ''
                  ${pkgs.lib.optionalString autoWorkspace (autoWorkspaceHook {
                    inherit includeClangTools;
                  })}
                  export CC=gcc
                  export CXX=g++
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
        //
          pkgs.lib.optionalAttrs
            (pkgs.stdenv.hostPlatform.isLinux && builtins.getEnv "OBCX_NIX_SHELL_INPUTS" != "")
            {
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
