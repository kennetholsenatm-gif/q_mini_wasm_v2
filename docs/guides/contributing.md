# Contributing Guide

## Process

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/name`
3. Make changes
4. Ensure CI passes
5. Submit a pull request

## Code Standards

- C++17 standard
- Headers use `#pragma once`
- Namespace: `q_mini_wasm_v2::`
- All public APIs must have doc comments

## Documentation Standards

Follow Cognitive Ergonomics principles:

- Line length ≤ 75 characters
- Paragraphs ≤ 4 lines
- Headers every ±200 words
- Code blocks ≤ 15 lines
- Navigation depth ≤ 3 levels

## Commit Messages

```
type: short description

Longer explanation if needed.
```

Types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`

## Testing

All changes must pass:

```bash
ctest --output-on-failure