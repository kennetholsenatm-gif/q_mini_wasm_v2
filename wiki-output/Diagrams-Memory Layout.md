# Memory Layout Architecture

## Overview

This diagram illustrates the memory layout and hierarchy for the QMINIWASM
quantum-inspired computing system.

```mermaid
graph TB
    subgraph "System Memory Hierarchy"
        direction TB
        
        subgraph "Host Memory"
            DRAM[DDR4/DDR5
32-128 GB]
        end
        
        subgraph "Pre-Allocated Arenas"
            ARENA1[Graph Arena
1-4 GB]
            ARENA2[Tableau Arena
512 MB-2 GB]
            ARENA3[Agent Arena
256 MB-1 GB]
        end
        
        subgraph "Cache Hierarchy"
            L3[L3 Cache
16-64 MB]
            L2[L2 Cache
256 KB-1 MB]
            L1[L1 Cache
32-64 KB]
        end
        
        subgraph "Specialized Memory"
            WASM[WASM Linear Memory
4 GB max]
            FLASH[Flash-CiM
Stochastic MAC]
        end
    end
    
    DRAM --> ARENA1
    DRAM --> ARENA2
    DRAM --> ARENA3
    
    ARENA1 --> L3
    ARENA2 --> L3
    ARENA3 --> L3
    
    L3 --> L2
    L2 --> L1
    
    L1 --> WASM
    WASM --> FLASH
    
    style DRAM fill:#e1f5ff
    style FLASH fill:#e1ffe1
```

## Ternary Tree Memory Layout

```mermaid
graph LR
    subgraph "Ternary Tree Structure"
        ROOT[Root Node
Address 0]
        
        subgraph "Level 1"
            L1_0[Child 0
Base + 0]
            L1_1[Child 1
Base + 1]
            L1_2[Child 2
Base + 2]
        end
        
        subgraph "Level 2"
            L2_0[0.0]
            L2_1[0.1]
            L2_2[0.2]
            L2_3[1.0]
            L2_4[1.1]
            L2_5[1.2]
            L2_6[2.0]
            L2_7[2.1]
            L2_8[2.2]
        end
        
        subgraph "Level 3"
            L3[... 27 nodes ...]
        end
    end
    
    ROOT --> L1_0
    ROOT --> L1_1
    ROOT --> L1_2
    
    L1_0 --> L2_0
    L1_0 --> L2_1
    L1_0 --> L2_2
    
    L1_1 --> L2_3
    L1_1 --> L2_4
    L1_1 --> L2_5
    
    L1_2 --> L2_6
    L1_2 --> L2_7
    L1_2 --> L2_8
    
    L2_0 --> L3
    
    style ROOT fill:#fff4e1
```

## Address Calculation

```mermaid
graph LR
    subgraph "Address Formula"
        A[Node Address] --> B[Base + Offset]
        B --> C[Base + Σpathᵢ × 3ⁱ]
        
        D[Example: Path 0→2→1] --> E[0×3⁰ + 2×3¹ + 1×3²]
        E --> F[0 + 6 + 9 = 15]
    end
    
    subgraph "Cache Line Packing"
        G[Cache Line
64 bytes] --> H[20 Trits
packed]
        H --> I[40 bytes
2-bit per trit]
        I --> J[24 bytes padding
metadata]
    end
```

## Memory Allocation Strategy

```mermaid
graph TB
    subgraph "Arena Allocation"
        INIT[Initialize Arena
Pre-allocate 1GB] --> BUMP[Bump Allocator
Sequential growth]
        
        BUMP --> CHUNK[Chunk Management
64KB blocks]
        
        CHUNK --> {
            Small Objects
            < 1KB
        }
        
        CHUNK --> {
            Medium Objects
            1KB - 64KB
        }
        
        CHUNK --> {
            Large Objects
            > 64KB
            Direct mmap
        }
    end
    
    subgraph "Object Layout"
        HEADER[8 bytes
Header] --> DATA[Variable
Data]
        DATA --> PADDING[0-7 bytes
Alignment]
    end
    
    subgraph "Header Fields"
        SIZE[4 bytes
Size]
        TYPE[2 bytes
Type ID]
        FLAGS[2 bytes
Flags]
    end
```

## WASM Memory Mapping

```mermaid
graph TB
    subgraph "WASM Linear Memory Layout"
        direction TB
        
        STACK[Stack
1 MB growth downward]
        
        HEAP[Heap
Dynamic growth upward]
        
        STATIC[Static Data
Pre-allocated]
        
        RESERVED[Reserved
~60 MB system]
    end
    
    subgraph "QMINIWASM Regions"
        TERNARY[Ternary Data
0x1000-0x100000]
        
        TABLEAU[Stabilizer Tableau
0x100000-0x500000]
        
        GRAPH[Graph Structures
0x500000-0x900000]
        
        AGENT[Agent State
0x900000-0xD00000]
        
        RAG[RAG Index
0xD00000-0xFFFFFFFF]
    end
    
    STATIC --> TERNARY
    TERNARY --> TABLEAU
    TABLEAU --> GRAPH
    GRAPH --> AGENT
    AGENT --> RAG
    
    STACK -.-> HEAP
```

## Flash-CiM Memory Array

```mermaid
graph TB
    subgraph "Flash CiM Architecture"
        direction TB
        
        WL[Word Lines
1024 cells]
        
        subgraph "Memory Cell"
            FG[Floating Gate
Charge storage]
            SA[Sense Amplifier
Stochastic MAC]
        end
        
        BL[Bit Lines
Data output]
    end
    
    subgraph "GF(3) Operations"
        A[Input Trits] --> B{Sense}
        B --> C[Stochastic
Multiplication]
        C --> D[Accumulate
Partial sums]
        D --> E[Normalize
GF(3) result]
    end
    
    subgraph "Energy Efficiency"
        READ[< 0.5 pJ/read]
        WRITE[< 10 pJ/write]
        MAC[< 0.5 pJ/MAC]
    end
```

## Memory Access Patterns

```mermaid
sequenceDiagram
    participant CPU as CPU Core
    participant L1 as L1 Cache
    participant L2 as L2 Cache
    participant L3 as L3 Cache
    participant DRAM as DRAM
    participant Arena as Arena
    participant WASM as WASM
    participant CiM as Flash-CiM

    CPU->>L1: Read trit data
    activate L1
    
    alt Cache Hit
        L1-->>CPU: Return data
    else Cache Miss
        L1->>L2: Request data
        activate L2
        
        alt L2 Hit
            L2-->>L1: Return data
            L1-->>CPU: Return data
        else L2 Miss
            L2->>L3: Request data
            activate L3
            
            alt L3 Hit
                L3-->>L2: Return data
                L2-->>L1: Return data
                L1-->>CPU: Return data
            else L3 Miss
                L3->>DRAM: Request data
                activate DRAM
                DRAM->>Arena: Access arena
                Arena-->>DRAM: Return block
                DRAM-->>L3: Return data
                deactivate DRAM
                L3-->>L2: Return data
                L2-->>L1: Return data
                L1-->>CPU: Return data
            end
            deactivate L3
        end
        deactivate L2
    end
    deactivate L1

    Note over CPU,CiM: Offload to CiM for bulk operations
    CPU->>WASM: Transfer control
    WASM->>CiM: Execute GF(3) ops
    CiM-->>WASM: Return results
    WASM-->>CPU: Return control
```

## Performance Metrics

| Memory Level | Latency | Bandwidth | Capacity |
|--------------|---------|-----------|----------|
| L1 Cache | 1-2 ns | 100+ GB/s | 32-64 KB |
| L2 Cache | 5-10 ns | 50+ GB/s | 256 KB-1 MB |
| L3 Cache | 20-50 ns | 30+ GB/s | 16-64 MB |
| DRAM | 50-100 ns | 20+ GB/s | 32-128 GB |
| Flash-CiM | 100 ns | 10 GB/s | 1-4 GB |

---

*Generated: April 6, 2026*
*Diagram Type: Mermaid Architecture*
