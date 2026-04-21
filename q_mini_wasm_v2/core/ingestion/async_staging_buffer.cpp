#include "async_staging_buffer.hpp"
#include <cstring>
#include <mutex>
#include <chrono>
#include <iostream>

namespace q {
namespace ingestion {

// ============================================================================
// StagingBuffer Implementation
// ============================================================================

StagingBuffer::StagingBuffer(size_t capacity) : capacity_(capacity) {
    entries_.reserve(capacity);
    for (size_t i = 0; i < capacity; ++i) {
        entries_.push_back(std::make_unique<StagingEntry>());
    }
}

StagingBuffer::~StagingBuffer() = default;

uint64_t StagingBuffer::now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

uint64_t StagingBuffer::stage_data(const uint8_t* data, uint32_t length, uint32_t trit_dim) {
    if (length > StagingEntry::MAX_RAW_SIZE) {
        return 0; // Data too large
    }
    
    // Get next write slot (lock-free)
    uint64_t write_idx = write_index_.fetch_add(1);
    size_t slot = write_idx % capacity_;
    
    StagingEntry& entry = *entries_[slot];
    
    // Check if slot is available (should be EMPTY or COMPLETE)
    StagingState expected = StagingState::EMPTY;
    if (!entry.state.compare_exchange_strong(expected, StagingState::STAGING,
                                             std::memory_order_acquire)) {
        // Slot not available - buffer full
        write_index_.fetch_sub(1); // Rollback
        return 0;
    }
    
    // Write data
    uint64_t corr_id = correlation_counter_.fetch_add(1);
    entry.correlation_id.store(corr_id, std::memory_order_relaxed);
    entry.timestamp_ns.store(now_ns(), std::memory_order_relaxed);
    entry.data_length.store(length, std::memory_order_relaxed);
    entry.trit_count.store(trit_dim, std::memory_order_relaxed);
    entry.retry_count.store(0, std::memory_order_relaxed);
    
    std::memcpy(entry.raw_data, data, length);
    
    // Mark ready for transformation
    entry.state.store(StagingState::READY_FOR_XFORM, std::memory_order_release);
    
    staged_count_.fetch_add(1, std::memory_order_relaxed);
    
    return corr_id;
}

int64_t StagingBuffer::claim_for_transformation() {
    // Scan for READY_FOR_XFORM entries
    uint64_t current_read = read_index_.load(std::memory_order_relaxed);
    uint64_t current_write = write_index_.load(std::memory_order_acquire);
    
    for (uint64_t i = current_read; i < current_write; ++i) {
        size_t slot = i % capacity_;
        StagingEntry& entry = *entries_[slot];
        
        StagingState expected = StagingState::READY_FOR_XFORM;
        if (entry.state.compare_exchange_strong(expected, StagingState::TRANSFORMING,
                                               std::memory_order_acquire)) {
            return static_cast<int64_t>(slot);
        }
    }
    
    return -1; // No work available
}

bool StagingBuffer::validate_entry(uint64_t index, uint8_t* trit_data, size_t trit_count) {
    if (index >= capacity_) return false;
    
    StagingEntry& entry = *entries_[index];
    
    // Verify in correct state
    StagingState current = entry.state.load(std::memory_order_acquire);
    if (current != StagingState::TRANSFORMING) {
        return false;
    }
    
    entry.state.store(StagingState::VALIDATING, std::memory_order_release);
    
    // Calculate parity on converted trits
    uint8_t calculated_parity = 0;
    for (size_t i = 0; i < trit_count; ++i) {
        // Convert int8_t {-1,0,1} to uint8_t {0,1,2} for parity
        uint8_t u = static_cast<uint8_t>(static_cast<int8_t>(trit_data[i]) + 1);
        calculated_parity = (calculated_parity + u) % 3;
    }
    
    uint8_t expected_parity = entry.parity_bit.load(std::memory_order_relaxed);
    
    if (calculated_parity != expected_parity) {
        // Parity mismatch - attempt SBFR
        uint8_t retry = entry.retry_count.fetch_add(1);
        if (retry < 3 && SBFRecovery::attempt_recovery(trit_data, trit_count / 4 + 1, 
                                                        trit_count, expected_parity)) {
            // Recovered - re-validate
            calculated_parity = 0;
            for (size_t i = 0; i < trit_count; ++i) {
                uint8_t u = static_cast<uint8_t>(static_cast<int8_t>(trit_data[i]) + 1);
                calculated_parity = (calculated_parity + u) % 3;
            }
            
            if (calculated_parity == expected_parity) {
                validated_count_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
        
        // Recovery failed
        entry.state.store(StagingState::FAILED, std::memory_order_release);
        failed_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    validated_count_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool StagingBuffer::mark_ready_for_wasm(uint64_t index) {
    if (index >= capacity_) return false;
    
    StagingEntry& entry = *entries_[index];
    StagingState expected = StagingState::VALIDATING;
    
    if (!entry.state.compare_exchange_strong(expected, StagingState::READY_FOR_WASM)) {
        return false;
    }
    
    transformed_count_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool StagingBuffer::mark_complete(uint64_t index) {
    if (index >= capacity_) return false;
    
    StagingEntry& entry = *entries_[index];
    StagingState expected = StagingState::READY_FOR_WASM;
    
    if (!entry.state.compare_exchange_strong(expected, StagingState::COMPLETE)) {
        return false;
    }
    
    // Update latency stats
    uint64_t now = now_ns();
    uint64_t ingress = entry.timestamp_ns.load(std::memory_order_relaxed);
    uint64_t latency_ns = now - ingress;
    
    injected_count_.fetch_add(1, std::memory_order_relaxed);
    
    // Update read index to allow reuse
    uint64_t slot_corr_id = entry.correlation_id.load(std::memory_order_relaxed);
    uint64_t current_read = read_index_.load(std::memory_order_relaxed);
    
    // Advance read index if this was the next expected entry
    if (slot_corr_id == current_read + 1) {
        read_index_.compare_exchange_strong(current_read, current_read + 1);
    }
    
    return true;
}

bool StagingBuffer::mark_failed(uint64_t index) {
    if (index >= capacity_) return false;
    
    StagingEntry& entry = *entries_[index];
    entry.state.store(StagingState::FAILED, std::memory_order_release);
    failed_count_.fetch_add(1, std::memory_order_relaxed);
    
    return true;
}

StagingState StagingBuffer::get_state(uint64_t index) const {
    if (index >= capacity_) return StagingState::EMPTY;
    return entries_[index]->state.load(std::memory_order_acquire);
}

StagingBuffer::Stats StagingBuffer::get_stats() const {
    Stats s;
    s.total_staged = staged_count_.load(std::memory_order_relaxed);
    s.total_transformed = transformed_count_.load(std::memory_order_relaxed);
    s.total_validated = validated_count_.load(std::memory_order_relaxed);
    s.total_injected = injected_count_.load(std::memory_order_relaxed);
    s.total_failed = failed_count_.load(std::memory_order_relaxed);
    s.total_dlq = dlq_count_.load(std::memory_order_relaxed);
    
    // Calculate average latency if we have injected samples
    if (s.total_injected > 0) {
        // This would need more sophisticated tracking for real avg
        s.avg_latency_us = 0.0; // Placeholder
    }
    
    return s;
}

void StagingBuffer::reset() {
    for (auto& entry : entries_) {
        entry->state.store(StagingState::EMPTY, std::memory_order_release);
        entry->correlation_id.store(0);
        entry->timestamp_ns.store(0);
        entry->data_length.store(0);
        entry->retry_count.store(0);
    }
    
    write_index_.store(0);
    read_index_.store(0);
    staged_count_.store(0);
    transformed_count_.store(0);
    validated_count_.store(0);
    injected_count_.store(0);
    failed_count_.store(0);
    dlq_count_.store(0);
}

// ============================================================================
// DeadLetterQueue Implementation
// ============================================================================

void DeadLetterQueue::enqueue(const FailedEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(entry);
}

bool DeadLetterQueue::dequeue(FailedEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return false;
    }
    entry = queue_.front();
    queue_.erase(queue_.begin());
    return true;
}

size_t DeadLetterQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void DeadLetterQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}

// ============================================================================
// SBFRecovery Implementation
// ============================================================================

bool SBFRecovery::attempt_recovery(uint8_t* packed_data, size_t packed_size,
                                    uint32_t trit_count, uint8_t expected_parity) {
    // Try flipping each bit in the packed data
    for (size_t byte_idx = 0; byte_idx < packed_size; ++byte_idx) {
        for (int bit_idx = 0; bit_idx < 8; ++bit_idx) {
            // Flip bit
            packed_data[byte_idx] ^= (1 << bit_idx);
            
            // Unpack and check parity
            // This is simplified - real implementation would need proper unpacking
            uint8_t test_parity = 0;
            for (size_t i = 0; i < trit_count; ++i) {
                // Extract trit from packed data (simplified)
                size_t packed_idx = i / 4;
                int trit_pos = i % 4;
                if (packed_idx < packed_size) {
                    uint8_t t = (packed_data[packed_idx] >> (trit_pos * 2)) & 0x3;
                    test_parity = (test_parity + t) % 3;
                }
            }
            
            if (test_parity == expected_parity) {
                return true; // Recovered!
            }
            
            // Flip bit back (restore)
            packed_data[byte_idx] ^= (1 << bit_idx);
        }
    }
    
    return false; // Unrecoverable
}

uint8_t SBFRecovery::calculate_parity(const int8_t* trits, size_t count) {
    uint8_t parity = 0;
    for (size_t i = 0; i < count; ++i) {
        uint8_t u = static_cast<uint8_t>(trits[i] + 1); // {-1,0,1} -> {0,1,2}
        parity = (parity + u) % 3;
    }
    return parity;
}

} // namespace ingestion
} // namespace q
