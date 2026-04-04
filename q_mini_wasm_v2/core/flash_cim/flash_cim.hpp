#pragma once

/**
 * @file flash_cim.hpp
 * @brief Flash Compute-in-Memory (Flash-CIM) interface for ternary data storage
 * 
 * This module provides a proof-of-concept implementation of Flash-CIM using
 * standard storage devices (like D:\ drive) to demonstrate compute-in-memory
 * concepts for the q_mini_wasm_v2 quantum-classical hybrid framework.
 * 
 * Key Concepts:
 * - Ternary data storage using multi-level cell (MLC) flash simulation
 * - In-storage compute operations for energy efficiency
 * - 1.58-bit ternary encoding optimized for flash cell characteristics
 */

#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <functional>
#include "../ternary/trit.hpp"

namespace q_mini_wasm_v2::core::flash_cim {

/**
 * @brief Flash cell states for MLC simulation
 * 
 * MLC flash cells can store 2 bits (4 states), we map these to ternary values
 * with one state reserved for error detection/correction.
 */
enum class CellState : uint8_t {
    ERASED = 0,   // All electrons removed
    LOW = 1,      // Few electrons (ternary -1)
    MEDIUM = 2,   // Some electrons (ternary 0)
    HIGH = 3,     // Many electrons (ternary +1)
    RESERVED = 4  // Error detection
};

/**
 * @brief Flash-CIM configuration
 */
struct FlashCIMConfig {
    std::string storage_path;       // Path to storage device (e.g., "D:\\flash_cim")
    size_t block_size;              // Block size in bytes (default: 4096)
    size_t page_size;               // Page size in bytes (default: 512)
    size_t cells_per_page;          // Number of flash cells per page
    bool enable_ecc;                // Enable error correction
    bool enable_wear_leveling;      // Enable wear leveling
    size_t max_program_cycles;      // Maximum program/erase cycles
};

/**
 * @brief Flash block status
 */
struct BlockStatus {
    uint32_t block_id;
    uint32_t erase_count;
    bool is_valid;
    bool is_bad;
    uint64_t last_program_time;
};

/**
 * @brief CIM operation result
 */
struct CIMResult {
    bool success;
    uint64_t cycles;
    double energy_pj;       // Energy consumption in picojoules
    size_t data_size;
    std::string error_message;
};

/**
 * @brief Flash-CIM Controller
 * 
 * Manages flash storage operations with compute-in-memory capabilities.
 * Provides ternary data encoding, in-storage computation, and energy tracking.
 */
class FlashCIMController {
public:
    /**
     * @brief Construct Flash-CIM controller
     * @param config Configuration parameters
     */
    explicit FlashCIMController(const FlashCIMConfig& config);
    
    /**
     * @brief Destructor
     */
    ~FlashCIMController();

    // ========================================================================
    // Initialization and Management
    // ========================================================================
    
    /**
     * @brief Initialize the flash storage
     * @return Result of initialization
     */
    CIMResult initialize();
    
    /**
     * @brief Shutdown the controller gracefully
     * @return Result of shutdown
     */
    CIMResult shutdown();
    
    /**
     * @brief Get controller status
     * @return True if controller is operational
     */
    bool is_operational() const;

    // ========================================================================
    // Ternary Data Operations
    // ========================================================================
    
    /**
     * @brief Write ternary data to flash
     * @param trits Vector of trits to write
     * @param block_id Target block ID
     * @return Result of write operation
     */
    CIMResult write_trits(const std::vector<ternary::Trit>& trits, uint32_t block_id);
    
    /**
     * @brief Read ternary data from flash
     * @param block_id Source block ID
     * @param num_trits Number of trits to read
     * @return Vector of trits read from flash
     */
    std::vector<ternary::Trit> read_trits(uint32_t block_id, size_t num_trits);
    
    /**
     * @brief Erase a flash block
     * @param block_id Block to erase
     * @return Result of erase operation
     */
    CIMResult erase_block(uint32_t block_id);

    // ========================================================================
    // Compute-in-Memory Operations
    // ========================================================================
    
    /**
     * @brief Perform in-storage vector-matrix multiplication
     * 
     * This demonstrates the key Flash-CIM capability: performing computation
     * directly in the storage medium without transferring data to main memory.
     * 
     * @param input_block_id Block containing input vector
     * @param weight_block_id Block containing weight matrix
     * @param output_block_id Block to store output
     * @return Result of CIM operation
     */
    CIMResult cim_vector_matrix_multiply(
        uint32_t input_block_id,
        uint32_t weight_block_id,
        uint32_t output_block_id
    );
    
    /**
     * @brief Perform in-storage accumulation
     * 
     * Accumulates values across multiple blocks without external transfer.
     * 
     * @param block_ids Vector of block IDs to accumulate
     * @param output_block_id Block to store accumulated result
     * @return Result of accumulation
     */
    CIMResult cim_accumulate(
        const std::vector<uint32_t>& block_ids,
        uint32_t output_block_id
    );
    
    /**
     * @brief Perform in-storage ternary addition
     * @param block_a_id First operand block
     * @param block_b_id Second operand block
     * @param output_block_id Result block
     * @return Result of addition
     */
    CIMResult cim_ternary_add(
        uint32_t block_a_id,
        uint32_t block_b_id,
        uint32_t output_block_id
    );

    // ========================================================================
    // Energy and Performance Metrics
    // ========================================================================
    
    /**
     * @brief Get energy consumption statistics
     * @return Total energy consumed in picojoules
     */
    double get_total_energy_pj() const;
    
    /**
     * @brief Get performance metrics
     * @return Map of metric names to values
     */
    std::vector<std::pair<std::string, double>> get_metrics() const;
    
    /**
     * @brief Reset energy and performance counters
     */
    void reset_metrics();

    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Get current configuration
     * @return Current FlashCIMConfig
     */
    const FlashCIMConfig& config() const { return config_; }

private:
    FlashCIMConfig config_;
    bool operational_;
    
    // Energy tracking
    double total_energy_pj_;
    uint64_t total_operations_;
    uint64_t total_cycles_;
    
    // Block management
    std::vector<BlockStatus> block_status_;
    std::vector<std::vector<CellState>> flash_cells_;
    
    // Internal helpers
    CellState trit_to_cell_state(ternary::Trit trit) const;
    ternary::Trit cell_state_to_trit(CellState state) const;
    std::vector<CellState> encode_trits_to_cells(const std::vector<ternary::Trit>& trits);
    std::vector<ternary::Trit> decode_cells_to_trits(const std::vector<CellState>& cells);
    
    // Energy calculation helpers
    double calculate_write_energy(size_t num_cells) const;
    double calculate_read_energy(size_t num_cells) const;
    double calculate_erase_energy(size_t num_blocks) const;
    double calculate_cim_energy(size_t operations) const;
};

/**
 * @brief Factory function for creating Flash-CIM controller
 * @param config Configuration parameters
 * @return Unique pointer to controller
 */
std::unique_ptr<FlashCIMController> create_flash_cim_controller(const FlashCIMConfig& config);

/**
 * @brief Create default configuration for D:\ drive
 * @return Default FlashCIMConfig pointing to D:\ drive
 */
FlashCIMConfig create_default_d_drive_config();

} // namespace q_mini_wasm_v2::core::flash_cim