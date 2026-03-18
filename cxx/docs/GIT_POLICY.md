# Git Policy

This document is the canonical reference for all Git workflow rules in this project. The rules here take precedence over any conflicting guidance elsewhere.

---

## Branch Naming

All feature and task branches **MUST** use a timestamp prefix in the branch name. An optional descriptive suffix may follow the timestamp, separated by a hyphen.

```
yyyymmddhhmm
yyyymmddhhmm-descriptive-suffix
```

**Examples:** `202603151042`, `202603151042-phase21-smoothed-param`

- Timestamp prefix is mandatory. No branch may omit it.
- Descriptive suffix is optional and for human readability only. It carries no semantic meaning in tooling.
- One branch per task or work session.

---

## Starting a New Task

Before creating any branch:

```bash
git checkout main
git pull origin main
git branch --show-current   # confirm you are on main
git checkout -b yyyymmddhhmm
```

All branches **MUST** originate from a clean, up-to-date `main`. Never branch from another feature branch.

---

## Commit Standards

This project uses [Conventional Commits](https://www.conventionalcommits.org/).

| Prefix | Use for |
|--------|---------|
| `feat:` | New feature or behaviour |
| `fix:` | Bug fix |
| `refactor:` | Code restructure without behaviour change |
| `chore:` | Build, tooling, or dependency changes |
| `docs:` | Documentation-only changes |
| `test:` | Adding or correcting tests |

For multi-line messages use multiple `-m` flags to preserve newlines in all shells:

```bash
git commit -m "feat: add wavetable interpolation" \
           -m "Adds cubic interpolation to WavetableOscillatorProcessor." \
           -m "Resolves pitch drift at low frequencies."
```

When using a HEREDOC for multi-line messages:

```bash
git commit -m "$(cat <<'EOF'
feat: add wavetable interpolation

Adds cubic interpolation to WavetableOscillatorProcessor.
Resolves pitch drift at low frequencies.
EOF
)"
```

---

## Pushing

Always push with `-u` to set the upstream tracking branch:

```bash
git push -u origin <branch_name>
```

Use the remote alias `origin`. If a push fails because the remote does not exist, verify with `git remote -v` before retrying.

**Never push directly to `main`.**

---

## Pull Requests & Merging

1. After pushing, **manually create the PR on GitHub**.
2. Use a **squash merge** — one commit per branch on `main`.
3. PR title should follow Conventional Commits format (e.g. `feat: implement chorus bypass`).

> After pushing, remind yourself: *"Go to GitHub, create the PR, and perform a squash merge."*

---

## Post-Merge Cleanup

After the squash merge is confirmed:

1. Delete the remote branch on GitHub (keeps the repo clean).
2. Sync your local `main`:

```bash
git checkout main
git pull origin main
```

---

## Documentation Sync Rule

Whenever a function is added to or removed from `include/CInterface.h`, `docs/BRIDGE_GUIDE.md` **MUST** be updated in the same commit. The bridge guide is the binary contract for all host integrations (Swift, .NET, C++ GUIs).

---

## Summary Checklist

```
[ ] git checkout main && git pull origin main
[ ] git checkout -b yyyymmddhhmm[-descriptive-suffix]
[ ] work, commit using Conventional Commits
[ ] git push -u origin <branch>
[ ] create PR on GitHub → squash merge
[ ] delete remote branch
[ ] git checkout main && git pull origin main
```
