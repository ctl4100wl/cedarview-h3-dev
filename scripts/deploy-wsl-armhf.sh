#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target="${1:-x6pro}"
binary="${project_dir}/dist/cedarview-armhf"
remote_temp="/tmp/cedarview-armhf-${USER}-$$"

"${project_dir}/scripts/build-wsl-armhf.sh"

file -b "${binary}" | grep -qE 'ELF 32-bit.*ARM' || {
  echo "ERROR: Refusing to deploy a non-ARM binary." >&2
  exit 1
}

target_machine="$(ssh "${target}" 'uname -m')"
case "${target_machine}" in
  armv7l|armv8l) ;;
  *)
    echo "ERROR: ${target} reports ${target_machine}, not a 32-bit ARM target." >&2
    exit 1
    ;;
esac

scp "${binary}" "${target}:${remote_temp}"
ssh "${target}" bash -s -- "${remote_temp}" <<'REMOTE'
set -euo pipefail

incoming="$1"
destination="${HOME}/.local/bin/cedarview"
candidate="${destination}.new"
backup="${destination}.previous"
trap 'rm -f "$incoming" "$candidate"' EXIT

case "$(uname -m)" in
  armv7l|armv8l) ;;
  *)
    echo "ERROR: Deployment destination is not 32-bit ARM." >&2
    exit 1
    ;;
esac

file -b "$incoming" | grep -qE 'ELF 32-bit.*ARM' || {
  echo "ERROR: Uploaded executable is not 32-bit ARM." >&2
  exit 1
}

ldd_output="$(ldd "$incoming" 2>&1)" || {
  echo "$ldd_output" >&2
  echo "ERROR: Target dynamic-link check failed." >&2
  exit 1
}
if grep -Eq 'not found|version .* not found' <<<"$ldd_output"; then
  echo "$ldd_output" >&2
  echo "ERROR: Target libraries do not satisfy this build." >&2
  exit 1
fi

mkdir -p "${HOME}/.local/bin"
install -m 755 "$incoming" "$candidate"
if [[ -e "$destination" ]]; then
  cp -a "$destination" "$backup"
fi
mv -f "$candidate" "$destination"
rm -f "$incoming"
trap - EXIT

file "$destination"
echo "Previous binary: $backup"
REMOTE

echo
echo "Deployed the verified ARM build to ${target}:~/.local/bin/cedarview"
