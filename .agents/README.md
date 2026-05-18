# Hybrid Agent Workflow

This repo uses a hybrid workflow:

- Hermes PM orchestrates
- Hermes Recon performs clean-room archaeology and factual extraction
- Codex implements narrow code tasks in dedicated worktrees
- Hermes Reviewer checks spec compliance and regression risk

Core rule:
- workers communicate through files, not free-form chat
- PM is the only relay between worker roles

Directory conventions:

- `.agents/state/` - mission state and PM-controlled task state
- `.agents/briefs/` - worker task briefs
- `.agents/reviews/` - review verdicts
- `.agents/templates/` - reusable artifact templates
- `docs/recon/` - recon reports and investigation outputs
- `docs/specs/` - PM-normalized implementation specs
- `worktrees/` - optional local home for task-specific git worktrees

Recommended artifact chain:

1. PM updates `.agents/state/mission.md`
2. Recon receives a brief from `.agents/briefs/recon-*.md`
3. Recon writes a factual report to `docs/recon/`
4. PM distills findings into a spec in `docs/specs/`
5. Codex receives a narrow brief from `.agents/briefs/codex-task-*.md`
6. Reviewer writes PASS or CHANGES REQUESTED to `.agents/reviews/`

Clean-room rule for this repo:
- do not pass raw decompilation or proprietary source-like output into Codex briefs
- PM should pass behavior specs, constraints, and acceptance criteria only
