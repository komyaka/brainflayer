# Compatibility Notes (GitHub Copilot Web)

These agent profiles are written for GitHub Copilot custom agents in `.github/agents/`.

Key constraints:
- Delegation uses `runSubagent()` (Copilot agent-to-agent calling).
- Avoid tool-specific directives like `tools:` lists or `task()` calls from other environments.
- Repository-wide rules live in `.github/copilot-instructions.md`.
