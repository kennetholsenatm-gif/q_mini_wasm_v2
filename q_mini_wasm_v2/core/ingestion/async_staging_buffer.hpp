#pragma once
#include <vector>
#include <cstdint>
#include <atomic>
#include <memory>
#include <chrono>
#include <string>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>

namespace q {
namespace ingestion {

// Asynchronous Data Ingestion - 4-Stage Pipeline
// Based on Q-MINI IncusOS research: Staging → Parallel Transformation → Validation → WASM Injection
// Target: 1.2M conversions/sec per core, <50μs latency

// Buffer entry states
enum class StagingState {
    EMPTY = 0,           // Slot available for writing
    STAGING = 1,         // Raw binary data being written
    READY_FOR_XFORM = 2, // Ready for transformation
    TRANSFORMING = 3,    // Worker thread processing
    VALIDATING = 4,      // Parity check and SBFR
    READY_FOR_WASM = 5,  // Ternary data ready
    COMPLETE = 6,        // Successfully injected
    FAILED = 7,          // Error / DLQ
    DEAD_LETTER = 8      // Unrecoverable, logged for retry
};

// Single staging buffer entry (lock-free circular buffer element)
struct alignas(64) StagingEntry {
    // Metadata header
    std::atomic<uint64_t> correlation_id{0};      // Unique ID for tracking
    std::atomic<StagingState> state{StagingState::EMPTY};
    std::atomic<uint64_t> timestamp_ns{0};          // Ingress timestamp
    std::atomic<uint32_t> data_length{0};           // Raw data bytes
    std::atomic<uint32_t> trit_count{0};            // Output trit dimension
    std::atomic<uint8_t> parity_bit{0};             // Trit 19 parity
    std::atomic<uint8_t> retry_count{0};             // SBFR retry counter
    
    // Data payload (variable size, power-of-2 for cache alignment)
    static constexpr size_t MAX_RAW_SIZE = 4096;      // 4KB max text per sample
    uint8_t raw_data[MAX_RAW_SIZE];
    
    // Constructor initializes state
    StagingEntry() = default;
};

// Lock-free circular staging buffer
// High-frequency ingestion decoupled from expensive conversion
class StagingBuffer {
public:
    explicit StagingBuffer(size_t capacity = 1024);
    ~StagingBuffer();
    
    // Stage 1: Ingest raw binary data (non-blocking)
    // Returns correlation_id on success, 0 on failure (buffer full)
    uint64_t stage_data(const uint8_t* data, uint32_t length, uint32_t trit_dim);
    
    // Stage 2: Claim entry for transformation (worker thread)
    // Returns index of entry in READY_FOR_XFORM state, or -1 if none available
    int64_t claim_for_transformation();
    
    // Stage 3: Validation - check parity and attempt SBFR recovery
    bool validate_entry(uint64_t index, uint8_t* trit_data, size_t trit_count);
    
    // Stage 4: Mark ready for WASM injection
    bool mark_ready_for_wasm(uint64_t index);
    
    // Mark complete (after WASM injection)
    bool mark_complete(uint64_t index);
    
    // Mark failed (move to DLQ)
    bool mark_failed(uint64_t index);
    
    // Get entry state
    StagingState get_state(uint64_t index) const;
    
    // Statistics
    struct Stats {
        uint64_t total_staged = 0;
        uint64_t total_transformed = 0;
        uint64_t total_validated = 0;
        uint64_t total_injected = 0;
        uint64_t total_failed = 0;
        uint64_t total_dlq = 0;
        double avg_latency_us = 0.0;
    };
    Stats get_stats() const;
    
    // Clear all entries (reset)
    void reset();
    
private:
    std::vector<std::unique_ptr<StagingEntry>> entries_;
    size_t capacity_;
    
    // Lock-free indices
    alignas(64) std::atomic<uint64_t> write_index_{0};
    alignas(64) std::atomic<uint64_t> read_index_{0};
    alignas(64) std::atomic<uint64_t> correlation_counter_{1};
    
    // Statistics counters
    alignas(64) std::atomic<uint64_t> staged_count_{0};
    alignas(64) std::atomic<uint64_t> transformed_count_{0};
    alignas(64) std::atomic<uint64_t> validated_count_{0};
    alignas(64) std::atomic<uint64_t> injected_count_{0};
    alignas(64) std::atomic<uint64_t> failed_count_{0};
    alignas(64) std::atomic<uint64_t> dlq_count_{0};
    
    // Helper: current time in nanoseconds
    static uint64_t now_ns();
};

// Dead Letter Queue for failed entries
class DeadLetterQueue {
public:
    struct FailedEntry {
        uint64_t correlation_id;
        uint64_t timestamp_ns;
        uint32_t data_length;
        std::vector<uint8_t> raw_data;
        std::string error_reason;
        uint8_t retry_count;
    };
    
    void enqueue(const FailedEntry& entry);
    bool dequeue(FailedEntry& entry);
    size_t size() const;
    void clear();
    
private:
    std::vector<FailedEntry> queue_;
    mutable std::mutex mutex_;
};

// Single-Bit Flip Recovery (SBFR) algorithm
// Attempts to recover from single-bit corruption by flipping each bit and checking parity
class SBFRecovery {
public:
    // Attempt recovery on corrupted trit data
    // Returns true if recovered, false if unrecoverable
    static bool attempt_recovery(uint8_t* packed_data, size_t packed_size, 
                                  uint32_t trit_count, uint8_t expected_parity);
    
    // Calculate ternary parity (sum of all trits mod 3)
    static uint8_t calculate_parity(const int8_t* trits, size_t count);
};

} // namespace ingestion
} // namespace q
