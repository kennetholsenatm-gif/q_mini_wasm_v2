# **Architecture Review and Integration Strategy for Ternary Quantum-Classical Systems**

The transition from traditional binary von Neumann computing architectures
to non-binary, quantum-inspired paradigms represents a foundational shift
in system design, particularly within the strict operational confines of
edge-compute environments. In scenarios where thermal dissipation, battery
capacity, and memory bandwidth are absolute constraints, the historical
approach of scaling artificial intelligence via massive, monolithic
floating-point architectures has reached diminishing returns. While the
broader industry attempts to decouple model size from compute budgets using
sparse Mixture of Experts (MoE) models 1, applying these paradigms directly
to edge devices often results in severe memory fragmentation and
unmanageable context-switching overhead. The repository under evaluation,
which focuses heavily on ternary-state quantum logic, Galois Field 3
(GF(3)) stabilizer tableaus, SYCL hardware acceleration, and MoE inference
pipelines, attempts to circumvent these traditional limitations. By
utilizing radix-3 mathematical structures, the system aims to achieve
higher informational density and superior routing efficiency.

However, realizing the theoretical benefits of this quantum-classical
topology requires an immaculate architectural implementation. The
integration of highly disparate languages—specifically C++, Go, Python, and
WebAssembly (WASM)—presents profound interoperability challenges. These
challenges are particularly acute when orchestrating application state
across continuous memory boundaries while simultaneously managing
asynchronous I/O and highly specialized hardware interfaces like Flash
Compute-in-Memory (CIM). The incorporation of quantum-inspired error
correction further complicates the system topology, demanding absolute
mathematical alignment between the theoretical abstractions of quantum
mechanics and the physical memory layout of the edge hardware.

The following exhaustive analysis decomposes the repository's architecture
into three distinct operational phases. The first phase resolves competing
orchestration and memory management frameworks, ruthlessly eliminating
operational redundancies that threaten edge efficiency. The second phase
synthesizes fragmented micro-components and scripting artifacts into a
cohesive, highly performant execution pipeline. The third and final phase
architects a mathematically rigorous integration strategy for a GF(3)
Quantum Graph Neural Network (QGNN), merging the classical simulability of
the Gottesman-Knill theorem with the routing efficacy of dynamic
graph-based MoE topologies.

## **Phase 1: Competing Framework Resolution**

The repository demonstrates a classic architectural schism often found in
experimental multi-language systems: the presence of mutually exclusive
operational frameworks competing for system resources, orchestration
authority, and memory bandwidth. In a constrained edge-compute environment,
the tolerance for redundant control planes is practically zero. The current
architecture attempts to harmonize native C++ numerical processing,
Go-based gateway routing, Python-driven agentic logic, and WASM memory
bridging. This topology invariably introduces unacceptable latency
penalties, memory duplication, and cognitive load, severely undermining the
project's core ethos.

### **Orchestration Paradigm: Go Concurrency vs. Python Global Interpreter Lock**

The most critical conflict arises between the Go-based gateway
orchestration and the Python-based agentic frameworks. The repository's
reliance on Python for application workflows, specifically evidenced by the
heavy utilization of asynchronous server gateway interfaces (ASGI) and
programmatic lifecycle management 2, fundamentally conflicts with the
highly concurrent, goroutine-based architecture of the go\_cli component.
Python's async implementations, while capable of managing I/O concurrency,
remain fundamentally constrained by the Global Interpreter Lock (GIL) for
CPU-bound tasks. In an edge-compute scenario, spinning up an embedded
Python runtime or managing inter-process communication (IPC) between a
compiled Go binary and a Python interpreter introduces exorbitant
context-switching overhead.

Furthermore, Python's dynamic memory allocation directly contravenes the
deterministic, pre-allocated memory requirements of the core/flash\_cim
modules. The Go orchestration plane offers superior concurrent network
throughput and a vastly smaller operational runtime footprint. However,
Go's garbage collection introduces non-deterministic latency spikes that
can disrupt the highly synchronized SYCL hardware acceleration pipelines
operating in the C++ layer. The coexistence of Python logic, Go network
routing, and C++ numerical processing across independent memory spaces
creates a fragile orchestration triangle that cannot survive the rigors of
edge deployment.


### Continue Reading

To resolve this orchestration conflict, the architectural ethos dictates
the complete deprecation of Python as an operational runtime in the
production edge environment. Python must be relegated strictly to a
build-time script, an offline training orchestrator, or a
data-preprocessing utility. The surviving operational framework must be the
Go-based gateway, but it must be heavily modified to act solely as a
lightweight, asynchronous network routing layer. All heavy computational
logic and agentic decision-making currently residing in the Python agents/
directories must be ported to C++ and compiled directly to WebAssembly. The
Go layer will then host a lightweight WASM runtime, completely isolating
the memory domains and eliminating the need for IPC with an external Python
process. This approach securely sandboxes agentic logic while allowing Go
to multiplex thousands of concurrent connections using a fraction of the
memory required by ASGI Python servers.

### **Memory Domains: WASM Bridging vs. Native SYCL Arenas**

A secondary, equally critical conflict exists within the memory management
paradigms operating between the WASM bridges (dll/) and the native C++ SYCL
implementations (core/). The repository currently exhibits a fragmented
approach where data initialized in the WASM linear memory must be
serialized, copied across the WebAssembly interface boundary, and
deserialized into native C++ arenas for processing by core/ternary and
core/qutrit\_stabilizer.cpp. This serialization penalty effectively negates
any computational speed-up provided by the SYCL acceleration layer. The
binary-centric memory alignment of typical WASM implementations directly
conflicts with the packed GF(3) ternary logic requirements of the core
engine, which relies on high-density data packing to maximize cache
utilization.

The optimal resolution requires the implementation of a zero-copy, unified
memory arena using the WebAssembly Memory64 standard combined with the
Arrow IPC format, specifically quantized and adapted for ternary
representations. Instead of passing massive data payloads across the
boundary, the C++ SYCL modules and the WASM runtime must be engineered to
map the exact same physical memory regions. The C++ allocator must be
designated as the sole, authoritative manager over the memory lifecycle,
utilizing custom, aligned allocators that map GF(3) data structures
directly to the cache lines of the underlying edge hardware.

The WASM modules will operate purely via deterministic pointer arithmetic
into this shared arena. Because traditional binary Arrow IPC assumes
radix-2 data structures, a superior alternative involves the introduction
of a custom, FlatBuffers-inspired serialization protocol that is
mathematically designed for radix-3 states. This ensures that memory reads
initiated from the WASM side naturally decode into the qutrit states
required by the core/flash\_cim, bypassing integer division and modulo
operations during the deserialization phase.

### **Inference and Routing: Softmax vs. Symplectic Gating**

The third major conflict lies deeply embedded within the inference
pipeline itself, specifically regarding the handling of Mixture of Experts
(MoE) routing within core/moe/router.cpp. The repository's architecture
implies the use of traditional floating-point, binary-centric softmax
routing logic to distribute tokens to computational experts. This standard
approach, while ubiquitous in conventional AI scaling 1, violently clashes
with the project's core ethos of ternary-state quantum logic. Operating a
binary-based, continuous floating-point MoE router atop a hardware and
software stack optimized for discrete GF(3) logic introduces immense
computational dissonance and unnecessary power consumption.

The MoE routing mechanism must natively understand ternary states and
quantum entanglement patterns to achieve true edge efficiency. Therefore,
the traditional floating-point MoE router must be entirely deprecated. The
required alternative is the implementation of a mathematically rigorous
ternary routing mechanism based on GF(3) linear codes and symplectic
geometry. By representing expert routing choices not as continuous
probability distributions, but as discrete stabilizer states over GF(3),
the system can route tokens using modular arithmetic. This aligns perfectly
with the Gottesman-Knill theorem's classical simulability of such circuits,
allowing the routing to be processed with unprecedented speed and minimal
power draw on the Flash CIM edge hardware.

| Competing Frameworks | Submodules Involved | The Chosen Survivor / Alternative | Architectural Justification |
| :---- | :---- | :---- | :---- |
| Go Gateway Orchestration vs. Python Agent Logic | go\_cli/, agents/ | **Alternative:** Go-hosted WASM runtime executing C++ compiled agents | Eliminates the Python GIL, massive runtime memory overhead, and costly IPC context switches. Confines dynamic agent logic to a secure, fast WASM sandbox orchestrated by lightweight Go concurrency, ideal for edge limits. |

### Continue Reading

| WASM Memory Bridging vs. Native C++ Allocation | dll/, core/ | **Survivor:** Native C++ Allocation via Zero-Copy Shared Arena | Copying data across the WASM boundary destroys throughput. Utilizing Native C++ as the absolute memory authority with memory-mapped WASM pointers ensures SYCL computational pipelines are never starved for data. |
| Floating-Point Softmax MoE Routing vs. GF(3) Ternary Engine | core/moe/router.cpp, core/ternary/ | **Alternative:** GF(3) Symplectic Gating Mechanism | Softmax is computationally expensive, requires floating-point ALUs, and is fundamentally binary-centric. Symplectic gating uses GF(3) discrete modular arithmetic, integrating perfectly with Flash Compute-in-Memory qutrit architectures. |
| Binary Vector Embeddings vs. Qutrit Stabilizer Tableaus | core/inference/, core/qutrit\_stabilizer.cpp | **Survivor:** Qutrit Stabilizer Tableaus | Storing embeddings as traditional continuous vectors wastes the ternary physical capacity of the hardware. Encoding information directly into GF(3) stabilizer tableaus allows the use of quantum-inspired error correction natively during the inference phase. |
| Standard Backpropagation vs. Entanglement-Aware Training | core/inference/entanglement\_token.cpp | **Alternative:** Topologically Twisted Circuit Gradients | Standard gradient descent completely ignores the phase space and interference patterns of qutrits. Using geometric reframing and topological twists allows gradient updates that respect the inherent entanglement structures of the tokens. |

## **Phase 2: Disparate Component Synthesis**

The architectural footprint of the repository is presently burdened by a
massive constellation of micro-agents, utility scripts, and fragmented
modules. The presence of over thirty separate Python scripts, multiple Go
CLI commands, and numerous Model Context Protocol (MCP) servers indicates
an ad-hoc, evolutionary growth pattern rather than a deliberately
engineered edge architecture.2 In a cloud-native environment, such
microservice proliferation is often masked by abundant resources. However,
in an edge-first, resource-constrained paradigm, defragmentation is not
merely a matter of aesthetic code hygiene; it is an absolute operational
imperative required to reduce technical debt, minimize static memory
consumption, and eliminate execution latency.

### **Unifying the Agentic Service Mesh**

A detailed mapping of the repository reveals significant functional
overlap across multiple operational domains. The most glaring redundancy
exists within the proliferation of Python scripts functioning as
independent Model Context Protocol (MCP) servers.2 MCP typically relies on
JSON-RPC over standard input/output (stdio) or HTTP to facilitate tool
integration and context delivery. Running multiple independent Python
processes to handle distinct MCP endpoints on an edge device drains the
battery, thrashes the CPU cache, and guarantees high context-switching
latency due to constant process scheduling overhead.

The consolidation blueprint mandates the construction of a cohesive,
unified Agentic Service Mesh compiled directly into the C++ core and
exposed either as a single shared dynamic library (dll/) or a unified WASM
module. The fragmented Python scripts currently acting as MCP servers must
be structurally analyzed, their core logic extracted, and systematically
rewritten into a unified, asynchronous C++ state machine. This state
machine will implement the MCP protocol natively in memory, multiplexing
all contextual tool interactions through a single persistent connection or
shared buffer array, rather than relying on disparate spawned OS processes
and expensive JSON serialization over stdio.

### **Network I/O and Inference Ingress**

A second major area of functional overlap exists in the networking domain.
Multiple Large Language Model (LLM) connection wrappers and API integration
points exist simultaneously within the Python scripts and the Go CLI. This
suggests that each micro-agent handles its own network I/O, error handling,
retry backoff, and serialization logic. This decentralization violates the
principle of a unified ingress/egress gateway, consumes excess TCP sockets,
and duplicates dependency footprints across languages.

The synthesis strategy requires that all LLM connection wrappers be merged
into a single, unified Go-based gRPC multiplexer located exclusively within
the go\_cli component. This multiplexer will serve as the sole
communication conduit to external models, cloud APIs, or distributed peer
nodes. The Go layer will handle all asynchronous network I/O, connection
pooling, and retry logic, leveraging its highly efficient networking stack.
Once the network payload is received and validated, the Go multiplexer will
utilize the WASM shared memory arena to pass the data directly into the
unified C++ inference engine without additional serialization overhead.
This architectural decision explicitly separates external network
orchestration from internal computational logic, allowing the C++ engine to
focus purely on SYCL-accelerated GF(3) calculations without blocking on
network jitter.

Furthermore, redundant text parsing and preprocessing logic scattered
across various Python agents and Go utilities must be consolidated. These
fragmented text parsing modules execute similar tokenization strategies but
incur the instantiation overhead of different language runtimes. All string
manipulation and tokenization logic will be consolidated into a highly
specialized core/inference/ternary\_tokenizer.cpp. This centralized module
will completely bypass standard string allocations, utilizing C++
std::string\_view and zero-copy buffers. Crucially, this tokenizer must be
specifically adapted to output data directly into the
core/inference/entanglement\_token.cpp format, mapping raw text straight
into the ternary state space required by the GF(3) architecture,
eliminating intermediate binary representations.

### **Retrieval-Augmented Generation via Stabilizer Tableaus**

The third critical defragmentation targets the Retrieval-Augmented
Generation (RAG) pipelines. Currently, RAG logic is fragmented across
agents, likely utilizing disparate vector database connectors and relying
on standard cosine similarity over continuous floating-point vectors. This
traditional approach to knowledge retrieval is fundamentally misaligned
with the repository's core mathematical ethos and the hardware capabilities
of the core/flash\_cim framework.

The consolidation plan requires translating the entire RAG retrieval
mechanism into a discrete stabilizer state matching algorithm. By encoding
contextual documents, memory chunks, and reference data as GF(3) tableaus
rather than floating-point embeddings, the system can leverage highly
optimized C++ symplectic inner product calculations to retrieve relevant
context. This entirely deprecates the need for separate Python-based vector
database connectors or heavy external vector search engines. The retrieval
logic is folded directly into the Compute-in-Memory hardware acceleration
layer, transforming a memory-bandwidth-bound vector search into an
instantaneous, highly parallel bitwise operation over GF(3).

| Scattered Components | Overlapping Function | Unified Module / Service | Consolidation refactoring Roadmap |
| :---- | :---- | :---- | :---- |
| 30+ Python Scripts, multiple .mcp-servers | Tool Integration, Context Delivery, Agent Logic | core/agents/mcp\_multiplexer.cpp | **1\.** Systematically extract functional logic from all Python scripts. **2\.** Implement a native C++ asynchronous MCP state machine. **3\.** Compile this unified logic to a single WASM module. **4\.** Deprecate Python execution entirely on edge nodes. |
| Disparate LLM Wrappers (Go & Python) | Network I/O, API Interaction | go\_cli/net\_gateway/ | **1\.** Remove all networking and socket management from Python agents and the C++ core. **2\.** Build a centralized Go connection pool using gRPC. **3\.** Bridge Go network payloads to the C++ core purely via zero-copy shared memory buffers. |

### Continue Reading

| Redundant Text Parsing (Agents, Scripts) | Tokenization, Preprocessing | core/inference/ternary\_tokenizer.cpp | **1\.** Identify all disparate parsing loops. **2\.** Rewrite using C++ std::string\_view semantics to eliminate allocations. **3\.** Map token output directly to the ternary entanglement token structures. |
| Fragmented RAG Logic, Vector DB Connectors | Knowledge Retrieval | core/flash\_cim/stabilizer\_rag.cpp | **1\.** Deprecate standard floating-point vector databases. **2\.** Encode text documents as discrete GF(3) tableaus. **3\.** Implement knowledge retrieval via symplectic inner products natively in C++ using SYCL acceleration. |
| Multiple CI/CD Scripts, Test Harnesses | Deployment, Code Verification | Unified CMake & GitHub Actions | **1\.** Consolidate redundant test matrices into a single, rigorous CMake configuration. **2\.** Create a unified test harness that directly queries the C++ DLL, ensuring mathematically rigorous GF(3) correctness testing without Python middleware. |

By executing this rigorous consolidation blueprint, the repository will
shed massive amounts of operational bloat and technical debt. The resulting
architecture will be an elegant, tightly coupled, and highly cohesive
engine where network orchestration is handled exclusively by Go, executing
unified WASM modules that interface frictionlessly with a highly optimized,
ternary-aware C++ core. This comprehensive de-fragmentation directly
answers the requirements of edge-first deployment, significantly lowering
the cognitive load required to maintain the system while drastically
increasing computational throughput and reducing power consumption.

## **Phase 3: GF(3) Gottesman–Knill QGNN Integration**

The integration of a Ternary Quantum Graph Neural Network (QGNN) operating
strictly over Galois Field 3 represents the absolute apex of this
architectural synthesis. The existing repository demonstrates a profound
reliance on ternary state spaces within the core/ternary directory,
utilizes qutrit stabilizers via core/qutrit\_stabilizer.cpp, and leverages
advanced non-volatile hardware paradigms through core/flash\_cim. To fully
realize the latent potential of this topology, particularly concerning the
Mixture of Experts (MoE) routing efficiency, dynamic load balancing, and
overall representation capacity, the system must bridge the Gottesman-Knill
theorem with advanced Graph Neural Network message-passing algorithms.

### **The Mathematical Foundation of GF(3) Clifford Simulation**

The Gottesman-Knill theorem fundamentally asserts that any quantum circuit
restricted solely to Clifford group operations (such as the Hadamard,
PHASE, and CNOT gates) applied to computational basis states can be
simulated efficiently on a classical computer in polynomial time.3 This
classical simulability holds true despite the fact that Clifford circuits
are capable of generating a remarkably high degree of multiparty
entanglement, such as the highly entangled cluster states often utilized in
measurement-based quantum computation.4 While traditionally formulated and
taught for binary qubits (GF(2)), the theorem extends elegantly and
powerfully to odd prime dimensions, including qutrits (GF(3)).3

In a GF(3) framework, the generalized Pauli operators ![][image1] and
![][image2] operate on states ![][image3] such that ![][image4] (the
identity matrix) and they satisfy the commutation relation ![][image5],
where ![][image6] is the primitive third root of unity. The Clifford group
is defined as the normalizer of this generalized Pauli group. The state of
an ![][image7]\-qutrit system undergoing continuous Clifford evolution does
not need to be tracked using an exponentially large ![][image8] state
vector. Instead, it is entirely and compactly described by its stabilizer
group, which can be tracked using a ![][image9] symplectic tableau over
GF(3). The matrix elements of this tableau are strictly ![][image10], or
![][image11], and all arithmetic operations are performed modulo 3\.


### Continue Reading

The evaluation of core/qutrit\_stabilizer.cpp suggests that the current
codebase successfully implements these GF(3) stabilizer tableaus to
maintain system states. By representing data as qutrit stabilizer
generators rather than standard 32-bit floating-point vectors, the system
benefits from immense radix economy. Furthermore, the Bernstein-Vazirani
algorithm and Mermin's pedagogical shortcuts demonstrate that quantum
computations can often be geometrically reframed as classical linear
computations over GF(3) in the conjugate Fourier basis.7 This perspective
reveals that apparent quantum parallelism is actually a coordinate
transformation. Building on this, the system distinguishes between globally
rotated circuits and topologically twisted circuits. Topological twists
involve non-aligned subsystem bases and are the true generators of quantum
entanglement within this classical simulation.7

This means the project's native C++ implementations can achieve massive
throughput by treating quantum-inspired entanglement patterns as purely
linear algebra operations modulo 3\. Classical algorithms for simulating
stabilizer circuits, such as Aaronson and Gottesman's CHP
(CNOT-Hadamard-Phase) algorithm, prove that by removing the need for
Gaussian elimination, the simulation becomes exceptionally fast.6 In fact,
simulating stabilizer circuits is complete for the classical complexity
class ![][image12] (Parity-L), meaning it can be solved by a
nondeterministic Turing machine in logarithmic space where the acceptance
condition is based on the parity of the number of accepting paths.6 This
incredibly low computational complexity class is precisely why this
architecture is perfectly suited for ultra-low-power edge hardware.

### **Identifying QGNN Insertion Points**

Despite this mathematical foundation, a severe structural bottleneck
exists. The system generates these highly entangled GF(3) tableaus but
fails to efficiently route them through the Mixture of Experts (MoE)
architecture. The MoE framework requires heterogeneous, dynamic mapping of
tokens to experts, but traditional MoE routing ignores the topological
twists and entanglement properties inherent in the stabilizer
representations.7

To drastically improve routing efficiency and representational capacity, a
GF(3) QGNN must be natively inserted into the inference pipeline. An
architectural bottleneck map identifies two critical insertion points:
core/moe/router.cpp and core/inference/entanglement\_token.cpp.

Currently, core/moe/router.cpp processes token-to-expert assignments using
continuous logic. Implementing a Graph Mixture of Experts (GMoE) model 9 at
this juncture allows individual nodes (tokens) to dynamically and
adaptively select information aggregation experts based on the graph
structure of the token's entanglement history. In real-world graph data,
structural diversity is high.9 By treating the tokens and experts as nodes
in a dynamic graph, the router can utilize Dynamic Mixture-of-Experts
(DyMoE) paradigms to increase the number of experts seamlessly.10 However,
instead of computing standard dot-product attention to select these
experts, the router must utilize the GF(3) tableaus to compute a discrete
symplectic inner product, mapping the topological structure of the data
onto the available expert nodes.

The second insertion point, core/inference/entanglement\_token.cpp, serves
as the perfect substrate for defining the edges of the QGNN. In traditional
GNNs, edges denote fixed spatial relationships or learned, continuous
attention weights. In this quantum-classical architecture, an edge between
two tokens (or between a token and an expert) is strictly defined by the
degree of non-aligned subsystem bases, or topological twists, that generate
the quantum entanglement.7 By redefining the tokens as discrete nodes in a
GF(3) QGNN, the entanglement tokens inherently and deterministically form
the adjacency matrix required for rapid message passing.

### **Mathematical Formulation of the Ternary Message-Passing Protocol**

The architectural integration requires a concrete, mathematically rigorous
translation of standard continuous GNN message-passing functions into
discrete ternary logic gates compatible with the project's SYCL
acceleration layer. Standard differentiable message passing must be
entirely quantized into GF(3) operations.

Let a graph ![][image13] represent the MoE network architecture, where
nodes ![][image14] are either entanglement tokens or MoE experts, and edges
![][image15] represent the entanglement linkages (topological twists)
between them. The state of each node ![][image16] at neural network layer
![][image17] is not a continuous vector, but rather a stabilizer tableau
row ![][image18], representing the discrete generalized Pauli operators
![][image1] and ![][image2] components.

The traditional continuous GNN message-passing formulation is typically
given by:

![][image19]  
where ![][image20] represents the direct neighbors of node ![][image16].

To map this directly to the GF(3) Gottesman-Knill implementation, we
redefine the MSG and AGG functions using strict Clifford group operations.
The message MSG from node ![][image21] to node ![][image16] is generated by
applying a two-qutrit generalized CNOT gate (a GF(3) SUM gate)
parameterized by the edge state ![][image22]. The SUM gate acting on
control qutrit ![][image23] and target qutrit ![][image24] maps
![][image25]. In the tableau representation over GF(3), this corresponds to
elementary row operations modulo 3\.

Let ![][image26] be a learned weight matrix composed exclusively of
elements in GF(3), defining the layer transformation. The message function
becomes a strictly linear transformation modulo 3:

![][image27]  

### Continue Reading

The aggregation function (AGG) presents a unique mathematical challenge.
Standard GNN designs differ mainly by their combine and aggregate
functions.10 They typically use permutation-invariant aggregators like SUM,
MEAN, or MAX over floating-point values, which do not translate directly or
meaningfully to discrete stabilizer states. Furthermore, research in
molecular property predictions has shown that simple sum aggregation often
fails to improve predictive accuracy, leading to the development of
learnable edge-to-node mechanisms like "patch aggregation," which is
heavily inspired by Multi-Head Attention and MoE techniques.11 Patch
aggregation significantly improves accuracy while remaining
parameter-efficient.11

To adapt patch aggregation for a GF(3) topology, we utilize a weighted
discrete superposition based on the symplectic inner product. The
symplectic inner product between two stabilizer rows ![][image28] and
![][image29] is defined as:

![][image30]  
We define the attention coefficient ![][image31] (the routing probability
or patch aggregation weight) not through an expensive exponential softmax
function, but as a discrete mapping of the symplectic inner product:

![][image32]  
The GF(3) patch aggregation update rule is then elegantly computed as a
sequence of Clifford SUM operations weighted by ![][image31]:

![][image33]  
This formulation is entirely classical, strictly linear over GF(3), and
perfectly bounded by the efficient simulability constraints of the
Gottesman-Knill theorem.6 It allows the neural network to process highly
complex structural and entanglement data without ever requiring the
hardware to convert states into power-hungry floating-point numbers.

### **Code Implementation and SYCL Hardware Mapping**

Translating this mathematical formulation into the C++ inference pipeline
requires meticulous mapping to the SYCL framework to ensure maximum
execution efficiency on the edge Compute-in-Memory hardware. The
core/moe/router.cpp must be completely rewritten as a series of specialized
SYCL computational kernels that operate exclusively on GF(3) packed arrays.

The implementation strategy must prioritize cache density and memory
alignment. Each element of ![][image34] mathematically requires only
![][image35] bits (![][image36]). To maximize hardware cache density, the
stabilizer tableaus must be aggressively bit-packed. Four qutrits can be
seamlessly packed into a single standard 8-bit byte because ![][image37].
The C++ implementation must provide custom bitwise extraction and insertion
operators for this packed format, effectively turning the memory interface
into a high-speed radix-3 bus. The shared WASM/C++ memory arena,
established during Phase 1, will exclusively hold these packed byte arrays.

The SYCL abstraction layer allows the execution of highly parallel code
across heterogeneous devices, including GPUs, FPGAs, or the specialized
Flash CIM controllers. The QGNN message-passing kernel will be mapped to
SYCL work-groups using the following execution paradigm:

First, in the node mapping phase, each work-item in a SYCL work-group is
deterministically assigned to compute the patch aggregation update for a
single node ![][image16].

Second, the system must aggressively utilize local memory. The adjacency
matrix (representing the entanglement linkages) and the stabilizer rows of
all neighboring nodes ![][image38] are pre-loaded into SYCL local memory
(the fast shared memory within the work-group). This explicit caching
prevents catastrophic global memory bandwidth saturation, which is the
primary cause of latency on edge devices.


### Continue Reading

Third, the implementation must optimize the modulo 3 arithmetic. Standard
modulo operations (% 3\) in C++ are computationally disastrous because they
trigger integer division hardware. The SYCL implementation must utilize a
custom lookup table (LUT) or heavily optimized bitwise arithmetic tricks to
compute additions and multiplications over GF(3). Given the incorporation
of core/flash\_cim, these GF(3) operations should ideally be pushed
directly down into the physical memory controllers. By leveraging the
physical threshold voltage states of the Flash cells (which naturally
support multi-level cell paradigms capable of representing 0, 1, and 2),
the system can compute the SUM gates natively inside the memory array
without ever moving data across the bus to the CPU's Arithmetic Logic Unit
(ALU).

Fourth, the system must implement the Expert Choice (EC) routing
algorithm.8 Traditional token-choice MoE routing often suffers from severe
load imbalance and under-utilization of experts, requiring massive
over-provisioning of expert capacity.8 Expert Choice routing eliminates
this by allowing heterogeneity in token-to-expert mapping and having
experts choose the top\-![][image39] tokens, significantly reducing
inference step time.8 In the GF(3) QGNN, the SYCL kernel will execute an
inverted traversal. The experts (acting as specialized nodes in the graph)
will compute their symplectic inner products against the token pool
simultaneously. They will select the tokens that exhibit the strongest
topological alignment (the highest symplectic weight), resolving load
imbalance natively and deterministically within the GF(3) domain.

The final structural flow for the new core/moe/router.cpp replacement
involves a highly pipelined five-step SYCL execution:

1. **Initialization:** Initialize the SYCL queue and bind it strictly to
the target Flash CIM device. Load the zero-copy shared Arrow/FlatBuffers
memory arena containing the packed token tableaus.
2. **Symplectic Attention Kernel:** Dispatch a SYCL parallel-for loop. For
each expert node ![][image40], compute ![][image41] for all available
tokens to determine alignment.
3. **Top-K Selection (Expert Choice):** Utilize a SYCL subgroup primitive
to perform an ultra-fast parallel reduction, allowing experts to lock in
the tokens with the highest symplectic alignment, guaranteeing perfect load
balancing.8
4. **Message Passing Kernel:** Dispatch the message generation. Execute
the GF(3) matrix multiplication ![][image42] utilizing the bit-packed
memory format.
5. **GF(3) Patch Aggregation:** Perform the final summation over the
dynamically selected sub-graph.11 Extract the fully resolved stabilizer
state from the SYCL buffer back into the primary shared memory arena,
completely ready for the next layer of the inference engine.

## **Conclusion**

The comprehensive architectural review of this multi-language,
ternary-state system reveals that achieving unprecedented edge-compute
efficiency demands rigorous de-fragmentation and an uncompromising
adherence to non-binary mathematical foundations. Resolving the deeply
entrenched competing frameworks by actively deprecating dynamic Python
orchestration on the edge in favor of a highly cohesive Go-WASM-C++
pipeline provides the structural and deterministic integrity necessary for
hardware-level acceleration. Consolidating the scattered MCP servers,
disparate LLM network wrappers, and redundant RAG logic into unified C++
native modules completely eliminates profound operational waste and memory
latency.

The crowning synthesis of this architecture is the mathematical and
programmatic integration of a GF(3) Quantum Graph Neural Network into the
Mixture of Experts router. By systematically replacing continuous
floating-point softmax mechanisms with discrete GF(3) symplectic inner
products, and mapping complex GNN message passing directly to classically
simulable Clifford gate operations, the architecture aligns perfectly with
the boundaries of the Gottesman-Knill theorem. This approach natively
leverages the physical multi-level cell properties of Flash
Compute-in-Memory hardware, utilizing extreme radix economy and topological
entanglement to achieve massive throughput. The disciplined execution of
this blueprint guarantees the transformation of the repository from a
fragmented experimental codebase into a mathematically rigorous,
structurally flawless quantum-classical inference engine.

#### **Works cited**

1. AI scaling with mixture of expert models | by Jeremie Harris | TDS
Archive \- Medium, accessed April 5, 2026,
[https://medium.com/data-science/ai-scaling-with-mixture-of-expert-models-1aef477c4516](https://medium.com/data-science/ai-scaling-with-mixture-of-expert-models-1aef477c4516)
2. florimondmanca/asgi-lifespan: Programmatic startup/shutdown of ASGI
apps. \- GitHub, accessed April 5, 2026,
[https://github.com/florimondmanca/asgi-lifespan](https://github.com/florimondmanca/asgi-lifespan)
3. Quantum Operations and Codes Beyond the Stabilizer-Clifford Framework
Bei Zeng ARCHIVES \- DSpace@MIT, accessed April 5, 2026,
[https://dspace.mit.edu/bitstream/handle/1721.1/53235/535632395-MIT.pdf?sequence=2\&isAllowed=y](https://dspace.mit.edu/bitstream/handle/1721.1/53235/535632395-MIT.pdf?sequence=2&isAllowed=y)
4. Classical simulation of quantum computation, the gottesman-Knill
theorem, and slightly beyond \- Rinton Press, accessed April 5, 2026,
[https://www.rintonpress.com/xxqic10/qic-10-34/0258-0271.pdf](https://www.rintonpress.com/xxqic10/qic-10-34/0258-0271.pdf)
5. Qubit code | Error Correction Zoo, accessed April 5, 2026,
[https://errorcorrectionzoo.org/c/qubits\_into\_qubits](https://errorcorrectionzoo.org/c/qubits_into_qubits)
6. Improved Simulation of Stabilizer Circuits \- Scott Aaronson, accessed
April 5, 2026,
[https://www.scottaaronson.com/papers/chp6.pdf](https://www.scottaaronson.com/papers/chp6.pdf)
7. The Geometry of Clifford Algorithms: Bernstein-Vazirani as Classical
Computation in a Rotated Basis \- arXiv, accessed April 5, 2026,
[https://arxiv.org/html/2603.12127v2](https://arxiv.org/html/2603.12127v2)
8. Mixture-of-Experts with Expert Choice Routing \- Google Research,
accessed April 5, 2026,
[https://research.google/blog/mixture-of-experts-with-expert-choice-routing/](https://research.google/blog/mixture-of-experts-with-expert-choice-routing/)
9. Graph Mixture of Experts: Learning on Large-Scale Graphs with Explicit
Diversity Modeling \- arXiv, accessed April 5, 2026,
[https://arxiv.org/pdf/2304.02806](https://arxiv.org/pdf/2304.02806)
10. Dynamic Mixture-of-Experts for Incremental Graph Learning \- arXiv,
accessed April 5, 2026,
[https://arxiv.org/html/2508.09974v1](https://arxiv.org/html/2508.09974v1)
11. Graph Neural Network-Based Molecular Property Prediction with Patch
Aggregation | Journal of Chemical Theory and Computation \- ACS
Publications, accessed April 5, 2026,
[https://pubs.acs.org/doi/10.1021/acs.jctc.4c00798](https://pubs.acs.org/doi/10.1021/acs.jctc.4c00798)

[image1]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABIAAAAXCAYAAAAGAx/kAAAA30lEQVR4XmNgGAWkAi0gvgPE/5HwNyC2hcpPRpO7D8RqUDmswBKIfwLxbSCWRBJnB+L1QFwPxNxI4jgBJxDvAOJ/QOwBFWME4lIoBrGJBhEMEOcvB2JWBogB3VA2SUAciK8D8XsgbmaAhA/JhsBAKwPEVYeAmB9NjiTgxQAJpxMMFBikCcT7gPgWA2qgEw1ANq8CYjMoHzmsdGCKCAGQIeuA2BtNvIEBElYgmiBQBOKNQFyILgEENkD8G4hPAbEwmhwcxALxLwZEsv8LxP5I8llQMWT5nUAshKRmFIxsAADUlTDEUF3cxQAAAABJRU5ErkJggg==>

[image2]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAA8AAAAXCAYAAADUUxW8AAAA2UlEQVR4Xu2SPQoCMRCFR9RCLGwFC2vRwkJsbdQL2HgDG2sbLyIieAwtrETwCIJgI3YWnsCf98wmJEOQbYX94GNlJlkfMyuSQcbw7fmEt0T+Zm3hTnvk4Bpe4QDmvV4XPuAOVry6owaPsKnqDXiBZ1hXPUcfzlWNh3npDjuqFzAU8y8WxmNMxmXs1JTgSsyQRqr3kyJcipnsTMwgU8GDvPBKnvZiAbbFvDgK4zFmbB1TOFE1h93jHlZVjxM/wJaqf7Hr0HtkxB48wa2YIQaU4UbCTzImY2f8Jx/pwS3KwXoejwAAAABJRU5ErkJggg==>

[image3]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFUAAAAXCAYAAAB6ZQM9AAAD3UlEQVR4Xu2YTahOQRjHH6F8hq58RHSREhv5isgGiVBYKMpCPrJigUJRkhILboTESkrKys7C107JAjuJRBRWSik8P/Oee+c878yZM+e9G3V/9e/ed+a857z//5mZ88wRGWCA/43RqsG2MQHHj7CNmQxVjbSNNeC6ub/XMqylHLI8n1JNto0JJqouqIbbjgwWqPbZxhrsVa21jZlsaCmHLM+hUAeplqguqi6r1kt5dNB/VrXUa8slFep81RrbqMxTXVINsR0ZhELFHz7xi7ZKOcAszzZUvnxY9UjVrepS3VJdEzdlC5arjnufcwmFyo3k2s9Uf1RHyt3/IMwe1QzbkYENFV+c84Rqpmq36ofqpWq6d1xtzzZUzH4Wd4ICDLyT8rQbo7opblo0IRYqZteJMxUKFbap9tvGDGyo+HqgmuK17RB3Y69L36yo7dmGelpcgH4bD7Mn4k7ISC7A2Ebvcw6hUAvoqwoVU1dUo2xHTWyoXKcIsGCq6oPqjZRDrOXZD5Un4n1pD5Uf/1DctBzntc8RN22arG+dhApnpOb6FsCGulD1QrXHa8M/Odgsann2Qy3CsyeKtRfrGxfKpdNQCZRgm2BDDcHy90t1T8rlVy3PfqixuxMLFZqub52GWnt9C5AKlQcX5/6uWmz6IOnZD5UfyBpiw6sKle/wAzCZQ6ehAsYwmEsq1C2qT6pVtqNF0nMn0x+o5Si3ppn2FP0RKk/t1DEhqkJlZD5XLbIdHknPfqisF3elPbwiVCoAKgEfivHzUq4K6tBpqFyPSoVjc4mFSqBPVbNan8mD0mps7xGOpGdbUmHkq5QX4vGqV+J2WJZj0r5tZHcyQar3152G2i2uBLLbRtZDru1vVCyhUCnyGVB+sY9vRqQdSCHPJWyo3CXqM3+tWqH6Iu0lDOXV1dZfn13i6r7bEi896oR61HZ4bG/Jwgji2idNu48NdZLqseqb6r0nPN+RsoeY5xI2VNiseiuubtupei3uoWCHO3eLu2bhB/8U99Cz5y4Ihcrnj+JCKYRRlh1GXwGjk1HKaLUcUP0Wt0OKbQ5sqEXxHxJLjE/Mc4lQqNAlfRfnfwsBMypYX0Iw9XskXvKEQq0L38WsvckFTFuuHXu1aEOtS8pzL7FQUzBK2CrG1k36z0mz6Z+CkRIrd4BlihczMZqGmvLcS9NQeZMTqxF5UBHoatvh0TRURuENideITHn659oOj6ahVnku0SRU1jTumP9Wx4fzbZL49ISmobKmHbSNHlQtK22joUmoKc8leFKH1swqqBAOSXVoKWZLjbc9AXgQVe67a7CspRz6w/MAufwFKr3D+3oSC0QAAAAASUVORK5CYII=>

[image4]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAG0AAAAYCAYAAADwF3MkAAADQUlEQVR4Xu2YWahNURjHPyFj5gx5kCFlKmUoIS/IEDJkKqUkkgjhAVESwguKEKUkHsiDqYRuyhgeSBlSUh4kLyhk+P99e5279jrn7LNOnfbe91i/+nXPXmvde9fe31rft/YRCfzXtIHL4G64PLpuSjSHs0Xnvxl2iHfXH83gXjjf+nwWtrAH5ZyVcKPo/JfCe7CjPaDeYHBOw5OiK5Yr9SHsbA/KOVvhTdEdNgN+gP1iI3LKYPga/rH8BsdH/YecvrdwYNRnYFq8Ajc47VmwUOLz/QXfR/Iz244WRivcaYfhEck2UwyAT6V4/ufsQTZj4Hf4Cvay2lvBi3A7bGe1GxbDJ/C4lO5PEz78U/AdnCSaAQyj4Wd4Q+IpcCy8Dy/DHlZ7lswUDdhBt8OFu+Ua/A2nRG18CMz5Ju8nsVqKH0ja9IZ34RCnfRB8A1/CPk6fgff8TMr3p8ku0aAxeBUxqYUHipaiwdoXfXZhGw8h/aNr1gQGfGJhRPrwf29x2hgEBusjHGm1cxEyUKZtBPwKVxRGZEN7eFuqqK9MDy9E08hO0XpWKmCEq/cTXB9dz4mu2Z4VkyX+/7nruft5P0yPNt3gc3giuh4lOi7LRUcYKAaM8/YuN2ZrNkhyqmMwD8C1cBy8JZoiK6XRtGC6Z0BYyOc6fYTzXAP3iNZzZpf9Un6RpoWpZ4yDN9NE05zvO0sn0YMLt7UP20QPCr6yznb595v+8MEfE735TZK8kDhvzp/34cMiKZ5jko9ET4W+mE0z1e0oB1ML31tYA+wDSVOCAWKgOH87YDzKD5fsd1ISVdczFuxLoqvCPpBk+d5SLQzQAtGUyJ1mB4gPgTU6z/fDOXrXM6bB89JYrO0DyVAzqEaYdORrd4m/byXB2sWAlXr1YL2txcmwtRTPMUk+S9/d7V3PeHMX4HSnfYfoH+DPWjIMzqtC5nafL6LNy3MD7On0MYvckdosQP4td45JzhL/muz1ftZXNCWucztET4Q/4QPY1enLG+ZdzH155gqfIJo1eKDxCX5WVKxnS+APiX+/xRVhWBW12f3XxX/FpAlz/1VpnGs5mR7zCL/HfSzxeHyBZ2Bba1wgEAgEAoFAIBCoX/4CuoPH0sPp2NkAAAAASUVORK5CYII=>

[image5]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAGQAAAAYCAYAAAAMAljuAAADTElEQVR4Xu2YW6hNURSGfyFECLlERFLuipQiL0gUhVwePEpJiqLkdkqeFIUHSUkeSLk9CCUJRVJeiFxSEg8e5IFCLuPfY41trHnWWmfvtY+tfZpf/Z29x5zrzDnmGHPMuTYQiUQikUjnsVb02+mn6F0ifqbteLU3MEn0KrGbvormJe1Hg7Y3oglJWyvQGf61Id3nu+it6L2zbbHOnm6iU9DOC0XdXdts0SfRTdEAZzfmiL6JXopGOHsv0SXRPlFfZ281yvrXT3Rb9Fg0DbrGxkpokp8Q9XT2KiNF90WTA/tE0WvRC9GYoM3oI7ou+iVanNg4+PZEfiKtSFn/ZoruioYG9gUoTvAK7LQrsDEADMRH0aygLcTK3VloxDnRg8nnrkAZ/zZAn/NYtXmC/ASvsAi6GwxGjhHkw/wnHTFM9Azafz+0vhZNtpn0R7rUhExNVEQZ/9YgPa4leFG1yYRb9CS0xrHW1coBaBbdQcFWbDL05YJoRdiQMBp6bj4VDQnaQhrxb5DoFmpP8CqMOg8aDrwD+fUxiyXQOvsA9U+YrIMuTq16JBpfeTIf1vzcgxNqPwQ9J7kLiijrn1WbehO8svgMAgf1weghmoF8pwjLHTOA29Effv8TzpvXdB6uxijRHtFgZ2M7d1HWTcko658leBgM7lyuaW7Cs4E1L+sqNg5aM+lgFqyHV6DZ6g+/vP7NghnPefd2tm2iD1CfjLmi3e57SFn/+OqwE+0TnHQ0ZvVenHUV2yzaGNgM9j2Pv3XRH35TrFONcOF4CNYqjlW0a5n5DIjnNNKLyUXaC71lZlHWP6s2DF6Y4Gw7jPwxq1cxHlbDgzZmxz1kD87JXhQtDext0Inwbz1wrFV1aDn0sMyDAbkMfUEjnC995HXUoO9Xk7aQRvyzBD+D9qWQY3IemWdW3lWMEZ0PzQa+FLHmecZCt/HWwE64HX+IHiJdq5sN6z7fobgj+PPEc+i8vkB3xRHRZ9Eye8DRiH95v2ywAqyGzukYMs4PRu4a0r+1ZIkly1gP/T3G2pgFzFRjU2Lz7TdQnMn/Cn9jpM6JpkNfyvidfnC+fmEa9Y9v5UzucA296rkUdDm42Mzigc7GQLFc+MM+EolEIpFIJNIl+QMO+/gIUsJrAwAAAABJRU5ErkJggg==>

[image6]:
<data:image/png;base64,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>

[image7]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAwAAAAXCAYAAAA/ZK6/AAAAxklEQVR4Xu3RLw9BURjH8WfDxuZPYDNBoMloio2NoOgiumKjkLwGb4CiCYqmqLpiUwWBovjee8+5zs5MFu5v++zuPM/Zc889VyTIPyWJNvJqHUEFHWT1Jp04lpjjih62GGCKOxr+btLCEGU8sEdK9XK4YKzWbvoooYsX6kbPqd8wMmpuEjhgg7CqOc81jvJ5o59vkwrifdMMMSyQ1k19nJouiHdDT1TRxMTouVNOYkwgRZyxw0qsY0XFu147zv/IIGQ3gvzKG7exHGm/doWYAAAAAElFTkSuQmCC>

[image8]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABMAAAAXCAYAAADpwXTaAAABSUlEQVR4Xu3TvyvFURjH8Uci+Zkfi8RgEFHIimw3A5PBqCgGEQalFKPBYjSRRFa5sikm/gQsBptEWZR4P57n6NzrXuRO6n7q1f2e55zv+XG/369IPrmkErPYxCpaUBD1D+EA7ZjAMcbSxnykE6cYQDUm8YIFscENGMcSTlCDer9Hf1OygVcMe1snvMQ92sRubsJhNKYH56jz9mfW8Sa2uqYCZ3gS27VGd6e1Zm9Pi23iS4rEVij0dgcexI5R7rVeJL2t9LoPM5JhdyH6IHZxi66oPo8Vv9bJ9rCMRBgQpwz7YpPciA0KO9UUi50gRPtKo3bWtOIOO2KL5BR9HfSo+lCm0vq+jW59zsXHWBSbbDuq/Rh9X56dXofoJDpZxsefLY24whaqvFaLC7HXo9trv84grrGGURzh0et/SonYtzmCfkn9//L5D3kH1801W/Wk4p0AAAAASUVORK5CYII=>

[image9]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAHMAAAAYCAYAAADJcMJ/AAAEwklEQVR4Xu2Za6ilUxjH/3KJ3Gfcrwfjg0tR7kLnA+IDiUJGEknJB4xLFDaSInKrkTANyf1SaNxiQghfReIDuYT4oPgi8f/N866z1157n/e87x7nzD67/at/Z/Zae797ree2nrVHmjBhwoT5ZCtr83JwzNjY2t7aqJwYJw61VlnblhNjBk5cYV1Z/XtWdrVutB62brGW9U6PLHtY71oHFeNbWOcp9nO/daIishcDrPNsa6oYh02tx62zyonEkdZb1vHWIdZr1r+KKKiNgA0Ma7vX6hTjZOiL1iXWftbN1j/WmmpuFCH4TrPus36w/rQO63lHl4Otj6w9ywke8rJ1kbqRu9T6RPUPHAXY1JfV35zLFdG7TfUap9+mCNDr0pvmiSusU8vBBuAHPjetCL46229iPaX+IF5XXr+1/lBkZeIGxeavysbWly2tncvBDIy+m5qXQxxDFSkbn9XqX/sR1l/W24p1zBesiQxbH3hGnTNhufWpoiGagRpMar+hcGyCB+aRjKFx9gmKzwCOYeGHZ2N17Gg9q3h/Cc+/0HpAzZ6FA3EkQVdyhvW5dVI2hmEw0FpF5wvsl2ygEgHPnLZO1vDleKGciS9IQoK0FtL4ecU5M12NscC7rPesR62brJXWudaH1guKUjEXNCyvK87pRFtHQqooTQ1HJBOcd1evWcdj1esvFN//SvU+GibOrQOq97ZhoZyZ9n9mOVFylOJhdIIYFyeRvWQiJQwn590UX86D88yuI3foMI4ENvqzdVw5MQCyjCD8ytq7GrvUOkVheJyc9gopi4dxykI5k+qyVnP0AGycc+UJdc8WuibuNsy9b72kXsMTyd+o/jwsSQ59UO0dCWz0u+pvHQTLtdbX6mYalQcj7KQo079Y+1dzwDWGgK1rZFgv+yWAc9FoXTBgnO9q0ws0debtxfgMLPAh6x4NLpkY4zdFVCdw8MeKsoyRmoKRaVB+tY4t5prQ1JlUEAJwqhiHdO6WTVTH+snaNxsrYc1kc6nPrDcHjFPZpvhgA9o485FifB3JkderG0E4j2YgQX3+W72l7RhFl8jZ2RQcyfWBjNxLken5GdqEJs7EkWusJdVrDMCZmBxHdfhevdGdgpPWv01wJjZ4mcW4K9T/MxEZmB+wg8ppR2GQfRTdI8aqI3dkKq27qL1DyZofNXsp5FlPqrcrJTjJkLTHQeU0D05K4x3WZtn8XCyUM7mScDXpuTqmBoQN4BSiPel3dbNwa0W5ystpig6Mxvm6UuHU2eC7LlP8alOekW0duoPi+pGX/ARO44zkLCz3k2ch52VZTjEkdiBrefY52VwT/i9n4o+6awfrI7EIyBlSi0tHVyrfKF0gm7y4eg04p6MwHM0M97s6DrRuVb8jE5TDO6u/c8F3r1JUi5LV6t9LUqocHCXPKAIoPy8JXjL+VcW1Zba1zsawziQZnlYEXL5e1kIPU8I6Cebdy4kmYDxSe1BHtp26F/GFhFLIz449v4K0gDWXvx4BY0vVe+Q0ZVhntqWj4c/1kQSDf6C4L44KdLnD/NjQBvb9juI/RsaK063nNPgqNa6cr7h5tD0CRh5K4TWVhimLi41liusWPcxYQoRebR1dTowZ3CjoxulkJ0yYsCj4DyPB9F9z2X0bAAAAAElFTkSuQmCC>

[image10]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABwAAAAXCAYAAAAYyi9XAAABV0lEQVR4Xu2VTytEURyGfxaKUpKFlAUWZCFKFiysbCxIsRA+gWxRtrLxBZRsLHwENgpLZUVhTcrKjgXlz/POnZMz5865c5WZheapp2neczrvmdvMb8zq/CeacRH3cAf7S5d/TTcuhKGjFU9wC1twGG9xzt+UgwFcwVP8wIPS5R828BLbvGwJ77DDyyqhwlkcx0eLFKpEZeHiKL7gTJDnoRPvLX1mAd3q2dKLI/iK20Geh8xCd3C4GMvzkFk4jV+WXqxa4ZTVuDB2cCzPQ2ZhLz5ZetEVbgZ5HjIL9UM/xyNs8vJJfC++OrS3HRu8rByZhWIZH7Cn+F4HaupcWDKFhIqu8A3HilkMV3hokcs14i6eWTIpVHZjyYhz6NMd4yeuebmPnoYmjMaavohSw+MaB719BXSTPpzHCUsuUQ4duhqG1WTdKj/SP2MI9y35K6sJmkpdYVjH5xv8w0oXUU7eagAAAABJRU5ErkJggg==>

[image11]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAoAAAAWCAYAAAD5Jg1dAAAAxUlEQVR4XmNgGFpAEohrgXgWEDcCsQqqNASYAfFuILYFYn0g3grE/4G4GIgZYYo4gXgDECcBMTNUTBiITwHxVyA2hoqBrXwIxJ8YIKbBQBUDxNQimAArEE8E4p0MEE0wUM4AUQiicQIWIF4DxH+B2AFVChWYM0DcBwoBkI0YACQ4A4j7GCCexApgiioZEL7XBGI3uAoGSFiBwqwQyoaBdCAOgnFAEglA/A2InwDxIyT8DohtYAph4QgKCnT8HIiVYApHHgAAyI0k9Y32SQwAAAAASUVORK5CYII=>

[image12]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABwAAAAYCAYAAADpnJ2CAAABoklEQVR4Xu2UvytGURjHH6H8KvlRskl+JIqSLIQyUDIY/AMWmSgMLBQpC/ImLF7KZJKUQTGy+Q/IQFksJgO+3/fc897zPvceltd2P/UZznOee557z3nuEUlICJmCH/DbkeM0LAnT8ks5vIbvsEPNxVIIJ+EDfIGf8Amew15YkM2MpxG+wltYkTsVpRIew0VYCuuDMR+sgbtwAxbbB2IYhl9wXU9ouMg2HHNibkHCr1sI9H0pC/HsxvWEZgTOS+5CuiDhi6VguxOzsDEuxWwpt9ZLEdySaFJcQcK3n1UxYs+PTcPm8VIF92GzmCLWTngGm1R8EO7wQcVf51cmpikzi9zDI3joeAofPfE9Pqiw5zeqJ8Q0IV+yjgN+Pjuw1s0Q/5b2wDUVs/+f7/z64aY4PbIspnFcfAXnJJr72//H3+0C9rlBnhMX56QlriDz0ipG2EjcTn1+rWI6905y184wAU9gdTDWBdvgFewKxmQavkl4d/JmehZzS7l36kqQH2FATAMtidkCdukQPIA3sCGbmUfYut1wVUxXzsAW8d8uCQn/yw9NfFIgc9OqmgAAAABJRU5ErkJggg==>

[image13]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFgAAAAXCAYAAACPm4iNAAAD+klEQVR4Xu2YWahNURjHPxkyZY5M3a4h6T5QppSEEMkQCrlvuvHgiaI8XUlS8mBKIlGGDCFj8iAPEkoeUIaSlBBKETJ9P+ssZ++199rDce3TrfOvf6fW2nuftf/ft/7ft7ZIDTXUUBzaKnsq27gTVUR3ZXt3MAt4maHKBcq5yn7h6cLBS2xWLnQnCgABHSBGB1eL8cojYoTOhG7KrcrPyl8B/lBOKV9WONYqt0s5e1cqvynfKJcExoOYIeY9tkhlWcYzpykfSViL18oRgesalXslw3+MVj5UrlZ2KY2Rydz8Uzm9NFY0GpR3lPWBsd7KJuUnMcFHTBfrxQjyVTnRmUsD4hLUu8rhpbGuyqsSFbiT8ryk7C5S/ZVykTNubz4hGSL0H8CL7ioxLkvJXoJ/TNnOmeP6ScoPYrZ2HsxTflHOCoz1Up6V+B2zVHlTPFbB4A3lfokucpXyvrLOGS8KA5WPxb97WPst5UvlIGcOkHWIMsadSADi4avYyzhnzochyqdiAhoB6vOwuG3UWYxNVAsI+0zixbPYIX4xhilPiSezPCDJuAd7Oa3sG56OBZZ6TbnBneiovCjG42iB4kBErScXDXz0uphM9IGChxiuDSDUbjG7MC/GKt9KubCxQ6aGrojikJjMD9kHmcHNiIzYLoj8ZTH2kQQyhYLwIgeX/bkzGSyabHKtKwiEjROYekJRypO9FuhyRnlPzA6ZIFHfdRGbDHgTldgnMPbB4pe7EwUBgWES4gSmaOPd/OYF2ftETKFLEzUIBI44wSjlRzG9nnuYsMXvnXKkM1cUKhGYdvO5VGYNZC5aIFZecA/1IqQjaqM6C1wn5YjRkm0rjV8R064lgUJIMeifg0m+apFH4NlSFjf4LnnQLKZdpStwwTvSf/uKfqxFALyKZp1F4jkHxESRvu67mINHGiiCc5SLczDLruB4THVOKrJrxNgcFfy9mMTw9ew2EeLs0CZbXMtHgh1W7hP/s71rJdKTxRQTLuAEM1hMNPmz+r9XFg86hAfKPu5EAPbEBpvFLwBYIea6uIOJ7amZp7BZofh8sEfMYctXMNGQDoL7MgGBKRKIXU1QhCk4SdmOhbAD2WlptoCdcELDK7EpF+xCdoMNGP01J0GenRQ4sv+2mMIYgs3e48r5yh5itgNt2UnxR6wo2KzydTFk2VExH2WyAnvYKdGibsEzZ4qxsQZJFtaCQxq2GtntGPI5KUcM8pVqo6QXtqLQqLwkLbceRMCnXYuoFCTpphK9O4iosmWIapaIFQmS4ILEfzHLC4oc4rbEsyw4ZFG76tyJ1gQWz07715cgibBCb6blBMl4UFI+VbYWUOiwrg7uRBXRJOFPmjUUid9aZ8wC9Tq4LgAAAABJRU5ErkJggg==>

[image14]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAC8AAAAYCAYAAABqWKS5AAACGElEQVR4Xu2WwUtUURTGP0mhSMooiFIxwU2rikgIsoXowoUgKug+RJFWQQURMbsIdKNoIi6yhW5cuBFERFoG/QeCoOLGFgWCQoHW9815t5m58951RgVbvB/88M279705c8859wqkpKScF4P0N/1O+2hF4XCWdnpA39Mqb+xcuU4H6D49hAXq85r+ob/oY2/szKihtzwvFsxIRqt+ROdppTembDyhP2mnN3Yq9OJW+o0u0mk6SZ/RXlqbmxrkKv1Kd2idNyaqYe9/6G5ciD48Ra6WFMw92uAmBdAz7+gEveKNnYQxWG0/8gdIE12A/cjsF4/Sl7BfrGuhwPfoKr0c3UuiGxb4WTWRmle17ZeGykjfM+RuqDHe0muw4Gej+5foJ/oFlqokNG+GNvoDp0BBxwXfQ1cQrbpQh9+Fda9S1e8GYM0xnvc5DjXjFK2PruO8ifKyEhd8M12P/haRgTVJ/gqqHJTCEApuDdagIe+7B0rAD17PbiKvXPJRyWiXUCO47UkNm4FlJcQN+hHh0ioXF3wHcoG/QvzBlV29Ldgh4FAGRlC81/rohR8Qf6iclBeww+oN/QGLI7HsVJMbsMlCExXQg38zwmg7XcLxWSoVd5LKDAKBC63eMN2ln+ky7SqYcTx3YDuB9vvbSEhxiWjH078Iz1HGe3SEl3OU++iwa4NtscrkdqSuW3LTguhMmYOd1ikpKSn/AX8BJItY9+BqdQsAAAAASUVORK5CYII=>

[image15]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADwAAAAYCAYAAACmwZ5SAAACo0lEQVR4Xu2WS6hNURjHP6G8xfUMCZFrQnlLBl6RR3kk16PMxADlFaZMlISkGMgAIVESXUoxMZKMZKIkEzNhQB7/X9/e9trrnHvsczlxbvtXv66717r2Wut7rG1WUlLSJPSSs+R6OUP2zA93HQbI0/Kb/BF4IpzUVRgrn8l9lkV0pfxu/+mG+8mRkTwrAvPa5X3ZO3jeJq/KEcGzf84U+Ug+lOcTd5rXX2swrxYz5Wd5WXaLxmoyXK6SC6zxhc7CtsnrcnR+qG7my6/yk1wju+eHK5kkn5qHf6M8Im9YPj3+NtPlTTkwHugEBOecZU3qo3m0+4STUqbK9/KQZekwVN41j3gj4D3H5dx4oJMQ0U3yrbwmd5t37AqI4B3zieOSZ2zyrNxvddZDHdBkeMdkq2xUodynvyONLhk5OBqrgOh+kF/kG/O05uRJt0ZtFtgwtXvRskZVzeXpH9Rgr3wlx8QD1Vhsfk8djAcaDJEjwqPigTohK8nOY/FAAgebu9rSdk5njqHgqQ3S5Kg8bJ4+S8xf0D95dsGyxrPIvMMXYXvin7DZvEmxjhg692vzT8xfsFDSOD4hDoKOPcT86iD1H5vX1Wx5W26Rc+Q98+ZD1G6ZZ00ReDdpXXR+NbhR2PA7Oc+8DHGhfCFXZFMzpsnn5kVP3XAAJ2Vf2UNOlKvNF8fvRHaX+VXGhh/IQeZ36ZPkZ1HIHg6W9J5gBe7PCA6NdafXEd/Q9KN28/V1CC8alhi/lBOjuZA+wNcPX0FA7afZQaSIcJHOGsL/T5M8JV+aN8/UrcG8juDv2RxrWipb8sP1Q9FzdZHmsMGyjnhJrk3+TR2xeb50xifPmhJO8IA8I/fIZcHYOnlF7jD/aiK9uPRJ/aaHSFdL1/A59R2XRElJSWP4CVy0bxPh4YdGAAAAAElFTkSuQmCC>

[image16]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAoAAAAYCAYAAADDLGwtAAAAu0lEQVR4XmNgGAXUBMxAbAzEdkDMChVjBGJ9IJaHKQJJ9AJxKRCfgLJBAKToExDvAWJukIArENcAsSBU4UKoQk4gXgDEB4CYBySQCsSaQGwJxN+AOAKqEARsgHgyEh8MGoD4CRArIokFAXE6Eh9s7WkgXgPELFAxkGcaGCC2wYEkED8E4nIkMZDJPQwIjWAgDsR3gbgKygeFRCcQG8JVQAHImiwgfgnEi4B4BxAHoKhAAxwMEGeA6FHAAABxGxeL5AUuPwAAAABJRU5ErkJggg==>

[image17]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAYAAAAZCAYAAAASTF8GAAAAd0lEQVR4XmNgoBngAWIxIGaGCWgB8R0g/g/EV4FYBCYBAoJAfBqIlwIxI7KEJhC/BeJ0ZEEQiAbi30Bsgy4xiQGP+WuAmAVZgnTzcUqQb7E6EFfDJDyB+CcDxPwsII6ASYBC8xwQ7wfixUDMD5MAAVaoAhA9iAEADUgZKJL1EpIAAAAASUVORK5CYII=>

[image18]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAE4AAAAbCAYAAADS6blZAAAEfklEQVR4Xu2YXahUVRTH/6GJWn6blQZeMYXAMoiiKHpJJSHzO7+gl8iEtMjSPiAwSjI1MkH6MNSroBRiRdJDBWlCED1ED71EkUX01mMPUlbr5zr7zj77nDN35s5cuaPzhz8zZ+09M3uvvdZ/rT3SwHCF8T7jrHRAblskn9NFgnnGTdn74cZdxn+MyzLbquh9FxnGGQ8ar41sk41fGm/KnpnznnFK34wudL9xW2K7x3jaOCGyvWx8MHq+7LFdxTR8zLg3sS007klsQw2jjOuN72avPA8KEPy3jLdHNjTuuHFdZAO3GY8aRyb2oQLW/YJxttxhh+TrvTKa0zBwzCTj+HQgw9XGj+ROCUj1LYA5zOUzQxHXG381bs6ekZs/jHP7ZjSI3cb/MoYvS0H0cCqx48r0DVxsx7G2540/GT8z7jf+KM8GUvEb43nj18YPjGOMd8gPHtAp4DgCYIZ8nyuMdxvfl8sORa8UaNffcmdUgYqKfgUEfcMWRx3fweIvdj+HjCyWH9gr2WvAWHm1/ySxk7bYkSFSlVZrgfwQ7srm9Mr701LggLPGaYk9Bo6CAQ/JT2er8vqA5lVFbn8YJm9lSKdAnrH3ByJ9g4qO47McIm3U25EdEDDsHa3DiTTx7JEg4TPMJYqJygIYPCUPbT5chTnGd5SfQ9jHm2Jsn3xuM2DhW4w/yE+YFNtpXC2P6EaqXpXj1mRjrG2lakWL731afugczkS5s3BaKHjsAzkKaZ0Dafan8Vn5qRCW96pYZfjSl5TXuRT8EIuudwAprjOeNK5VY5FVhTLHseY3VIwYNI7KOlUe1c8Yb5Dr9eeq7RGf0IZxzcQnOdCsIp5fyHOdU0ZI0YP0pBHJF7PXFNhw7DXpQB2ECF2eDgwAqeN6jM/JhT/WKLqH71UriPCMPHuorJ+qtj8iEsc/qWIgXchxHMc9Mwg6nqZkcxopCPWyHq3KXg9EKI5rJkKrEBxHS/Wx8YS8oh5RHXFPQMSPTmxXqaTQBX37UHmPEp4/K38vHQywoSeULwYpiZDCwkuQRhyv6NZXyncDbUGsbwGNFot2AMdRtSgGVdyh6sY8RpnjwCPZWFuBvp1Tvn/jR/5S/iqF1iGgr8vz/2YVS/tAgGg/lRoHiCrHjVBRn9gPDn3V+Gj23BRIybPK92/bjL/Lu+j58kvwUuOt8urHAnvkxaRMA5sBbcCx7LVVVDkuBWn/mrwPDe9ZQ8PZVZaSsQ2RfNN4o7wxvFNeqinZ/CDaRCVqFVRUundEuBXUuznEYK+HjQfkxQCZ+lbFq2MlpstL9cbIhkO4CXDlwHlLorHQ0wAawsejsVbAbz4gr4B08lUbrkLZXfUXeQDQVNer9KQo7QdtR8NgwXi5rOlEjNMN9Kr2fxwpfEs01g5wn0R3kIDfIhLlVNZ2g4b7O7mjW432uiCl+HcBzXtYjbUInQAyjsMqa+rbBsI+jcROAxWWwjAze6Yl+lfFa1kXCULvGv7BQX54xt5FHRBx9KPcP+lf+RebdL1UpGfQQQGkD+106ekM/A+12bwjtaiv7AAAAABJRU5ErkJggg==>

[image19]:
<data:image/png;base64,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>

[image20]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACwAAAAYCAYAAACBbx+6AAAC2UlEQVR4Xu2WS6hNURjHP6EojzwiUcJIBhQpz4EQAwZGioFSGJCiKAPduhkoSkJ55VGIFCVSBm6ZiCmRR1HKyERRlMf/d9Ze7trfWfucfRxicH/175yzvrPX+tb6HmubDfB/M0U6LX2VDjsbDJVG+8E2jJCG+cF2jJIWSkO8IWGq9EL6IX2T1pbNDUdPSvPdeDuY93rxWQt2d0N6L013tpT90ilphjTY2djocWmbG6/LEumm1YzOROm1hZPzpxYZI12TJntDwQqpz2oumGGQdEza6w05ZkofLDjMQzzsWSydsLxtuHRX2u4NHbJAeiZN8wbPcume9NzCA5y4h51v8IMFc6WXFjbeDUTxsVWv84td0m4Lp/tdWlU2N3L8nFU7tFV6II30BgtdY6mFTcW855MCHx//lHBGumT5SDbAQJsi5DiKwziXPkAhXrHQfnJcKOQhnxnfaSECm4vx1RbSD+c8RLJq8w3Y5S0LxcT3p9I7K+cRhdib/E5hE33SATcOnDzhZS7mjAU11oJTuU2ukd5Kk7whwsletP7+22Nh92keHbKQ5zmiw766mY+xCdJ66bOFooowP6noweGW7dUXE5N+sVD1VH+7dlblcATHSaeHVm55eyx/CDj8UZrtDcAEpENaTLFF4TTOUyyXrfrqbOcwJ8WJpSnDGkcsX3QtUwKHzlvzdUwISQtOZpNVOwM8y7WaKyBgw58sOBJhXU44B9HmEiu11n0WdsA1uy41FIyTHlk4ZR7OhS7lqHTb8lEgtIQ4rkNUuYB4icqBb01zcXqoz6pbFQvQ4t5Ydf5G6CJPLB9i+vBBC13irIULal7pH/3EaDVFlNvslTTHGxJYaIe0yBsy0La4Jek4VXAwhJl5q2AefEu7yV+BS4Y+XfUeUpeN0lVrvak/Bjl5X5rlDTWhbu5Y5+/SXcFi9OxOXzGJSo+F95luIvRbLLPw7tAJK6Ut9g+cHQB+Akdje5uvzqngAAAAAElFTkSuQmCC>

[image21]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAsAAAAYCAYAAAAs7gcTAAAA00lEQVR4Xu3RsQtBURTH8SOUQUkGyWSSySCDslr8BYpdZoOSwWJU/gAyK/kLJDH6GwxKyWAxmH3Pu/dyldnkV5+659zbfee9J/LPLxNGCVW79pNA1BW6mKCPHcZug+RxRcM1ahiIuWGPhbxvb+KOoq2lY4sKHuLdQqY4IOn1ggxxRs7WekAPzhGyvSBxbLFExPYKuKFt61cyOKHn9XReHauMOlpuI42jmC+i0Zddi7kgK2ZEfVIQnakrZuYZVmJe/IINRvbMR3R2fYr7CTGk5MvBf541XB6SVFFgSAAAAABJRU5ErkJggg==>

[image22]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABgAAAAYCAYAAADgdz34AAABU0lEQVR4Xu3SvytGURzH8a9QfibyI8VAUSZKwmJBWSg/kmKwWrAgrCwWgywMslAkSjLYWIz+BtlsBoNB3p++B9f1KM+P8X7q1XPuOc+955zvOWZJkiRJlToMow+FsbGs0op7HGMK6zhFcfRPmaYdz1hFXuirwZX5jrKKVniJJzSFPn10F0v2PWHG0epf8IZH8zJtodNy8HFlAO9YiQ/kKl14Nb858ZQgH1XYwJr5zRrEJspD3z4qwjv95jfwKxpQWfRCNJpYN6oas+alvEU9unGBGfTgGr0owrl5VX6kAw/m13LPfMJtlKIALRjBSXjWyufNr7YmuEElGnAXfn9FpagN1I5Gh32A6fDchonQ1tl97l4r1w60k7RSZn6VVTZlEo2hfYix0NZ5aLJRNIe+f0U7WMYOFjEUGRvHEeZwZl7mBfNSph3tJNX2o/06n3iJk/ydD8IEL4psJhhNAAAAAElFTkSuQmCC>

[image23]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAgAAAAZCAYAAAAMhW+1AAAAl0lEQVR4XmNgGAW4gDgQ+wKxHRCzIktIAPEaIF4PxBFAXAfEu2CS8kB8BYhnMUB08QPxCSB+C5JkAeI5QPwEiBWhGkBiSUAcQJQCTQaIUSD7QRIYAOTi/0BchC4BA54MEAUgheiAC0TIAvFtIE5AkWJgcALidTAOyBSQI1czQLx6AIjbgZgPpgAEQP4HhaIYEDMjSwx9AADJfBce37n/6QAAAABJRU5ErkJggg==>

[image24]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAcAAAAYCAYAAAA20uedAAAAjUlEQVR4XmNgGOQgCYh3A7EwugQHEG+FYhAbBcgA8RMgbkUW5AFiSSAOBeLfQBwBxOJAzAqSjAfiWUB8H4h/AvFSIJ4ExMogSRAg3T4YcAHiX1AaA1QB8XMgVkKXgNm3B4i5GSCu7GKAWMUgAsRXGRD2BQFxARAzgjggohGI7wDxSigb7EdkIADFQxQAAFlmF1Xx4IiWAAAAAElFTkSuQmCC>

[image25]:
<data:image/png;base64,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>

[image26]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACUAAAAYCAYAAAB9ejRwAAACN0lEQVR4Xu2WPWgVURCFjxiLoEEMQRGUxB8CElCIKCLaGSSFjaAI2lkIIQSJhShqIAqKIqggNmqwUoIK9qIRCwsLe8HCysoyRZok52R2HrPz9iUWWYvwPviQnbvZe/be2fsE6qWbnqXr8wA5Qo/lYt100od0R3F9gP6kX2kXXUev0MPF+H/hHB1JtUv0SbjeRZ/BXqB2NMkU3R1qHfQtPZ9qL+jBUFuih36jC8E/sBu19D/SmLagf+kvgQtp7APdCAszDdsmR/N8pvtCTYzDVrCSq7AHj+YBch02djoPkP30E+0LtUOwbVHfOGrqL3RLqIlTKG9pCS2rJla4jIeKSy806Q06lOqa6FWq5X5yqu5toEFNrACRPvoL1YEH6H26IdVP0OfhuqqfnGVD6UHzKN+glbhDH6E5lCa6CwuWUT++g/WX8H46Si/6TQUKmheigR40i3Io1e7Bekmh4thxeg3lvnEUQo2uf8Vm+p5Owg7NyG06nGoNPNRH2BtqSx7TvWHMQ22CNfLO4jqjoA9QPrH1PF85R2Gn6LZUb7Cd/qYzsEnVvJeLsRxYS97yMy7Qtmp7tc2tOEnHcjHiob7DVucl3VqM6Wz5Cwu8hz6FBV8JNbGsopfexAqnuc4PBVKwCXomjHlg/W7dQvMR0Apto//OZbxFlkVvPgP7At+g/AYeSs3+Gv/wsNVCydUzc7AvK+KB1VdNv1N1o69Lh15uTg+lz7dqK2pFq+BnS2YQ9p+2Nm3arFkWAYeUZiH1MannAAAAAElFTkSuQmCC>

[image27]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAmwAAAAkCAYAAAA0AWYNAAAFYUlEQVR4Xu3dW6jtUxTH8SEUuV9yCXFwlCi3XENb7g+UWxTxQJSUWxJK57jkQRR1ECKK5BaJFPGXB6K8eSF1SOQBUR54wPiZc5w19txrsfde/22ttft+arTnnP+1117zvx/2aMz5n9sMAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABbrVI+1qa/2Zqk/STt7bJ76l3jskfqLcVbTv6LpAwAATLUdPHav7V09Dq7tJz12q+1JUXJ2bW1f4/FwbV9cvy7GHamt79f7yOlpHAAAYKrl6tOJHjvV9t0e56Zrk/C0x/61/bLHpbWtsW1r+788m9ofWJmjPGL9VBFV/Yt7NmlbtgMAAGD2KWF5NPWVFIWjPJ5P/XF95PFXDb333qn/RX3NZbX/usc2Vj5bJFVKtnJidFpqn+Lxu8e7aSzcntqa3xa1/ZINqonjeND6SfyyM6zch2faC8n1Ho97HGSDn6/799imVwAAgFVBVarXUv/91FZSla/14VaP61JfyZQSk+y91M4JSyyHhnOavhLAe5sx0c8MsRwqmpvmOI5DPNa0gz35zEYnbId5zFlJYP/wuLmOb+3xRm0DAIBVRMuOQUlRVJ20dPhEutYHJVm54vWVzU/YlADlZb1XrFTatLfuBI8r67iqZEfHiyolZrF8mmkvnmhex3qcV/uvWknyxhGVwZXQ2eiE7QEb3IsPPX5N13QPYtkXAABMufUe6zw2eFxe+9rAr438WnKMPWCH2mCZcLv6VfR9utYnVbQiCVH7fBskbCd53Fbb4X4bJB9K3MLxNn8Zci+PjVaqS0o6f0jXckK6VWrHwwzj6FL7Ro+frVT5dI/1OS7weMHjFo8b6uv0ufX7mLMyv3fquGiZVvv0lIz9YqMTtrC9xzcehzfjzzV9AAAwhfTUp6pSSiCOqWN/WkkcRJWuWA5UAjFsafAeGyRyrbetJAqjYhT9HO0zUxXtodr/rV7TfrV9ajuo4nZfM6bPe1czpv1smp+WBPN7yrCnQfW6catrkvf8iap+ujei9//O40Ar97GzkiTr8+j3IroPL1r5PGp/Usfl35ZERYmg7vWZNv/YE+maPgAAmFJKYmJjvipQsb9LVaY3rSQJQWO5+pTbfdrT41OPp6wcGaJlyh89DrDRT30qQcvVNFUBc180n662tTT6/eDSP3LlcFh/udqESsli7K3TXPP1zsoctQysdtDnvcnK9+X9dp0tfP9hVE1r9wHqZwAAgBmgvWKxp0uJWxzRoWXOqPAs1y5WEpJRMYo2yX/tcVHt67Xq37npFcuz0QYJaWcLK18rpU2oFpOwfW4laQ1K0rQMupSETcuvsddP39MmbF3TBwAAU+rb1FYFKvaA6elI9bVx/f+mz6DjN4ISmM6GL8kuhSpqkZxGO85sW0lKvjItiWpfnrQJm16rZeojPH6qY0p8tQwaVcRcGdOy7qjE80srBx6Lvj8n4Hqf9olaAAAwpfKZZfHHPexYv+qJSW1YV8K0n8fJVipz62z+fxbok5KW7Mimvxztcq4SIdGSazyVmo/06MvH7cAS6Hcw7KBbJXoxh/g9DTPncaEtfA89kLGmGQMAADNsrcdxVpI7VWZUmXrLypOZ2hsV1aJZpX1ymo/mthJPTuqg2rwXcNI0T/2HCn0FAACrSCzbxdOUSmz0B18H2Orss1mn+WipUvM5u7nWBx2Rsm87OCE6wmTWk2wAADCEzgq72srZYKL9bVdZOZBV1ZpZp/lssDKfPo7yaGnZdX07OCH5/8ICAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAKbT36Lfwioc1DDbAAAAAElFTkSuQmCC>

[image28]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAGIAAAAYCAYAAAABHCipAAADsklEQVR4Xu2YXYhNURTHlxBifKeENIUSRYkiPAiRjweUfDwo+SolJCUPIymUB/L9kZBISkIoMfLgwbwopKSQ8iSlPEjh/79r77nnrnvOPmdmzr0zd7q/+nfP3fvce/baa+219j4iderU6T70hgbZxpzo65QXPaEhUA/bUevQAeegGbYjJ5Y55QUdsBva6a67Bb2gU9BW25EjeTuCcAVfhVbajlplAdQslUtLpBKOIJOhl9AY21Fr9IMeQdttR85UyhFczTegJtNec0yDPkATbUfOVMoRZB30SrR4lzAQWizF5cLPpdDI1ju6DlugF1CD7XAwD8914jV3K3TebHedlSRHsNBOkNL/GwuNar0jnSnQZ2h6tHGA6O7jAPQVOgGdgfZC76DG4q1dgitOcXBC7kCbRe14Dl2A9kCPoWPFW1OJcwTTyhHon9N10ZXJZ4yO3JcGA5yOWBFtXCQaZfTST+iy6JJ5An2X9qeAcVAL9KUNWlP4ZTIMmmbokGknrB1nRZ9LuAp+iebi+dBfUduybh3jHDETOi/q8PGiW1GOu61baG8Hg72VbaJOoHf+iC45DpapiU7yA+cnl9JC970ziDXAQQdwYjxMtb9F7ekvukqiUZtmT5wjotDxR0WdHIXpis+eatqjhAKqsJTfQsNtB1gL3Rftj5uEahFyhIVGfpL43J3FnpAj6Njj0CzTvg+6J5p2kn5LvB0XTXuh8LEAMueFli5zc9LALYyMEaL5MKs4wBBZHeHvuy2a15MI2ZPkiKGiNSEp4v2z437rSbSDdYD1gLUiRGjgFkbNEmhVG5RWjzipnNyySBJNOzdFD3venuhYN0l5GgrZE+cIOiFahwiD7bQUgyiLI1iDuX3dZTui9SFEaODVgin0gZS/kKNR3MlshDa4az8ZLK7XoGHuuydkj3UET/HcwPwQzRx8DtUipa9asjiCQfNRyutLYWfxWsoHagkNvFosh95IeS1jENEGThJ3RwdFa8Al6K7Er7aQPVFHMF3vh1ZDfdy138LymucVTxZHcKwcW1n9YnSl5WcSGni1aITeS/zqpQ0MJl/n7HdLyB7riAb36Rks8XOWxRFNoq85QvUrSGjg1YKTwWg/6a47Qsgem5qykuYIBsZTaI7tyAJ3CFzi30Sj8bBoRHQWzLHPoEm2IyNZ7GmPI3h+4GtuHiT5hpUnelvL1osW/Gg6q2l4mr0llXsV3h5HpMHd1kPRzUO3Yh60wzbmBA9r9sDWEVhjeMiMnu7r1KkTy3+4FLFdVM2MuwAAAABJRU5ErkJggg==>

[image29]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAGIAAAAYCAYAAAABHCipAAAEEElEQVR4Xu2YS4gUVxSGj6io+H4gaNSgqCAqCqKgaBZBRTFxoYIEIwjiCwTxhSAuRiQEhSwiSXyjJpgEEcSF+ECSCQlk4WwEEwIhEGVwJ4LgQgT1fHOqpu+cut1dPVbP2EP/8DO37625Vef+51Ul0kQTTfQdDFSO9JMFYXDCotBfOVrZzy80OhDgjHKRXygInyYsCgiwX7k3GfcJDFB+q9zpFwpE0UIAIvh75Xq/0KhYoWyV+qUlUA8hwBzln8rJfqHRMER5W7nbLxSMeglBNP+kbHHzDYcFyn+Vs/xCwaiXEGCT8r5Y8e6CEcrVUgoX/n6inNB5xfuDHcrflcP9QgLy8EcJGdOtIN7SZJwX5YSg0M6Urvt9qPyg84rqmKd8pFwYTg4T6z6OKtuVJ5WnlIeUfyunli59L3A5YQwcyHXldjE7flOeUx5U3lF+Vbq0KmJCkFaOK98kvCIWmdxjUnBdNeDgCLEunFwl5mWo9Fx5USxk7imfSvdTwHRlm/JxDfys4z/LA6dpVX7h5gG147TYfQFR8EIsFy9XvhazLW/rGBNisfKsmOAzxFpRnrvWFjq1A2fvxC4xEVDnlVjI8bCkJkRiTAjScvEQh8VSWW8gakACBOBgUpBqX4rZM1QsSlKvzWNPTIgQCH9CTORwjvuwL3/5HUMlh+oI5b+U4/yCWMSkOfGI8g/l2C5X9AwqCeGBkf9LPHfnsaeSEAj7tXJJMEfaQlTqBwJcUv4oVqc8UjvOu/mOwkcBJOf50PX/NE35RMzjKgEjx4vlw7zkXpWQV4j0umtiBxQirz3lhBgjVhPmu/k07+9LfiM0+5JtPMraQR2gHuApMcwVy4uAa7lBGJIx4DVrlBtqYLV6xKFyuBlPEks7P4u97KX2hIZuU65MxnnsiQmBCGEdAjjbd2LOTK1IMwr7sW/MJmow7WsqWifC+lANGHdXqntvvUAKvSnZD3IYRSezVbklGacHyaH/INn0A8rZ44XgLZ4G5plY5uA+sE2yn1pwGJyF7tNHJMBp/pOs+B2dxQOJP2gIFCfv4Rm9hbXKh5KtZTgRNnBIdEfHxGreBeUNiXtmJXtCIUjX1JKNykHJOG1hGfs6gGPjMOWKNc/Ks2XqF97lPcKDnPilWMrhwQnJ3gDvNf9IPHqxAWdK65z/HaKaPV4IUk+4zyiJnxm1hq+siMOeMZFbxD5zxKKlIghtWrUpYkVps7i3wh4Eh4G3f5OMu4M89vjUlAdEGJ3TRLF9D0j2RQ/H+EW5zM1XBdHC22oairBdsjfoSXDvX5Wz/UIO5LWnViE4YFJjuC+dKJEU4nOxgu/TWcMC77sq9fsUXqsQeUC3dUtKHVufwcfKPX6yIPCyFr6wvSuIDF4yfeQ10UQTGbwFZES+wBMBnycAAAAASUVORK5CYII=>

[image30]:
<data:image/png;base64,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>

[image31]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABwAAAAYCAYAAADpnJ2CAAABg0lEQVR4Xu3UPyhGURjH8UcoiiR/y39lMJBCSUJKGAz+FKUMEgNZKIVByWhRSiIZpFAGf1LEYJHZoAwWq0XZFN+n57zd+96yvJcy3F996nSe+5577vOee0WiRInyH1OAXrQjLb70uynEMU4wjEW8osHVi1DlxqFThkdsIdXNpeAAl0jHElpcLVR04W2xp6kI1Obxjm7sIiu+nFiq8SbWTr25PyP4ELvZUKCWcPSAfGEyWBCvdirW1l9Jj9iiungwOveJNt+c3ngOa2ItrsEmMtGFC9S5a4sxJoHOleAZ0765JHTgSbzN6I9z0Se24BnqUY5rtGIQC1gWi3Zt3Y3joou/4Ag7uMcs8sR2/CB2WvU066vRhCtki21uBpUoddfradb5ffTLD0lGPnLELvbP65PFXheNnt5VN9balBvrE8c2ovO3YocydPbE23knat1YW69PpRtuxA2axc5JqAzgEBMYFa8j2vJzjGMDd1gR+/9DR7+zGcFJsdbHPhB6zZ9+j6PIN4jWOdAhqQRxAAAAAElFTkSuQmCC>

[image32]:
<data:image/png;base64,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>

[image33]:
<data:image/png;base64,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>

[image34]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABQAAAAXCAYAAAALHW+jAAABLElEQVR4Xu2TLUsEURSGj/gBwiKCggiCsOAvUINYDRaDf8BiMLgYLILYDBqMiiAiNotVLGLXooImf4DRZDH5vJxzYWeY3dndsekDD5w79847M/eeMfvnN1jHJ3zEM3zG26jv8Btf8B4n455SpnAz6n3L3jiMu+YP7ThQCxUkmgP7sN88VG9cOXAJl6NewbGoS2kV2MCNqLsiHziDa/iOO2lRN6TAAbzAa/PTPrGKgSJ9sg7iErfjekKHtIoH5g8byU47RYFiEIeiTuyZ96SCdFAfWM+ssNaB7VBLHeOp+VZlaNfYRSziA97gRG6u8NfT+BBHm9YVoR59xen8RKfoMxUyF+NZ/LIee1WM4xuex3geP83/qJ7QG26Zb8cCXuGReTdUomZ+cGV7/Bf4Ae01MhLMn3SnAAAAAElFTkSuQmCC>

[image35]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADcAAAAXCAYAAACvd9dwAAACsUlEQVR4Xu2WS8hNURTH/0KRdwYfRS6FyMyrlEgpEhPyDiUmRFKfR4qSAUkREymPgWeUgUIGXxl4lgmRxxAhjEjJ4/+39r5n7/3de9zz1Snp/OvXuXftdfbZ6+y11j5ApUqV/jd1J4tJLbE3Uy+yikxwv3X/MLIenecYSw6Q42Ql6RMPl6PeZD45TF6TL2Ri5NFcA8k98ivhIOkZ+C0kF8k4WPCbyAMyIvApRQpuLplJdqNYcH3JNfKYvCKnyFTSLfBpI9dhQXlp/CjZE9giaeLBiCdqJKVLq9qG4sGdJkPTgUCa6xkZndj1rBOJDQPIeWQp8J4sg+V7qn5kQ2rMURnBjSJvyUsyw9kUw02ywDtJ2qX9ZB2ynB5JrpC7ZLyzeSnXdya2PHUluAvkECw1VbNXETcTrbkd2WacdD5b3Vhdg8iW1Oj+zyIvyCNYR+qABRzm+t/UleBukCWwNYi95DniZqGs2ocswA9kNpI4VPy10JBIu6kgtVu6hh2rFRUNTotT6oeLnEy+ImsWGtsIS8NpsF1TgD9gx0gnTYLVnXZmM+kfD9elZjIvNeaoaHCNpHs1xy3YWaYdUsoOd+PaxeXO5ymsm9alItS266xYCivod2jcVDRhmTWn+v8OC8DLB9cBS1t1xCPBuNcc8gnBs/QmdMrrCAilutJ23yfTYd1L19vu2qrygtOLqyE+WvRilV5hcD4t1TiUkvJRvaXSmh/CDvY/0hbq06aR9HCl4B3yE5YKqru0+eRJwWlhWmCqtbBaOUd6OJsyZzuyZ+i6g3wmU5xNmfYEca+Q3xpyFtlcpUjZoPpVivhuJt7AWryXPtG+wdq6D0YN6xi5TFaTM+Qj7IvHSy9d98i+i6wgl2Bddkjg909KgY4hi2CfcM2+iFRKekHyk3+RjKpUqVKl1vUbdLeFjrY/LokAAAAASUVORK5CYII=>

[image36]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADoAAAAXCAYAAABaiVzAAAADy0lEQVR4Xu2XTahVVRzFV5ig9KF9YERaaaaYlkFkE52IZSGGmeFApYFUgkJUg0AqgoggDErUUoKogUUFjcIQKckwqUEaSeEHZohSkkJUk8BaP/5nv7vPPucdrxjvBblgce/d+9x99/p/X+kCLmAoMNIcUy52YIR5hXlRuVGCB64yLy03hgEI3GzOKjc6wP2fMp+o3rditfl3xaeLvaHGxeZGc1WxzuXvMtebm8yHzEtqT0QUvGM+WKzXcLP5i4Zf6N3mTtXDFvGvmS+a4xV3/dz8zrwhew7MML80JxTrA7jWPKrhFTra/MRcU6xPM381t6uXWssUEYiHc2CUd83ni/UB/BeE3mEeVAjLMdX82dynqCNgsULom+mhDBjha0VxaqBLKGHwsPmkOUXtyU6o3WMuVFyG8541H1HkTj94zNxlXlZuGGPV8ya/v8E8oxBcYqZCy53lBmgTygWfMT9VfJncICzeUr0QLDCPKareo+Yhc7f5uPmj4ux+8HbFLiByvnnKXKd2IyYtbUZoFUrlQ8DkbA1vfWW+qvjRa8zvFYUiYa0i1G5Ru3fagLd2qn5OiXnmT4qiSfu5ur49gHRWW3Q2hHLIfvNjc1R6qAJWP2FOUuTVH6ofynvy575s7WzovFwBhoOXzNMK8SU6jVYKJVR/U3sosZaEjFOUecI55S4eJRImVp/7wbkIBeTfn4rf5g450llthaohNJX0DxUlOwdC/zJnV5/JRayLBZ9TiCRvc+AFGvkWhSEur293Cr1N0UZ4TUj3JZqIqhxdZzWEUkX3qFkFEY148pL85FCE08ixLOeUoQ6oqBgGwRS4L9RrFSCd2+aFFEF5dKWUwRllO6Kt0F7oEg2UQsH9Ck/NydYoTHgsjWhUXxo5Fl9ScZF5k0IUKEOJ3D6uZg5zRltN4E4nzXuzteUK8a+rGXEY/bBa8pdL/67evMsIhXfIubnmXvMDhUVpHUurPcAroUtPS99PzEe0W7P3eACh5UUwLN8pqynRRVvbYa5UtDHu+161V4LIoZBeV26cDYghzBCfvJTA5ZhY8hbE8+QTP1aOaAAP5eNcAsXrB/VyPwdn3qiIFjjoLKsY/zBM6enzAuGY504Oig5eyIcL/nptNa/M1hIQ84Ji6kkRc67AIQw4ebr9K+DiR8wH1PN2CvkDqv9lul3R/xCO0LItAPLrM3N6udEnyN031D4xnTcoZK8oQvgb81tFX2UYTyA/XzavVzy/QoPMogrjva/2/OsC6bNNzb9uQwaq6EeqFyoqN94bDEQERa5f0ALT/9X/N/4BGQW/kQkx6E4AAAAASUVORK5CYII=>

[image37]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAHUAAAAXCAYAAAA1OADtAAAE1klEQVR4Xu2ZaahuUxzGH6HMc4bQPYZc85CLkuFemcWVoYhP5hBxu0jk+nCTDMkUMibFTfkgwwe5pjKGdMsH5JKhCFE+IMPzO/+97l57v/s95737DO85137q6bzv2mvvd631/Nfz/699pA4dCpxqXlBv7DB7MWJ+YV5ba+8wS7GuebX5ijpRZwU2Ma80HzJvNncz16r0kE42jzSf0HBFZVyHmHeb9yvGRcD1wwHmsfXGacba5kmK8cIzzPUrPaR9zDPNrRVz3Mg83Dwn71SgvgZHFG2rsJ/5mjnf3Ny82PzTXKSy4w7mZcX3YYrK7y827zPnFLzNfNHcNOvHhK8x3zf/1eSONy3ocvO62rUmEHD3mDeZu5gXmr+bKxTjTzhLMdacX5v7Z33AhuZj5nPmHuZe5gfmMXkn1P7bPKX4jrAsxk+Km9ZR2C7CgmGKurdiMhtnbUQ8orIoCSw6O/hExQJOxnjZbUeZbyoCacvq5b44XpGyts/azlWI9rBifQHjpV6B7yicEwfNQUDdZb6hMojRpidw7ygazy++s2AM/DfFLt5TsZPh60U7EXR9dJ9WMPEP1bugBBqTq+NATVxUdhoB84l5g3oXejzw20nABDbINwoBtynamNt440zzwU0TeNZSc+esbXTQWykiEbAbflGIiK/n4Dvt4/34VOEw8y/zPUXeByPmu4rcWcdERMUBzjM/VgQ8ttcG8xTPuChr2878qiCfwSCiLlHMn3VAC+5dL+/QBKLwKTV7+WaKXf2jwsPTzp5OEIAUc0Q+ef9OhauclnfK0EZU1oB8jCOwQ8cqwtoiBSepJImCqDjOy4od/KUi/6bNRr8XFPO5xXxQ4U6fmZeqt7AdjcKnFWLywONUPqwtzlY8b1ASKLuO3jk2NjCfUVlMfGruW+lRYnVFJfWQ/1g8aoupAEFCoYMbHpy1I+qr5hbFd9YCiybAECy5ZD0XL1CkxFQTNWJ38zvzSbW3nKkCC3K7YmyHmh8pJolwRH8dqysqaFsQDYrTze/No2vt7MTcShES10TYnVSK+o+q9yYrJxD7WnF6GIt1Se3asMF4lqvM9eQ9jhVU79hW/ezXRtSE/Ohyr8rcNxGwM7H1g+oX+gA7RocTFBsMF2E+zCuhJz8T+VcVzHNHqth4aFsQNfzIoKQKHCt/pUnllV/C5aoWHQkTETUBcbH3lxTrMbd6eWAg6FsqUwz2ydGGWoWi71vzeVUDM4mKNQNsd1xR06TrHdPDOMO2xRzFm5NBuVBlPmlCsp+mNyxE/tuKKj7HZIiaAxt8RCEwQiP4IGAtni3+JjBWij7yeBpnLmpyzFwbCrc/VE01SdRV9rujonp6XOVhlhzCkYFE3nRMGCYWKYTN3x6RA6kGl2RtCWmxJvtMzUJiyTfWLzRgW8XLgp9VLQx/MJcpdizzQcCRuGUUfKZoRfjkYLxCXKFqkC5QQ6GEX39u3qqIBBT/tWifaSCKHzBXmlcojlVY8qOqFnXkXoq9VCFDFpXih4WZTqRU1sSlWb95inxLADK3lYoTSR7AgCKO4olAph+fCfYe12DbzlfYIC+Hx8ptMwE4DP/XhXxeU0Bg8s8H5jWiBqEK5P3WpPlPGShc6kVcP9bfuHWYgcCxFivy2CDM/4nQoUOHDh3+v/gPCQoXuAMZ2+wAAAAASUVORK5CYII=>

[image38]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAE8AAAAYCAYAAAC7v6DJAAAECUlEQVR4Xu2YS6hVZRTH/6JComWiIOoNNSLRhBQfGNVEbqBCDqKBoCNFRGziI1+IHRAHDYKIHqKGNsiwLISIIkIvKSU1sUHowAZKKAYRCDoJrfVr7c3Ze/Xtc86+pxtX7/nBn3Pca9/9fWvt9fiOUo8ePbpjnukL013T+mCDCaaH4sU2TDSNjRfvJ6aZFsWLgaWmP0x/ZZ8Ly2YtMB2TB6MOPPdD1f+7YcFk00+mH02Tgi1njOm46TXTdNOoklXqM501PRWud8o60yHdhxn4tOmW6baqs+9x00fysowQyDdNjXC9DuNMn5teioah5FF5yRVVt+f0m+7Jy/GVYMtZa9oVL2bMN13OPrthjek7DXH58qaXy8vstOmw6V3TBtPLphnNWztik+lTeR/7Sp4FRViP5z8XrucQVIZI3ZcWIbuvqGKd0fKywMj3Ip1OHO7Zb3rH9EiwDRaCT9AJHAGMGTTF9HH2GSFgBG5vNGTQT1eaHitcY9/EIPo73vSNEs/iRvoChm9NbxRsc0w35WnbDnoCgYsLD5aH5YHhrVOylG4sTxwl8+KQANrEVdOL0WAsNn1i2m26JvcTjsrXIaiRD+STt7TWC6Z98gw7ZzqpZvbRT2jYNO5WUE4sPDsaumCu3EEGAd9/N11Que8QTPaYgkrixcdSY69MzyfkSYF/+TB6Vj6cUgFnrQGFwbRZHpxnTHdUzjIC0uqYkMNbZkOUQBwSuaaqXlYSlIPZd44jTNQ/1QwGZcnZjcCmICBkVZzS7HGrfC88s/hCWOeI0slC8Cpj0TD9qmb2cBM3s8FUWRQhOGfkPaqVOKx2QmoQ8FIpqbcze6sjClQFLwc/8bdRuEarYJ+pHkrwfpEnQQk2MGA6JY8+5KXCxGsHi72nakfqgmNfquwEm76k5gteLe/VVbQLHpldzGRgIHHYTpEsW8iba7Eh83DKeIlplfykXQWZ8Lq8f/4XkGUH4kV5lpB9fGIngFWQmdeVbv6Ar/iM7zk75e0rBS2EicvkLcFbJSXzUUwP4EYeztmsoerekjNTfjRod18VlPQW+Xqc6FMZQ5MnIDT1i2q9Fln7s6orZ5s8i/uyf/M7lgDllVeE5GDSvhUNgHG7/GHvmz6TD5Ib8l7GQ9v1PZhl+lp+3puuzv4mJz8mIL6nnAAyhnsGlCihAqxNv046LA8avxq+lx9DTqj6FwT9/we1zvR/NlOcikw0DpN1gsAxp1/+Y51spu8gvj/fvO1fUF6/yR3KsyEFDu4xPRkNCSh/nE5OSLlf+NfOR0qZfkuvHTEQlPOmFdFQA4JKf0WtAvxAQqlx2OZwPBjos/R/evqIg2x5NVPdzKF90Tf/1/+OGm4QhB2mZdHQho3qruR79BgC/gYjGLH04qpezQAAAABJRU5ErkJggg==>

[image39]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAsAAAAXCAYAAADduLXGAAAA4UlEQVR4XuXSoYpCQRTG8SMquKigGA0iirDNBzBqsrnR4AtYtBjFKJpsxu2iGOyC0bppk0Fs+wIK6v/cOwMy94p1wQ9+cOfMwJk5XJF/mQhyyLgbbsY444a+sxeaFi6ouRthmeGAvFMPJI0dNkg4e4F84g8Ds9bHVtDAhz1k08YVdcQxwgRrCXmwvW8JQ1TFPxSYThZ7/GAu/pU0eo0ekmbt5XFkZfxiJU8e6o7sW/xO2rGJjqlLClssEDM1PaxrncIURVOXAk7o2gL5whFLU9cxetEPbRe1BRPt+PKHet/cAcfeIy832IBiAAAAAElFTkSuQmCC>

[image40]:
<data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAA8AAAAZCAYAAADuWXTMAAAA/klEQVR4XmNgGAWeQPyfSFwE1YMBFgLxbyC2QRNnBGIjIH4IxEFocmAgCMSngfgBEEujSsHBHCB2QRcEAX0g/gTEa4CYBSoGog2AmBXKnwhVhwGiGSB+KkcSA7kAZBs3lA9ysgBCGgFAiv4AcQAQSwKxPBDPBOJWZEXYAMy/f4H4CRA/AuJXUD5WPyIDYyD+yoDqX14gXgXESlA+M1QMA1CkGRZYyAlAHIgnAzEHlB8BxFkIaQgAJYD5DNgTBwzwAPFiIFZEl4AF1l0GiG3YQAwDxBUgi1AANv/CACh+K4D4NRBbIkuAouAZAyLBI0cTCP9CktsBxJwQbaNgCAEA3l87nMQUjqsAAAAASUVORK5CYII=>

[image41]:
<data:image/png;base64,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>

[image42]:
<data:image/png;base64,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>