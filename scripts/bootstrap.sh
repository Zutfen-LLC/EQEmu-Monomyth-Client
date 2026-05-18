#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TARGET_DIR=${1:-.}
TARGET_DIR=$(cd -- "$TARGET_DIR" && pwd)
FORCE=${FORCE:-0}

copy_file() {
  local src_rel="$1"
  local dst_rel="$2"
  local src="$SOURCE_DIR/$src_rel"
  local dst="$TARGET_DIR/$dst_rel"
  mkdir -p "$(dirname "$dst")"

  if [[ -e "$dst" && "$FORCE" != "1" ]]; then
    echo "skip existing: $dst_rel"
    return
  fi

  cp "$src" "$dst"
  echo "copied: $dst_rel"
}

ensure_dir() {
  mkdir -p "$TARGET_DIR/$1"
  echo "ensured dir: $1"
}

if [[ ! -d "$TARGET_DIR/.git" ]]; then
  echo "warning: target does not appear to be a git repo: $TARGET_DIR" >&2
fi

ensure_dir .agents/templates
ensure_dir .agents/state
ensure_dir .agents/briefs
ensure_dir .agents/reviews
ensure_dir docs/process
ensure_dir docs/specs
ensure_dir docs/recon
ensure_dir worktrees
ensure_dir scripts

copy_file .agents/README.md .agents/README.md
copy_file .agents/templates/mission-template.md .agents/templates/mission-template.md
copy_file .agents/templates/recon-brief-template.md .agents/templates/recon-brief-template.md
copy_file .agents/templates/codex-brief-template.md .agents/templates/codex-brief-template.md
copy_file .agents/templates/review-template.md .agents/templates/review-template.md
copy_file .agents/templates/spec-template.md .agents/templates/spec-template.md
copy_file docs/process/workflow-overview.md docs/process/workflow-overview.md
copy_file docs/process/local-policy-template.md docs/process/local-policy-template.md
copy_file worktrees/README.md worktrees/README.md
copy_file scripts/bootstrap.sh scripts/bootstrap.sh
copy_file scripts/update-framework.sh scripts/update-framework.sh
copy_file CHANGELOG.md CHANGELOG.md
copy_file framework-managed-paths.txt framework-managed-paths.txt

if [[ ! -e "$TARGET_DIR/.agents/state/mission.md" ]]; then
  cp "$SOURCE_DIR/.agents/templates/mission-template.md" "$TARGET_DIR/.agents/state/mission.md"
  echo "created starter mission: .agents/state/mission.md"
else
  echo "preserved existing: .agents/state/mission.md"
fi

if [[ ! -e "$TARGET_DIR/docs/process/local-policy.md" ]]; then
  cp "$SOURCE_DIR/docs/process/local-policy-template.md" "$TARGET_DIR/docs/process/local-policy.md"
  echo "created local policy stub: docs/process/local-policy.md"
else
  echo "preserved existing: docs/process/local-policy.md"
fi

for path in   .agents/briefs/.gitkeep   .agents/reviews/.gitkeep   .agents/state/.gitkeep   docs/recon/.gitkeep   docs/specs/.gitkeep
 do
  if [[ ! -e "$TARGET_DIR/$path" ]]; then
    : > "$TARGET_DIR/$path"
    echo "created placeholder: $path"
  fi
done

echo
echo "Bootstrap complete."
echo "Next steps:"
echo "1. Edit .agents/state/mission.md"
echo "2. Move repo-specific workflow rules into docs/process/local-policy.md"
echo "3. Create .agents/briefs/recon-001.md from the recon template"
echo "4. Create docs/recon/ and docs/specs/ artifacts as work progresses"
