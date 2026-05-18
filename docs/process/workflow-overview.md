# Workflow Overview

This framework is designed for centrally managed orchestration patterns that can
be deployed into many repositories without carrying project-specific state.

## Roles

- PM: owns mission framing, scope control, worker interfaces, and acceptance
- Recon: performs evidence gathering, archaeology, and factual narrowing
- Builder: implements bounded tasks using PM-authored specs and briefs
- Reviewer: evaluates spec compliance, tests, risk, and overbuild

## Artifact flow

1. Create `.agents/state/mission.md` from `.agents/templates/mission-template.md`
2. Create a recon brief in `.agents/briefs/`
3. Write factual recon output to `docs/recon/`
4. Distill recon into a builder-safe implementation spec in `docs/specs/`
5. Create a narrow builder brief in `.agents/briefs/`
6. Create a dedicated git branch and optional worktree for implementation
7. Review the result with a file in `.agents/reviews/`

## Shared vs local ownership

Shared framework files should stay close to upstream so improvements can be
rolled out across repositories. Local mission state and work products should be
owned by the target repository.

Shared framework files:
- `.agents/README.md`
- `.agents/templates/*`
- `docs/process/workflow-overview.md`
- `docs/process/local-policy-template.md`
- `worktrees/README.md`
- bootstrap/update scripts
- `CHANGELOG.md`
- `framework-managed-paths.txt`

Local repo files:
- `.agents/state/mission.md`
- `.agents/briefs/*.md`
- `.agents/reviews/*.md`
- `docs/specs/*.md`
- `docs/recon/*.md`
- `docs/process/local-policy.md`
- project-specific design, handoff, and intervention-map docs

## Update policy

When refreshing from upstream:
- update framework-managed files automatically
- do not overwrite mission state or work products without explicit force
- move repo-specific workflow rules into `docs/process/local-policy.md`
- prefer additive migration notes when the framework structure changes

## Suggested branch/worktree convention

- one task -> one branch -> one worktree
- use branch names like `codex/task-001-short-name`
- use worktree paths like `worktrees/task-001-short-name`
- do not reuse a dirty worktree for another task
