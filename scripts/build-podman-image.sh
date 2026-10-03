#!/usr/bin/env sh
set -eu

# This is a refusal, not an alternate builder or a compatibility mode.
message='Container packaging is unavailable after package-v2 cutover; see packaging/podman/README.md.'
if [ "$#" -eq 1 ] && { [ "$1" = '--help' ] || [ "$1" = '-h' ]; }; then
  printf '%s\n' "$message"
  exit 0
fi
printf '%s\n' "$message" >&2
exit 2
