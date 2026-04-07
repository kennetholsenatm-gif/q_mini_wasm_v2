# GitHub Actions Workflows

This directory contains CI/CD workflows for the q_mini_wasm_v2 project.

## Active Workflows

### Core CI/CD
| Workflow | Trigger | Purpose |
|------------|---------|---------|
| `ci.yml` | Push/PR to main/develop | Full CI pipeline with GF(3) validation, build matrix, tests |
| `deploy.yml` | Tags (v*) | Deployment pipeline with staging and production |
| `release.yml` | Manual | Release management |
| `multi-lang-build.yml` | Workflow call | Multi-language build (C++, Rust, Go, R, DLL) |

### Quality & Security
| Workflow | Trigger | Purpose |
|------------|---------|---------|
| `gf3-integrity.yml` | Push/PR | GF(3) arithmetic integrity checks |
| `architecture-enforcement.yml` | Push/PR | Architecture compliance validation |
| `security.yml` | Push/PR/Schedule | Security scanning |

### Documentation & Research
| Workflow | Trigger | Purpose |
|------------|---------|---------|
| `docs.yml` | Push to main | Documentation generation |
| `documentation-agent.yml` | Manual/Schedule | Automated documentation improvements |
| `research-to-production-pipeline.yml` | Research docs changes | Research-to-production alignment |

### Automation
| Workflow | Trigger | Purpose |
|------------|---------|---------|
| `auto-pr-detailed.yml` | Various | Automated PR generation with details |
| `agent-recompilation.yml` | Manual/Call | Agent recompilation |
| `agent-recompilation-trigger.yml` | File changes | Trigger agent recompilation |
| `rag-cicd-orchestration.yml` | Push/PR/Schedule | RAG-powered CI/CD (fixed: upload-artifact v4) |
| `kanban-auto-commit.yml` | Schedule | Automated kanban commits |

## Removed Workflows (Cannot Pass)

The following workflows were removed because they referenced non-existent resources:

- **continuous-improvement.yml** - Referenced `agents/agentd.exe` binary that doesn't exist
- **kanban-review-fix.yml** - Called `python -m agents.cli kanban-review` which doesn't exist
- **agent-automation.yml** - Used broken secret syntax (`secrets-token` instead of `${{ secrets.XXX }}`)
- **python-rewrite-pipeline.yml** - Referenced `scripts/generate_rewrites.py` which doesn't exist
- **gemini-agent-improvement.yml** - Used wrong paths (`q_mini_wasm_v2/agents/gemini_improver` vs `agents/`)

## Workflow Status

- ✅ **Working:** Core CI/CD, quality checks, security scans
- 🔧 **Fixed:** rag-cicd-orchestration.yml (updated upload-artifact v3→v4)
- ❌ **Removed:** 5 broken workflows that could never pass

## Contributing

When adding new workflows:
1. Ensure all referenced scripts/binaries exist in the repo
2. Use correct secret syntax: `${{ secrets.SECRET_NAME }}`
3. Use latest action versions (v4 for upload-artifact/download-artifact)
4. Test locally with `act` or similar tools when possible
