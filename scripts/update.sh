#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
install_prefix="${HOME}/.local"
non_interactive=false

if [[ "${1:-}" == "--non-interactive" ]]; then
  non_interactive=true
elif [[ $# -gt 0 ]]; then
  echo "Usage: $0 [--non-interactive]" >&2
  exit 2
fi

if ! git -C "${project_dir}" rev-parse --is-inside-work-tree \
  >/dev/null 2>&1; then
  echo "CedarView was installed from an archive, not a Git checkout." >&2
  echo "Clone the CedarView repository once, then run install-app.sh." >&2
  exit 3
fi

if [[ -n "$(git -C "${project_dir}" status \
  --porcelain --untracked-files=normal)" ]]; then
  echo "Update stopped: the CedarView checkout has local changes." >&2
  echo "Commit or stash them first so an update cannot overwrite your work." >&2
  exit 4
fi

upstream="$(
  git -C "${project_dir}" rev-parse \
    --abbrev-ref --symbolic-full-name '@{upstream}' 2>/dev/null || true
)"
if [[ -z "${upstream}" ]]; then
  echo "Update stopped: the current branch has no upstream remote." >&2
  echo "Set one with: git push -u origin main" >&2
  exit 5
fi

current_commit="$(git -C "${project_dir}" rev-parse HEAD)"
git -C "${project_dir}" fetch --prune --quiet
remote_commit="$(git -C "${project_dir}" rev-parse "${upstream}")"

if [[ "${current_commit}" == "${remote_commit}" ]]; then
  if ! "${non_interactive}"; then
    echo "CedarView is already up to date."
  fi
  exit 0
fi

echo "Updating CedarView:"
git -C "${project_dir}" log \
  --oneline --no-decorate "${current_commit}..${remote_commit}"

git -C "${project_dir}" merge --ff-only "${upstream}"

# Ninja recompiles only files affected by the fetched changes.
"${project_dir}/scripts/build.sh"
cmake --install "${project_dir}/build" --prefix "${install_prefix}"
ln -sfn "${project_dir}/scripts/update.sh" \
  "${install_prefix}/bin/cedarview-update"

echo
echo "CedarView updated successfully."
echo "Restart the running app to use the new version."
