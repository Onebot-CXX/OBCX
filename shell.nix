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
    ffmpeg
  ];

  buildInputs = obcxDependencies;

  shellHook = ''
    export NIX_ENFORCE_PURITY=0
  '';
}
