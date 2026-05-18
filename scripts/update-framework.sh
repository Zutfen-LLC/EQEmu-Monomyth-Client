#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TARGET_DIR=${1:-.}
TARGET_DIR=$(cd -- "$TARGET_DIR" && pwd)

copy_force() {
  local src_rel="$1"
  local dst_rel="$2"
  local src="$SOURCE_DIR/$src_rel"
  local dst="$TARGET_DIR/$dst_rel"
  mkdir -p "$(dirname "$dst")"
  cp "$src" "$dst"
  echo "updated: $dst_rel"
}

if [[ "$SOURCE_DIR" == "$TARGET_DIR" ]]; then
  echo "refusing to run in-place against the framework repo itself" >&2
  exit 1
fi

copy_force .agents/README.md .agents/README.md
copy_force .agents/templates/mission-template.md .agents/templates/mission-template.md
copy_force .agents/templates/recon-brief-template.md .agents/templates/recon-brief-template.md
copy_force .agents/templates/codex-brief-template.md .agents/templates/codex-brief-template.md
copy_force .agents/templates/review-template.md .agents/templates/review-template.md
copy_force .agents/templates/spec-template.md .agents/templates/spec-template.md
copy_force docs/process/workflow-overview.md docs/process/workflow-overview.md
copy_force docs/process/local-policy-template.md docs/process/local-policy-template.md
copy_force worktrees/README.md worktrees/README.md
copy_force scripts/bootstrap.sh scripts/bootstrap.sh
copy_force scripts/update-framework.sh scripts/update-framework.sh
copy_force CHANGELOG.md CHANGELOG.md
copy_force framework-managed-paths.txt framework-managed-paths.txt

echo
echo "Framework update complete."
echo "Preserved local state under .agents/state, .agents/briefs, .agents/reviews, docs/specs, docs/recon, and docs/process/local-policy.md."
