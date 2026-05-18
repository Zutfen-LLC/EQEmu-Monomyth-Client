# Worktrees

Optional home for task-specific builder worktrees.

Example:

```bash
TASK=task-001
BRANCH=codex/$TASK
WT=worktrees/$TASK

git worktree add -b "$BRANCH" "$WT" HEAD
```

Suggested rule:
- one task -> one branch -> one worktree
- do not reuse a dirty worktree for a different task
