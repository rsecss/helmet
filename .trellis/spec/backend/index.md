# Backend Development Guidelines

> Best practices for backend development in this project.

---

## Overview

This directory contains guidelines for backend development. Fill in each file with your project's specific conventions.

---

## Guidelines Index

| Guide | Description | Status |
|-------|-------------|--------|
| [Directory Structure](./directory-structure.md) | Module organization, naming conventions, ST7735 display contract | Maintained |
| [Database Guidelines](./database-guidelines.md) | Embedded runtime data, DMA, per-module ring buffers, scheduler/UART/voice contracts | Maintained |
| [Error Handling](./error-handling.md) | HAL/module/system error tiers, degradation strategy, common mistakes | Maintained |
| [Quality Guidelines](./quality-guidelines.md) | CI quality gate, forbidden patterns, code review checklist, biosignal algorithm rules | Maintained |
| [Logging Guidelines](./logging-guidelines.md) | USART1 printf conventions, log-level discipline, ASR mode quiet rule | Maintained |

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
