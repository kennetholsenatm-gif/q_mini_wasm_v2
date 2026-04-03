# User Journey Diagram

```mermaid
journey
    title q_mini_wasm_v2 User Journey
    section Getting Started
      Clone repository: 5: User
      Install dependencies: 4: User
      Build framework: 3: User, System
      Run tests: 4: User, System
    section Development
      Write ternary code: 5: User
      Configure MoE router: 4: User
      Implement Forward-Forward: 3: User
      Debug with SYCL: 2: User, System
    section Deployment
      Build WebAssembly: 4: User, System
      Optimize for edge: 3: User
      Deploy to device: 5: User, System
      Monitor performance: 4: User
    section Maintenance
      Update documentation: 3: User, Agent
      Run CI/CD: 4: System
      Review improvements: 5: User
      Apply patches: 4: User, System
```

## User Personas

### 1. Framework Developer
- **Goal**: Extend core functionality
- **Key Actions**: Modify C++ code, add new gates
- **Pain Points**: Complex GF(3) arithmetic

### 2. Application Developer
- **Goal**: Build edge AI applications
- **Key Actions**: Use MoE routing, deploy to WASM
- **Pain Points**: SYCL setup, optimization

### 3. Researcher
- **Goal**: Experiment with ternary networks
- **Key Actions**: Run benchmarks, analyze results
- **Pain Points**: Documentation gaps

### 4. DevOps Engineer
- **Goal**: Maintain CI/CD pipeline
- **Key Actions**: Monitor builds, manage deployments
- **Pain Points**: Cross-platform builds
