# Wiki Navigation

This wiki is the extended knowledge layer for `qminiwasm-core`. The canonical onboarding split (Strategic vs Tactical) now lives in the repository `README`.

Start here:

- Main entrypoint: [../README.md](../README.md)
- Project taxonomy glossary: [../docs/Q-Mini-WASM_ Edge AI Taxonomy.md](../docs/Q-Mini-WASM_%20Edge%20AI%20Taxonomy.md)
- Training WUI (native): [../training-wui/README.md](../training-wui/README.md)
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
- Zero-Trust Ephemeral Enrollment reference: [Identity Stack Reference](../docs/IDENTITY_STACK_REFERENCE.md)
- Enclave runtime flow (boot → ECL → CGE): [Journey of a Vector](../docs/architecture/JOURNEY_OF_A_VECTOR.md)
- [DevSecOps](DevSecOps.md)
- [Performance](Performance.md)
- [Testing](Testing.md)

## Community and Design

- [Community](Community.md)
- [UX-UI-Design-Style-Guide](UX-UI-Design-Style-Guide.md)

## Documentation Rules of Engagement

- Use `README.md` for audience routing and top-level orientation.
- Use `training-wui/README.md` (Path A) for the first successful native training run.
- Use `docs/operations/OPERATIONS_RUNBOOK.md` for deployment and infra commands.
- Use wiki pages for deeper context, analysis, and research narratives.
- Use `docs/IDENTITY_STACK_REFERENCE.md` as the source of truth for ZTEE-aligned identity protocol text; other pages should link rather than duplicate.