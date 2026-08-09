# skills

Sharable [Claude Code](https://claude.com/claude-code) skills for
working with AmiPilot from other projects.

## Installing a skill into another project

Copy (or symlink) the skill's directory into the consuming project's
`.claude/skills/`:

```sh
mkdir -p /path/to/your-project/.claude/skills
cp -r skills/amipilot /path/to/your-project/.claude/skills/
```

Or install it once for every project on the machine:

```sh
cp -r skills/amipilot ~/.claude/skills/
```

Claude Code picks the skill up automatically on the next session; the
frontmatter `description` in each `SKILL.md` is what triggers it, so no
further wiring is needed. The skill documents the host-side `amipilot`
Python package's public API — if that API changes, update the skill in
the same PR.

## Available skills

- **amipilot** — drive/test/inspect classic AmigaOS GUIs from a host
  project: connecting to `AmiPilotServer`, the `Amipilot` client API,
  the pytest fixture, the `amipilot dump` inspector CLI, and the
  platform's honest limits.
