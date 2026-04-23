# Data Synthesizer Agent

## Overview

The Data Synthesizer Agent implements an infinite-curriculum training pipeline that continuously acquires data from external knowledge engines and generates contrastive training pairs for Forward-Forward learning.

**Source:** `q_mini_wasm_v2/core/training/data_synthesizer.hpp` and `data_synthesizer.cpp`

## Architecture

### Dual-Thread-Pool Design

```cpp
// Acquisition Pool: Async HTTP requests to APIs
// Perturbation Pool: Generate contrastive pairs (positive/negative)

void start(size_t acquisition_threads = 4, size_t perturbation_threads = 2);
```

The synthesizer uses two thread pools:
- **Acquisition Pool** (default: 4 threads): Manages rate-limited HTTP requests to external APIs
- **Perturbation Pool** (default: 2 threads): Generates contrastive training pairs

### C++17 Features

```cpp
// API response types using std::variant
using ApiPayload = std::variant<
    std::string_view,                    // Raw JSON/text
    std::vector<float>,                  // Numeric vector
    std::vector<std::vector<float>>,     // Matrix data
    std::vector<int32_t>,                // Fixed-point numeric (scale 1000)
    std::vector<std::vector<int32_t>>,   // Fixed-point matrix
    std::vector<Trit>                    // Ternary encoded
>;
```

Key C++17 features used:
- `std::optional` for API responses that may fail
- `std::variant` for type-safe payload containers
- `std::string_view` for zero-copy JSON parsing
- `std::visit` for variant dispatch

## API Clients (15 Total)

### Academic & Research APIs

| API | Class | Perturbation Strategy | Rate Limit | API Key |
|-----|-------|----------------------|------------|---------|
| **OpenAlex** | `OpenAlexClient` | Citation manipulation, fake co-authorship | 100,000/day | No |
| **arXiv** | `ArxivClient` | Semantic contradiction (invert core claims) | 15,000 | No |
| **Wikidata** | `WikidataClient` | Property recommender disruption (swap plausible entities) | 10,000 | No |
| **OEIS** | `OeisClient` | Sequence mutation (splice, alter growth factor) | 20,000 | No |
| **Lean/ProofDB** | `LeanClient` | Frame-preserving tactic injection | 10,000 | Optional |

### Scientific Data APIs

| API | Class | Perturbation Strategy | Rate Limit | API Key |
|-----|-------|----------------------|------------|---------|
| **PubChem** | `PubChemClient` | SMILES corruption (valency violations) | 5,000 | No |
| **NASA Exoplanet** | `NasaExoplanetClient` | Non-Keplerian transit noise injection | 10,000 | No |
| **PDB** | `PdbClient` | Spatial coordinate drift (steric clash generation) | 5,000 | No |
| **GBIF** | `GbifClient` | Location spoofing, species misclassification | 10,000 | No |
| **USGS Earthquake** | `UsgsEarthquakeClient` | Magnitude manipulation, location drift | 10,000 | No |

### Technical & Code APIs

| API | Class | Perturbation Strategy | Rate Limit | API Key |
|-----|-------|----------------------|------------|---------|
| **GitHub** | `GitHubClient` | AST mutilation (remove barriers, swap allocations) | 6,000 | Recommended |
| **SpaceX** | `SpaceXClient` | Payload mass errors, date shifts, stage swaps | 50,000 | No |
| **WolframAlpha** | `WolframClient` | Symbolic substitution (sign change, chain rule errors) | 10,000 | Required |

### Literature & Historical APIs

| API | Class | Perturbation Strategy | Rate Limit | API Key |
|-----|-------|----------------------|------------|---------|
| **Gutendex** | `GutendexClient` | Chapter reordering, character name swaps | 50,000 | No |
| **Chronicling America** | `ChroniclingAmericaClient` | Date misattribution, headline swaps | 20,000 | No |

## Usage

### Basic Initialization

```cpp
#include "core/training/data_synthesizer.hpp"

// Create synthesizer
DataSynthesizer synthesizer;

// Initialize API clients (no keys needed for most)
synthesizer.initialize_apis();

// Start with default thread counts
synthesizer.start(4, 2);  // 4 acquisition, 2 perturbation threads
```

### Fetching Training Samples

```cpp
// Blocking get - waits until sample available
TrainingSample sample = synthesizer.get_sample();

// Non-blocking check
if (synthesizer.has_sample()) {
    TrainingSample sample = synthesizer.get_sample();
    // Process sample...
}
```

### Training Sample Structure

```cpp
struct TrainingSample {
    ApiPayload data;                 // Variant payload from API
    Trit label;                      // +1 (positive), -1 (negative), 0 (unknown)
    std::string_view source_api;     // "wolfram", "pubchem", "oeis", etc.
    std::string_view domain;         // "math", "chemistry", "biology", "physics"
    
    int8_t is_positive() const;
    int8_t is_negative() const;
};
```

### Statistics

```cpp
auto stats = synthesizer.get_stats();
std::cout << "Total acquired: " << stats.total_acquired << "\n";
std::cout << "Total perturbed: " << stats.total_perturbed << "\n";
std::cout << "API failures: " << stats.api_failures << "\n";
std::cout << "Queue depth: " << stats.queue_depth << "\n";
```

## Integration with Training Pipeline

### Via Trainer CLI

```bash
# Enable web APIs (default: true from config)
q_mini_wasm_v2_trainer.exe --enable-web-apis true --moe-experts 243

# Disable for offline training
q_mini_wasm_v2_trainer.exe --disable-web-apis
```

### Via AutonomousTrainingPipeline

```cpp
#include "core/training/autonomous_training_pipeline.hpp"

PipelineConfig config;
config.enable_knowledge_engine = true;  // Enable DataSynthesizer
config.acquisition_threads = 4;
config.perturbation_threads = 2;

AutonomousTrainingPipeline pipeline;
pipeline.initialize(config);
pipeline.start_training();  // Automatically uses DataSynthesizer
```

## Perturbation Strategies

Each API client implements domain-specific negative sample generation:

### Symbolic Substitution (WolframAlpha)
```cpp
// Corrupts mathematical expressions
// - Changes sign: x^2 + 2x + 1 → x^2 - 2x + 1
// - Misapplies chain rule
// - Alters physical constants
```

### SMILES Corruption (PubChem)
```cpp
// Generates chemically invalid but syntactically valid SMILES
// - Violates valency rules
// - Impossible topological properties
// - Maintains surface syntax
```

### Sequence Mutation (OEIS)
```cpp
// Mutates integer sequences
// - Combines first half of one sequence with second half of another
// - Alters recursive growth factor
// - Modifies Colijn-Plazzotta rank
```

### Triple Swapping (Wikidata)
```cpp
// Disrupts RDF triples
// Replaces 'Object' in Subject-Predicate-Object with plausible but incorrect alternative
// Example: Swapping city coordinates with neighboring city
```

### Semantic Negation (arXiv)
```cpp
// Inverts scientific claims in abstracts
// "superconducting state at 4K" → "insulating state at 4K"
```

### Coordinate Drift (PDB)
```cpp
// Applies rotational/translational noise to 3D coordinates
// Generates steric clashes
// Violates Ramachandran plot boundaries
```

### AST Mutilation (GitHub)
```cpp
// Destructive logical mutations to code
// - Swaps sycl::malloc_shared with sycl::malloc_device
// - Removes sycl::barrier synchronization
// - Maintains syntactic validity, breaks semantics
```

## Rate Limiting & Backoff

All API clients implement exponential backoff:

```cpp
void backoff() {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ *= 2;  // Exponential increase
    rate_limit_ = initial_limit;  // Reset after backoff
}
```

Default rate limits are conservative. Production deployments should:
1. Use API keys where available (WolframAlpha, GitHub)
2. Monitor rate limit headers
3. Implement request caching

## Error Handling

API failures are non-fatal:
- Failed requests return `std::nullopt`
- Queue continues processing from other APIs
- Statistics track failure count
- Exponential backoff prevents hammering

## See Also

- [Autonomous Training Pipeline](autonomous-pipeline.md) - Integration guide
- [MoE Training Guide](moe-training.md) - CLI options for data acquisition
- [Research: Autonomous Forward-Forward Training](../research/Autonomous%20Forward-Forward%20Training%20Plan.md) - Original design

---

*Version: 2.0*  
*Last Updated: April 2026*  
*Source: `q_mini_wasm_v2/core/training/data_synthesizer.hpp:1-509`*
