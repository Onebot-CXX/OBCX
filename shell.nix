{ pkgs ? import <nixpkgs> {} }:
let
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
pkgs.mkShell.override { stdenv = pkgs.clangStdenv; } {
  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    lld
    gettext
    cmake-format
  ];

  buildInputs = obcxDependencies;

  shellHook = ''
    export LDFLAGS="$LDFLAGS -Wl,-rpath,${pkgs.lib.makeLibraryPath obcxDependencies}"
    export NIX_ENFORCE_PURITY=0
    if [ -z "$ZSH_VERSION" ]; then
      export SHELL="zsh"
      exec zsh
    fi
    echo "Welcome to OBCX."
  '';
}
