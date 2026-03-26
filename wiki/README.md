# Wiki Navigation

This wiki is the extended knowledge layer for `qminiwasm-core`. The canonical onboarding split (Strategic vs Tactical) now lives in the repository `README`.

Start here:

- Main entrypoint: [../README.md](../README.md)
- Project taxonomy glossary: [../docs/GLOSSARY.md](../docs/GLOSSARY.md)
- 0-to-1 happy path: [../docs/getting-started/QUICKSTART_0_TO_1.md](../docs/getting-started/QUICKSTART_0_TO_1.md)
- Operator runbook: [../docs/operations/OPERATIONS_RUNBOOK.md](../docs/operations/OPERATIONS_RUNBOOK.md)
- Vector dataflow walkthrough: [../docs/architecture/JOURNEY_OF_A_VECTOR.md](../docs/architecture/JOURNEY_OF_A_VECTOR.md)

## Strategic and Product Context

- [Home](Home.md)
- [Overview](Overview.md)
- [Business-Value](Business-Value.md)
- [Roadmap](Roadmap.md) — **tooling-first** plan (WLES harness, ZTEE simulator extensions, CPL spike); not algorithm research milestones

## Architecture and Platform

- [Architecture-Overview](Architecture-Overview.md)
- [AI-Training-Pipeline](AI-Training-Pipeline.md)
- [Development](Development.md)
- [Deployment-Guide](Deployment-Guide.md) (advanced, operational)

## Research and Theory

- [Mathematical-Formulation](Mathematical-Formulation.md)
- [Quantum-Optimization](Quantum-Optimization.md)
- [Vec2Text-Inversion](Vec2Text-Inversion.md)
- [Cascade-RL-and-MOPD](Cascade-RL-and-MOPD.md)
- [Intel-Quantum-and-ARC](Intel-Quantum-and-ARC.md)

## Security, Compliance, and Operations

- [Security-and-Compliance](Security-and-Compliance.md)
- [Security](Security.md)
- [Compliance](Compliance.md)
- Zero-Trust Ephemeral Enrollment: [ZTEE Framework](../docs/ZTEE_FRAMEWORK.md)
- Enclave lifecycle (boot → ZTEE → ECL → CGE): [ENCLAVE_LIFECYCLE](../docs/ENCLAVE_LIFECYCLE.md)
- [DevSecOps](DevSecOps.md)
- [Performance](Performance.md)
- [Testing](Testing.md)

## Community and Design

- [Community](Community.md)
- [UX-UI-Design-Style-Guide](UX-UI-Design-Style-Guide.md)

## Documentation Rules of Engagement

- Use `README.md` for audience routing and top-level orientation.
- Use `docs/getting-started` for the first successful run.
- Use `docs/operations/OPERATIONS_RUNBOOK.md` for deployment and infra commands.
- Use wiki pages for deeper context, analysis, and research narratives.
- Use `docs/ZTEE_FRAMEWORK.md` as the single source of truth for ZTEE protocol text; other pages should link rather than duplicate.