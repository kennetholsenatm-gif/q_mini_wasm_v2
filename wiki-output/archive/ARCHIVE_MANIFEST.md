# wiki-output/archive/ Manifest

**Archive Date:** April 21, 2026  
**Reason:** Massive documentation refactor - moving superseded research documents

---

## Archived Documents

### Research Documents (Superseded by GROUND_TRUTH.md)
These large research reports contain valuable analysis but are now superseded by the consolidated GROUND_TRUTH.md:

| Document | Original Location | Archive Reason |
|----------|------------------|----------------|
| Research-Autonomous forward Forward training plan.md | wiki-output/ | Superseded - training is stubbed |
| Research-Cognitive ergonomics model protocol research.md | wiki-output/ | Research complete, implementation pending |
| Research-Quantum ai architecture review synthesis.md | wiki-output/ | Consolidated into GROUND_TRUTH.md |
| Research-Quantum architecture review & integration proposal.md | wiki-output/ | Superseded |
| Research-Quantum betti numbers integration analysis.md | wiki-output/ | Technical research archived |
| Research-Quantum Classical system architectural review.md | wiki-output/ | Consolidated |
| Research-Quantum codebase analysis and synthesis.md | wiki-output/ | Superseded by audit reports |
| Research-Repository analysis for qgnn integration.md | wiki-output/ | Integration complete |

### System Documents
| Document | Purpose |
|----------|---------|
| DOCUMENT_CRITIQUE_TRACKER.md | Tracks 5-deep critique progress across all docs |

---

## Why These Were Archived

1. **Research Complete:** The research phase is complete - now in implementation phase
2. **Consolidated:** GROUND_TRUTH.md contains the definitive current state
3. **Historical Value:** Kept for reference but not actively maintained
4. **Reduce Clutter:** Main wiki-output/ should contain current operational docs only

---

## How to Access Archived Docs

Archived documents remain in git history and can be accessed:
```bash
git show HEAD:wiki-output/archive/Research-[name].md
```

Or browse the archive/ directory directly.

---

## Documents NOT Archived

These remain in wiki-output/ root (current operational docs):

- Home.md
- Guides-*.md (Quick Start, Building, Contributing, etc.)
- Architecture-*.md (Overview, QGNN, Expert Networks, etc.)
- API-Core Reference.md
- Diagrams-*.md
- Decisions-*.md
- Betti Extractor Trace.md
- Quantum Betti Numbers.md
