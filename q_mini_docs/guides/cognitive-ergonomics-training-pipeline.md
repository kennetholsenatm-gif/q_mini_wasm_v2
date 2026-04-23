# Cognitive Ergonomics Guide: Training Pipeline Mental Model

> **Purpose**: This document provides visual and conceptual guidance for understanding the q_mini_wasm_v2 training pipeline. It follows cognitive ergonomics principles to reduce mental load when working with the system.

---

## Quick Mental Model: The "Knowledge Refinery"

Think of the training pipeline as a **knowledge refinery** that transforms raw information into trained expertise:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          KNOWLEDGE REFINERY                                  │
│                                                                              │
│  Raw Knowledge Sources                                                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                                   │
│  │ Wolfram  │  │ PubChem  │  │  OEIS    │  ← External APIs                 │
│  │ (Math)   │  │(Chemistry│  │(Sequences│                                   │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘                                   │
│       │             │             │                                          │
│       └─────────────┴─────────────┘                                          │
│                     │                                                        │
│                     ▼                                                        │
│         ┌─────────────────────┐                                              │
│         │  DATA SYNTHESIZER   │  ← Continuous acquisition + perturbation   │
│         │   (Dual Thread Pool)│                                              │
│         └──────────┬──────────┘                                              │
│                    │                                                         │
│                    ▼                                                         │
│         ┌─────────────────────┐                                              │
│         │ Contrastive Pairs   │  ← (+) Positive vs (-) Negative              │
│         │   [⊕]     vs   [⊖]  │                                              │
│         └──────────┬──────────┘                                              │
│                    │                                                         │
│                    ▼                                                         │
│  ┌─────────────────────────────────────────┐                               │
│  │     AUTONOMOUS TRAINING PIPELINE        │                               │
│  │                                         │                               │
│  │  ┌──────────────┐    ┌──────────────┐   │                               │
│  │  │  MoE Router  │───→│  243 Experts │   │                               │
│  │  │  GF(3) Top-K │    │   Forward    │   │                               │
│  │  │   Routing    │    │  -Forward    │   │                               │
│  │  └──────────────┘    └──────────────┘   │                               │
│  │         │                   │            │                               │
│  │         ▼                   ▼            │                               │
│  │  ┌──────────────────────────────────┐   │                               │
│  │  │   Betti-Guided Topology          │   │                               │
│  │  │   ╔═══════════════════════════╗  │   │                               │
│  │  │   ║  β₀: Connected Components ║  │   │  ← Graph optimization         │
│  │  │   ║  β₁: Cycles (1-holes)    ║  │   │    based on topology         │
│  │  │   ╚═══════════════════════════╝  │   │                               │
│  │  └──────────────────────────────────┘   │                               │
│  │                                         │                               │
│  └─────────────────────────────────────────┘                               │
│                    │                                                         │
│                    ▼                                                         │
│         ┌─────────────────────┐                                              │
│         │  Trained Model      │  ← Output: 243 specialized experts           │
│         │  (Checkpoint .bin)  │                                              │
│         └─────────────────────┘                                              │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Decision Flow: "Should I Trigger Betti Guidance?"

```
                    ┌─────────────────┐
                    │ Training Loop   │
                    │ Batch Complete  │
                    └────────┬────────┘
                             │
                             ▼
              ┌──────────────────────────────┐
              │ batch_count % interval == 0 ?  │
              └────────┬──────────┬──────────┘
                       │          │
                    YES │          │ NO
                       ▼          ▼
              ┌─────────────┐  ┌──────────────┐
              │   TRIGGER   │  │   CONTINUE   │
              │BETTI EVAL   │  │   TRAINING   │
              └──────┬──────┘  └──────────────┘
                     │
                     ▼
        ┌────────────────────────┐
        │ Compute Betti Numbers  │
        │ β₀, β₁, β₂, χ          │
        └───────┬────────────────┘
                │
                ▼
        ┌────────────────────────┐
        │  Reconfiguration Test  │
        │                        │
        │  routing_quality < 0.5?  │────YES────┐
        │  training_stalled?       │           │
        │  β₀ > 3?               │────YES────┤
        │                        │           │
        └──────────┬─────────────┘           │
                   │                          │
                NO │                          │
                   ▼                          │
        ┌─────────────────┐                   │
        │ Topology OK     │◄──────────────────┤
        │ No changes      │                   │
        └─────────────────┘                   │
                                              ▼
                                   ┌─────────────────────┐
                                   │ APPLY TOPOLOGY      │
                                   │ CHANGES:            │
                                   │ • Add/remove edges  │
                                   │ • Adjust nodes      │
                                   └─────────────────────┘
```

---

## Data Flow: From API to Trained Expert

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ STAGE 1: ACQUISITION                                                         │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  HTTP Request                    Response Parser                             │
│  ┌─────────┐                   ┌───────────────┐                            │
│  │ GET api │ ───200 OK─────▶   │ JSON → float │  ← Error handling          │
│  │ /query  │    [1.2, 3.4]     │ vector        │    → exponential backoff   │
│  └─────────┘                   └───────┬───────┘                            │
│                                          │                                   │
│  Rate Limit: 100 req/min                 ▼                                   │
│  Backoff: 1s → 2s → 4s...        ┌───────────────┐                           │
│                                  │ ApiPayload    │                           │
│                                  │ variant       │                           │
│                                  └───────┬───────┘                           │
│                                          │                                   │
└──────────────────────────────────────────┼───────────────────────────────────┘
                                           │
                                           ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│ STAGE 2: PERTURBATION (Contrastive Learning)                                 │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Positive Sample                    Negative Sample                         │
│  ┌─────────────┐                    ┌─────────────┐                           │
│  │ [1.2, 3.4,  │  ──corrupt 10%─▶  │ [1.2, -1.0, │  ← Frame-preserving      │
│  │ -0.5, 2.1]  │     of values     │ 0.0, 2.1]   │    mutation              │
│  │   label: +1 │                    │  label: -1  │                           │
│  └──────┬──────┘                    └──────┬──────┘                           │
│         │                                  │                                 │
│         └────────────┬─────────────────────┘                                 │
│                      │                                                       │
│                      ▼                                                       │
│            ┌─────────────────┐                                               │
│            │ TrainingSample  │                                               │
│            │ Queue (FIFO)    │                                               │
│            └────────┬────────┘                                               │
│                     │                                                        │
└─────────────────────┼────────────────────────────────────────────────────────┘
                      │
                      ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│ STAGE 3: ROUTING & TRAINING                                                  │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Ternary Quantization              MoE Routing                               │
│  ┌─────────────┐                   ┌─────────────────┐                        │
│  │ float →     │                   │ Top-K Selection │                        │
│  │ {-1,0,+1}   │ ────────────────▶ │ k=3 experts     │                        │
│  │ (thresholds │                   │ from 243 total  │                        │
│  │  ±0.33)     │                   │                 │                        │
│  └─────────────┘                   └────────┬────────┘                        │
│                                             │                                │
│                                             ▼                                │
│                              ┌────────────────────────┐                       │
│                              │ Expert 47  Expert 182  │                       │
│                              │    ┌──┐       ┌──┐     │                       │
│                              │    │FF│       │FF│     │  ← Forward-Forward    │
│                              │    │  │       │  │     │    per layer          │
│                              │    └──┘       └──┘     │                       │
│                              │   goodness    goodness │                       │
│                              │   pos=12.3    pos=9.8   │                       │
│                              │   neg=3.1     neg=4.2   │                       │
│                              │   Δ=+9.2      Δ=+5.6    │                       │
│                              └────────────────────────┘                       │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## Configuration Decision Tree

```
┌─────────────────────────────────────────────────────────────┐
│                START: Configure Pipeline                    │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        ▼
            ┌───────────────────────┐
            │ Need real-time API    │
            │ data?                 │
            └───────────┬───────────┘
                        │
            ┌───────────┴───────────┐
            │                       │
         YES│                       │NO
            ▼                       ▼
    ┌───────────────┐      ┌────────────────┐
    │ Enable HTTP   │      │ Use synthetic  │
    │ clients       │      │ data only      │
    │ Set rate      │      │ (testing)      │
    │ limits        │      └───────┬────────┘
    │ (100,50,200)  │              │
    └───────┬───────┘              │
            │                      │
            └──────────┬───────────┘
                       │
                       ▼
           ┌───────────────────────┐
           │ GPU acceleration      │
           │ available?            │
           └───────────┬───────────┘
                       │
           ┌───────────┴───────────┐
           │                       │
        YES│                       │NO / Not needed
           ▼                       ▼
   ┌───────────────┐      ┌────────────────┐
   │ Build with    │      │ CPU fallback   │
   │ USE_SYCL=ON   │      │ (default)      │
   │ Link SYCL     │      │                │
   │ runtime       │      │ Single-thread  │
   │               │      │ or OpenMP      │
   └───────┬───────┘      └───────┬────────┘
           │                      │
           └──────────┬───────────┘
                      │
                      ▼
          ┌───────────────────────┐
          │ Enable Betti          │
          │ guidance?             │
          └───────────┬───────────┘
                      │
          ┌───────────┴───────────┐
          │                       │
       YES│                       │NO
          ▼                       ▼
  ┌───────────────┐      ┌────────────────┐
  │ Set threshold │      │ Static topology│
  │ (default: 15) │      │ No adaptation  │
  │ Enable graph  │      │                │
  │ optimization  │      │                │
  └───────┬───────┘      └───────┬────────┘
          │                      │
          └──────────┬───────────┘
                     │
                     ▼
         ┌───────────────────────┐
         │   PIPELINE READY      │
         └───────────────────────┘
```

---

## Error Handling Mental Model

```
┌────────────────────────────────────────────────────────────────┐
│                    ERROR LAYERS                                  │
├────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Layer 4: API Errors (recoverable)                              │
│  ┌────────────────────────────────────────────────────────┐   │
│  │ Rate limit exceeded → Backoff → Retry                  │   │
│  │ Network timeout → Log → Use synthetic fallback         │   │
│  │ Invalid response → Skip batch → Continue               │   │
│  └────────────────────────────────────────────────────────┘   │
│                          │                                      │
│                          ▼                                      │
│  Layer 3: Training Errors (recoverable)                         │
│  ┌────────────────────────────────────────────────────────┐   │
│  │ Expert overflow → Clip gradients → Continue            │   │
│  │ Numerical instability → Reduce LR → Retry              │   │
│  │ Bad sample → Skip → Next batch                         │   │
│  └────────────────────────────────────────────────────────┘   │
│                          │                                      │
│                          ▼                                      │
│  Layer 2: System Errors (graceful degradation)                  │
│  ┌────────────────────────────────────────────────────────┐   │
│  │ SYCL init failed → CPU fallback → Continue             │   │
│  │ Memory full → Checkpoint → Clear buffers → Continue      │   │
│  │ Thread crash → Restart worker → Log → Continue           │   │
│  └────────────────────────────────────────────────────────┘   │
│                          │                                      │
│                          ▼                                      │
│  Layer 1: Fatal Errors (pipeline stop)                         │
│  ┌────────────────────────────────────────────────────────┐   │
│  │ Config corrupted → Stop → Report                       │   │
│  │ Core dump → Stop → Alert                             │   │
│  └────────────────────────────────────────────────────────┘   │
│                                                                  │
└────────────────────────────────────────────────────────────────┘
```

---

## Metric Interpretation Guide

| Metric | Healthy Range | Action if Outside |
|--------|---------------|-------------------|
| `ff_goodness_delta` | > 0 | If < 0: Negative samples too similar to positive - adjust corruption rate |
| `expert_utilization` | 0.2-0.8 per expert | If < 0.1: Expert underused - consider topology change |
| `betti_beta_1` | 5-20 | If > 30: Too many cycles - reduce edges; If < 3: Too sparse - add edges |
| `routing_quality` | > 0.6 | If < 0.5: Trigger topology reconfiguration |
| `ds_api_failures` | < 5% | If > 10%: Check API keys and network connectivity |

---

## Common Patterns

### Pattern 1: "Cold Start" - Bootstrap from No Data

```cpp
// When starting with empty queue
PipelineConfig config;
config.enable_knowledge_engine = true;  // Enable API calls
config.acquisition_threads = 4;        // Fill queue quickly

pipeline.initialize(config);

// Wait for buffer to fill before training
while (pipeline.get_metrics().ds_queue_depth < config.batch_size * 2) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

pipeline.start_training();
```

### Pattern 2: "Adaptive Training" - Dynamic Topology

```cpp
// Check metrics periodically and adjust
auto metrics = pipeline.get_metrics();

if (metrics.betti_beta_1 > 30) {
    // Too complex - simplify topology
    pipeline.trigger_topology_evaluation();
    // This will reduce edges automatically
}
```

### Pattern 3: "Checkpoint Resume" - Fault Tolerance

```cpp
// Try to resume from checkpoint
if (pipeline.import_model("checkpoint_epoch_50.bin")) {
    std::cout << "Resumed from epoch 50\n";
} else {
    std::cout << "Starting fresh\n";
}
```

---

## Visual Summary: Complete Architecture

```
                    ┌─────────────────────────────────────┐
                    │          USER CODE / WUI            │
                    │  (Controls: start, stop, configure)  │
                    └───────────────┬─────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────────────┐
│                        AUTONOMOUS TRAINING PIPELINE                            │
├────────────────────────────────────────────────────────────────────────────────┤
│                                                                                │
│  ┌────────────────────────────────────────────────────────────────────────┐   │
│  │                     DATA SYNTHESIZER                                    │   │
│  │  ┌────────────┐    ┌────────────┐    ┌────────────┐                    │   │
│  │  │ Wolfram    │    │ PubChem    │    │ OEIS       │  HTTP Clients     │   │
│  │  │ ThreadPool │    │ ThreadPool │    │ ThreadPool │  (Rate limited)   │   │
│  │  └─────┬──────┘    └─────┬──────┘    └─────┬──────┘                    │   │
│  │        └──────────────────┴──────────────────┘                         │   │
│  │                      │                                                  │   │
│  │                      ▼                                                  │   │
│  │              ┌──────────────┐                                         │   │
│  │              │ Perturbation │  ← Generates (+)/(-) pairs              │   │
│  │              │ ThreadPool   │                                         │   │
│  │              └──────┬───────┘                                         │   │
│  │                     │                                                  │   │
│  └─────────────────────┼──────────────────────────────────────────────────┘   │
│                        │                                                     │
│                        ▼                                                     │
│  ┌────────────────────────────────────────────────────────────────────────┐   │
│  │                     TRAINING CORE                                       │   │
│  │                                                                         │   │
│  │    ┌──────────────┐     ┌──────────────┐     ┌──────────────┐          │   │
│  │    │  Ternary     │────▶│  MoE Router  │────▶│  243 Experts │          │   │
│  │    │  Quantize    │     │  Top-K=3     │     │  Forward     │          │   │
│  │    │  (±0.33)     │     │  GF(3)       │     │  -Forward    │          │   │
│  │    └──────────────┘     └──────┬───────┘     └──────┬───────┘          │   │
│  │                                │                    │                   │   │
│  │                                └────────────────────┘                   │   │
│  │                                                  │                      │   │
│  │                    ┌───────────────────────────────┘                      │   │
│  │                    │                                                     │   │
│  │                    ▼                                                     │   │
│  │    ┌────────────────────────────────────────────────────────────┐       │   │
│  │    │              BETTI EXTRACTOR                             │       │   │
│  │    │  ┌─────────┐    ┌─────────┐    ┌─────────┐              │       │   │
│  │    │  │ Graph   │───▶│ Compute │───▶│ Optimize│              │       │   │
│  │    │  │ Tableau │    │ β₀,β₁,β₂│    │ Topology│              │       │   │
│  │    │  └─────────┘    └─────────┘    └─────────┘              │       │   │
│  │    │         (Evaluated every N batches)                    │       │   │
│  │    └────────────────────────────────────────────────────────────┘       │   │
│  │                                                                         │   │
│  └────────────────────────────────────────────────────────────────────────┘   │
│                                                                                │
│                        │                                                       │
│                        ▼                                                       │
│  ┌────────────────────────────────────────────────────────────────────────┐   │
│  │                     OUTPUT                                             │   │
│  │  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐             │   │
│  │  │ Checkpoint   │    │ Metrics      │    │ Logs         │             │   │
│  │  │ .bin files   │    │ JSON/stream  │    │ stderr/file  │             │   │
│  │  └──────────────┘    └──────────────┘    └──────────────┘             │   │
│  └────────────────────────────────────────────────────────────────────────┘   │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘
```

---

## Quick Reference: Common Operations

| Operation | Code Pattern | Expected Latency |
|-----------|-------------|------------------|
| Initialize pipeline | `pipeline.initialize(config)` | 100-500ms |
| Get one sample | `synthesizer.get_sample()` | 10-100ms (blocking) |
| Process batch | `pipeline.process_batch()` | 50-500ms (depends on k) |
| Betti evaluation | `pipeline.trigger_topology_evaluation()` | 10-100ms |
| Checkpoint | `pipeline.export_model(path)` | 100-1000ms |

---

*Document Version: 1.0 | Generated for q_mini_wasm_v2 training pipeline*
