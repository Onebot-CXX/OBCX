#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
actor_root=${OBCX_ACTOR_SOURCE_ROOT:-local_actor}

restore_actor_source() {
  actor=$1

  case "$actor" in
    bridge)
      repository=obcx-message-bridge
      base_revision=de8c3046c218c9e2a254abe832e91595f4cc629a
      bundle_ref=refs/heads/develop
      ;;
    message-store)
      repository=obcx-message-store
      base_revision=3a9dfc2b27375d22531b4308356b75f4bac7077f
      bundle_ref=refs/remotes/origin/main
      ;;
    registry)
      repository=obcx-actor-registry
      base_revision=057b46522872bfbc2dd87435e3751b8d2001e26b
      bundle_ref=refs/remotes/origin/codex/actor-only-runtime-cutover
      ;;
    template)
      repository=obcx-actor-template
      base_revision=993048e6d7e280167cc2189a51464d8fd9197c68
      bundle_ref=refs/remotes/origin/main
      ;;
    *)
      printf 'unknown actor source: %s\n' "$actor" >&2
      return 2
      ;;
  esac

  bundle=$script_dir/bundles/$actor.bundle
  checkout=$actor_root/$repository
  if [ -e "$checkout" ]; then
    printf 'actor checkout already exists: %s\n' "$checkout" >&2
    return 1
  fi

  git init "$checkout"
  git -C "$checkout" bundle verify "$bundle" >/dev/null
  git -C "$checkout" fetch --no-tags "$bundle" "$bundle_ref"
  git -C "$checkout" checkout --detach "$base_revision"
  test "$(git -C "$checkout" rev-parse HEAD)" = "$base_revision"
}

if [ "$#" -eq 0 ]; then
  set -- bridge message-store registry template
fi

mkdir -p "$actor_root"
for actor in "$@"; do
  restore_actor_source "$actor"
done

OBCX_ACTOR_SOURCE_ROOT=$actor_root \
  sh "$script_dir/apply-patches.sh" "$@"
