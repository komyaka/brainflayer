## Русская версия

Этот README переведён на русский язык. Если есть расхождения, приоритет у логики в `.github/copilot-instructions.md` и профилях агентов.

---

# Agent — 10-Agent AI Coding System

A production-ready multi-agent system for GitHub Copilot that writes, fixes, refactors, and reviews code across all programming languages and project types (web, desktop, embedded, industrial software).

---

## Quick Start

Copy the template from [`prompt.md`](./prompt.md), fill in your task, and hand it to the **Orchestrator** agent in GitHub Copilot Chat.

---

## Architecture

All agents live in [`.github/agents/`](./.github/agents/). The single source of rules is [`.github/copilot-instructions.md`](./.github/copilot-instructions.md). The single source of task truth is [`STATUS.md`](./STATUS.md).

### The 10 Агенты

| # | Agent | Role | Write-Zone |
|---|---|---|---|
| 1 | **orchestrator** | Master coordinator; manages phases, gates, REDO cycles | `STATUS.md` (coordination sections) |
| 2 | **issue-analyst** | Debug detective; reproduces bugs and identifies root cause before any fix | `STATUS.md` (REPRO, ROOT CAUSE) |
| 3 | **architect** | Scope & design authority; sets boundaries, interfaces, and acceptance criteria | `STATUS.md` (SCOPE, DESIGN) |
| 4 | **coder** | Implements features, fixes, and refactors per approved design | Source code + tests in scope |
| 5 | **refactor** | Safe structural improvements without behaviour change | Source code in scope |
| 6 | **qa** | Independent test plan and regression coverage | `STATUS.md` (TEST PLAN) + test files (if authorised) |
| 7 | **security** | Threat model, input validation, auth/authz, secrets, dependencies | `STATUS.md` (SECURITY REVIEW) |
| 8 | **performance** | Complexity, hot paths, memory/latency analysis | `STATUS.md` (PERF REVIEW) |
| 9 | **dx-ci** | Build, CI pipelines, linters, formatters, tooling | CI/build config files |
| 10 | **docs** | README, API docs, changelog, migration notes, run steps | Documentation files |
| + | **auditor** | Final independent verification; always the last agent | `STATUS.md` (AUDIT) |

> **Note:** Auditor is always active as the final gate — it is logically the 11th participant but always present.

---

## Workflow Patterns

### Fast-path (small change, ≤2 files)
```
Orchestrator → Coder → Auditor
```


### Bug-path (crash / regression / unclear root cause)
```
Orchestrator → Issue Analyst → Coder → [QA] → Auditor
```


### Feature-path (new functionality)
```
Orchestrator → Architect → Coder → QA → [Docs] → Auditor
```


### Modernization-path (refactor / cleanup)
```
Orchestrator → Architect → Refactor → QA → Auditor
```


Add `Security`, `Performance`, or `DX-CI` to any path when their triggers apply.

---

## Key Design Decisions

### Fix: Subagents Not Picking Up Instructions

Every `task()` call from Orchestrator **must** include the full `## GUARDRAILS` block (verbatim content of `.github/copilot-instructions.md`). Every subagent validates this on receipt and returns `STATUS: REDO` if it is missing.

### Single Source of Truth

`STATUS.md` is the canonical coordination artifact. Every agent writes only to its designated sections.

### No Parallel Агенты

Агенты execute sequentially. No two agents modify the same file category at the same time.

---

## Supported Languages & Stacks

Python · JavaScript/TypeScript · Go · Rust · C# · Java · PHP · Ruby · C/C++ · and more.

See the [Command Matrix](./.github/copilot-instructions.md) for fallback build/test/lint commands per language.

---

## Files

| File | Purpose |
|---|---|
| `.github/copilot-instructions.md` | GUARDRAILS — universal rules for all agents |
| `.github/agents/*.agent.md` | Agent profiles (role, write-zone, checklist) |
| `STATUS.md` | Task state — единственный источник истины |
| `prompt.md` | Universal task prompt template |
| `chatgpt_agent/` | Reference профили агентов for ChatGPT / non-Copilot environments |
