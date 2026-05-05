# Frontend Development Guidelines

> Best practices for frontend development in this project.

---

## Status: Not Applicable

> **SmartHelm is firmware-only**. There is no frontend codebase in this repository. Browser-side dashboards or mini-programs (if any) live downstream and consume the M100PG telemetry/command protocol documented in `APP/m100pg_protocol.h` and the project README.
>
> The files in this directory are leftover Trellis scaffolding. **Do not fill them in unless a real frontend is added to this repo.** Backend (firmware) guidelines under `../backend/` are the only authoritative spec.

---

## Overview

This directory contains guidelines for frontend development. Fill in each file with your project's specific conventions.

---

## Guidelines Index

| Guide | Description | Status |
|-------|-------------|--------|
| [Directory Structure](./directory-structure.md) | Module organization and file layout | To fill |
| [Component Guidelines](./component-guidelines.md) | Component patterns, props, composition | To fill |
| [Hook Guidelines](./hook-guidelines.md) | Custom hooks, data fetching patterns | To fill |
| [State Management](./state-management.md) | Local state, global state, server state | To fill |
| [Quality Guidelines](./quality-guidelines.md) | Code standards, forbidden patterns | To fill |
| [Type Safety](./type-safety.md) | Type patterns, validation | To fill |

---

## How to Fill These Guidelines

For each guideline file:

1. Document your project's **actual conventions** (not ideals)
2. Include **code examples** from your codebase
3. List **forbidden patterns** and why
4. Add **common mistakes** your team has made

The goal is to help AI assistants and new team members understand how YOUR project works.

---

**Language**: All documentation should be written in **English**.
