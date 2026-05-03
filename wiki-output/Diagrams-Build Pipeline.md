# Build Pipeline Diagram

```mermaid
graph LR
    subgraph Source
        S1[C++ Sources]
        S2[Headers]
        S3[CMakeLists]
    end
    
    subgraph Build
        B1[Configure]
        B2[Compile]
        B3[Link]
    end
    
    subgraph Test
        T1[Unit Tests]
        T2[Integration]
        T3[Performance]
    end
    
    subgraph Deploy
        D1[Library]
        D2[WASM]
        D3[Plugins]
    end
    
    subgraph CI/CD
        C1[Lint]
        C2[Build Matrix]
        C3[Test Matrix]
        C4[Deploy]
    end
    
    S1 --> B1
    S2 --> B1
    S3 --> B1
    
    B1 --> B2
    B2 --> B3
    
    B3 --> T1
    B3 --> T2
    B3 --> T3
    
    T1 --> D1
    T2 --> D2
    T3 --> D3
    
    C1 --> C2
    C2 --> C3
    C3 --> C4
    
    style S1 fill:#e3f2fd
    style B1 fill:#e8f5e9
    style T1 fill:#fff3e0
    style D1 fill:#fce4ec
    style C1 fill:#f3e5f5
```

## Build Matrix

| Platform | SYCL | Build Type | Status |
|----------|------|------------|--------|
| Windows | OFF | Release | OK |
| Windows | OFF | Debug | OK |
| Linux | OFF | Release | OK |
| Linux | OFF | Debug | OK |
| Linux | ON | Release | Optional |

## Build Commands

```bash
# Basic build
mkdir build && cd build
cmake ..
cmake --build .

# With SYCL
cmake -S q_mini_wasm_v2 -B q_mini_wasm_v2/build_sycl -DCMAKE_CXX_COMPILER=icpx ..
cmake --build .

# Run tests
ctest --output-on-failure
```
