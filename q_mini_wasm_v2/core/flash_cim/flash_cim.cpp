#include "flash_cim.hpp"
#include <chrono>
#include <random>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iostream>

namespace q_mini_wasm_v2::core::flash_cim {

// ============================================================================
// Configuration Helpers
// ============================================================================

FlashCIMConfig create_default_d_drive_config() {
    FlashCIMConfig config;
    config.storage_path = "D:\\flash_cim";
    config.block_size = 4096;
    config.page_size = 512;
    config.cells_per_page = 256;  // 2 bits per cell (MLC) = 256 cells per 512-byte page
    config.enable_ecc = true;
    config.enable_wear_leveling = true;
    config.max_program_cycles = 10000;  // Typical MLC flash endurance
    return config;
}

// ============================================================================
// FlashCIMController Implementation
// ============================================================================

FlashCIMController::FlashCIMController(const FlashCIMConfig& config)
    : config_(config)
    , operational_(false)
    , current_backend_(HardwareBackend::SOFTWARE_SIMULATION)
    , total_energy_pj_(0.0)
    , total_operations_(0)
    , total_cycles_(0)
{
    // Initialize block status tracking
    const size_t num_blocks = 1024;  // Simulate 1024 blocks
    block_status_.resize(num_blocks);
    for (size_t i = 0; i < num_blocks; ++i) {
        block_status_[i] = {
            static_cast<uint32_t>(i), // block_id
            0,                        // erase_count
            true,                     // is_valid
            false,                    // is_bad
            0                         // last_program_time
        };
    }
    
    // Initialize flash cells
    flash_cells_.resize(num_blocks);
    for (auto& block : flash_cells_) {
        block.resize(config_.cells_per_page * 8, CellState::ERASED);  // 8 pages per block
    }
}

FlashCIMController::~FlashCIMController() {
    if (operational_) {
        shutdown();
    }
}

CIMResult FlashCIMController::initialize() {
    CIMResult result;
    result.success = true;
    result.cycles = 1;
    result.energy_pj = calculate_write_energy(config_.block_size);
    result.data_size = config_.block_size;
    
    // Create storage directory if it doesn't exist
    try {
        std::filesystem::create_directories(config_.storage_path);
        operational_ = true;
        std::cout << "[Flash-CIM] Initialized at " << config_.storage_path << std::endl;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = "Failed to create storage directory: " + std::string(e.what());
        std::cerr << "[Flash-CIM] Initialization failed: " << result.error_message << std::endl;
    }
    
    total_energy_pj_ += result.energy_pj;
    total_operations_++;
    total_cycles_ += result.cycles;
    
    return result;
}

CIMResult FlashCIMController::shutdown() {
    CIMResult result;
    result.success = true;
    result.cycles = 1;
    result.energy_pj = 0.0;
    result.data_size = 0;
    
    operational_ = false;
    std::cout << "[Flash-CIM] Shutdown complete. Total energy: " 
              << total_energy_pj_ << " pJ" << std::endl;
    
    return result;
}

bool FlashCIMController::is_operational() const {
    return operational_;
}

// ============================================================================
// Ternary Data Operations
// ============================================================================

CellState FlashCIMController::trit_to_cell_state(ternary::Trit trit) const {
    switch (static_cast<int>(trit)) {
        case -1: return CellState::LOW;
        case 0:  return CellState::MEDIUM;
        case 1:  return CellState::HIGH;
        default: return CellState::ERASED;
    }
}

ternary::Trit FlashCIMController::cell_state_to_trit(CellState state) const {
    switch (state) {
        case CellState::LOW:    return ternary::Trit::NEGATIVE;
        case CellState::MEDIUM: return ternary::Trit::ZERO;
        case CellState::HIGH:   return ternary::Trit::POSITIVE;
        default:                return ternary::Trit::ZERO;
    }
}

std::vector<CellState> FlashCIMController::encode_trits_to_cells(
    const std::vector<ternary::Trit>& trits) 
{
    std::vector<CellState> cells;
    cells.reserve(trits.size());
    
    for (const auto& trit : trits) {
        cells.push_back(trit_to_cell_state(trit));
    }
    
    return cells;
}

std::vector<ternary::Trit> FlashCIMController::decode_cells_to_trits(
    const std::vector<CellState>& cells) 
{
    std::vector<ternary::Trit> trits;
    trits.reserve(cells.size());
    
    for (const auto& cell : cells) {
        trits.push_back(cell_state_to_trit(cell));
    }
    
    return trits;
}

CIMResult FlashCIMController::write_trits(
    const std::vector<ternary::Trit>& trits, 
    uint32_t block_id) 
{
    CIMResult result;
    
    if (!operational_) {
        result.success = false;
        result.error_message = "Controller not operational";
        return result;
    }
    
    if (block_id >= block_status_.size()) {
        result.success = false;
        result.error_message = "Invalid block ID: " + std::to_string(block_id);
        return result;
    }
    
    // Encode trits to cell states
    auto cells = encode_trits_to_cells(trits);
    
    // Write to flash cells
    size_t cells_to_write = std::min(cells.size(), flash_cells_[block_id].size());
    for (size_t i = 0; i < cells_to_write; ++i) {
        flash_cells_[block_id][i] = cells[i];
    }
    
    // Update block status
    block_status_[block_id].last_program_time = 
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    block_status_[block_id].erase_count++;
    
    // Calculate energy
    result.success = true;
    result.cycles = 1;
    result.energy_pj = calculate_write_energy(cells_to_write);
    result.data_size = trits.size();
    
    total_energy_pj_ += result.energy_pj;
    total_operations_++;
    total_cycles_ += result.cycles;
    
    return result;
}

std::vector<ternary::Trit> FlashCIMController::read_trits(uint32_t block_id, size_t num_trits) {
    std::vector<ternary::Trit> trits;
    
    if (!operational_ || block_id >= block_status_.size()) {
        return trits;
    }
    
    // Read from flash cells
    size_t cells_to_read = std::min(num_trits, flash_cells_[block_id].size());
    std::vector<CellState> cells(flash_cells_[block_id].begin(), 
                                  flash_cells_[block_id].begin() + cells_to_read);
    
    // Decode cells to trits
    trits = decode_cells_to_trits(cells);
    
    // Update energy
    total_energy_pj_ += calculate_read_energy(cells_to_read);
    total_operations_++;
    
    return trits;
}

CIMResult FlashCIMController::erase_block(uint32_t block_id) {
    CIMResult result;
    
    if (!operational_) {
        result.success = false;
        result.error_message = "Controller not operational";
        return result;
    }
    
    if (block_id >= block_status_.size()) {
        result.success = false;
        result.error_message = "Invalid block ID";
        return result;
    }
    
    // Erase all cells in block
    std::fill(flash_cells_[block_id].begin(), flash_cells_[block_id].end(), CellState::ERASED);
    
    // Update block status
    block_status_[block_id].erase_count++;
    
    result.success = true;
    result.cycles = 10;  // Erase takes longer than read/write
    result.energy_pj = calculate_erase_energy(1);
    result.data_size = flash_cells_[block_id].size();
    
    total_energy_pj_ += result.energy_pj;
    total_operations_++;
    total_cycles_ += result.cycles;
    
    return result;
}

// ============================================================================
// Compute-in-Memory Operations
// ============================================================================

CIMResult FlashCIMController::cim_vector_matrix_multiply(
    uint32_t input_block_id,
    uint32_t weight_block_id,
    uint32_t output_block_id) 
{
    CIMResult result;
    
    if (!operational_) {
        result.success = false;
        result.error_message = "Controller not operational";
        return result;
    }
    
    // Read input and weights directly from flash cells
    auto input_trits = decode_cells_to_trits(flash_cells_[input_block_id]);
    auto weight_trits = decode_cells_to_trits(flash_cells_[weight_block_id]);
    
    // Perform computation in-memory (simulated)
    // In real Flash-CIM, this would happen in the storage medium
    std::vector<ternary::Trit> output_trits;
    size_t output_size = std::min(input_trits.size(), weight_trits.size());
    
    for (size_t i = 0; i < output_size; ++i) {
        // Ternary multiplication: result = input * weight
        int input_val = static_cast<int>(input_trits[i]);
        int weight_val = static_cast<int>(weight_trits[i]);
        int product = input_val * weight_val;
        
        // Clamp to ternary range
        ternary::Trit result_trit;
        if (product > 0) result_trit = ternary::Trit::POSITIVE;
        else if (product < 0) result_trit = ternary::Trit::NEGATIVE;
        else result_trit = ternary::Trit::ZERO;
        
        output_trits.push_back(result_trit);
    }
    
    // Write result back to flash
    auto output_cells = encode_trits_to_cells(output_trits);
    for (size_t i = 0; i < output_cells.size() && i < flash_cells_[output_block_id].size(); ++i) {
        flash_cells_[output_block_id][i] = output_cells[i];
    }
    
    result.success = true;
    result.cycles = output_size;  // One cycle per element
    result.energy_pj = calculate_cim_energy(output_size);
    result.data_size = output_size;
    
    total_energy_pj_ += result.energy_pj;
    total_operations_++;
    total_cycles_ += result.cycles;
    
    return result;
}

CIMResult FlashCIMController::cim_accumulate(
    const std::vector<uint32_t>& block_ids,
    uint32_t output_block_id) 
{
    CIMResult result;
    
    if (!operational_ || block_ids.empty()) {
        result.success = false;
        result.error_message = "Invalid input";
        return result;
    }
    
    // Read all blocks and accumulate in-memory
    std::vector<int> accumulator(flash_cells_[block_ids[0]].size(), 0);
    
    for (uint32_t block_id : block_ids) {
        auto trits = decode_cells_to_trits(flash_cells_[block_id]);
        for (size_t i = 0; i < trits.size() && i < accumulator.size(); ++i) {
            accumulator[i] += static_cast<int>(trits[i]);
        }
    }
    
    // Convert accumulated values back to ternary
    std::vector<ternary::Trit> result_trits;
    for (int sum : accumulator) {
        ternary::Trit t;
        if (sum > 1) t = ternary::Trit::POSITIVE;
        else if (sum < -1) t = ternary::Trit::NEGATIVE;
        else if (sum > 0) t = ternary::Trit::POSITIVE;
        else if (sum < 0) t = ternary::Trit::NEGATIVE;
        else t = ternary::Trit::ZERO;
        result_trits.push_back(t);
    }
    
    // Write result
    auto output_cells = encode_trits_to_cells(result_trits);
    for (size_t i = 0; i < output_cells.size() && i < flash_cells_[output_block_id].size(); ++i) {
        flash_cells_[output_block_id][i] = output_cells[i];
    }
    
    result.success = true;
    result.cycles = block_ids.size() * accumulator.size();
    result.energy_pj = calculate_cim_energy(result.cycles);
    result.data_size = result_trits.size();
    
    total_energy_pj_ += result.energy_pj;
    total_operations_++;
    total_cycles_ += result.cycles;
    
    return result;
}

CIMResult FlashCIMController::cim_ternary_add(
    uint32_t block_a_id,
    uint32_t block_b_id,
    uint32_t output_block_id) 
{
    CIMResult result;
    
    if (!operational_) {
        result.success = false;
        result.error_message = "Controller not operational";
        return result;
    }
    
    // Read operands
    auto trits_a = decode_cells_to_trits(flash_cells_[block_a_id]);
    auto trits_b = decode_cells_to_trits(flash_cells_[block_b_id]);
    
    // Perform ternary addition in-memory
    std::vector<ternary::Trit> result_trits;
    size_t add_size = std::min(trits_a.size(), trits_b.size());
    
    for (size_t i = 0; i < add_size; ++i) {
        int sum = static_cast<int>(trits_a[i]) + static_cast<int>(trits_b[i]);
        ternary::Trit t;
        if (sum > 1) t = ternary::Trit::POSITIVE;
        else if (sum < -1) t = ternary::Trit::NEGATIVE;
        else t = static_cast<ternary::Trit>(sum);
        result_trits.push_back(t);
    }
    
    // Write result
    auto output_cells = encode_trits_to_cells(result_trits);
    for (size_t i = 0; i < output_cells.size() && i < flash_cells_[output_block_id].size(); ++i) {
        flash_cells_[output_block_id][i] = output_cells[i];
    }
    
    result.success = true;
    result.cycles = add_size;
    result.energy_pj = calculate_cim_energy(add_size);
    result.data_size = result_trits.size();
    
    total_energy_pj_ += result.energy_pj;
    total_operations_++;
    total_cycles_ += result.cycles;
    
    return result;
}

// ============================================================================
// Energy and Performance Metrics (Fixed-Point)
// ============================================================================

int32_t FlashCIMController::get_total_energy_fixed() const {
    // Return energy in fixed-point: 1000 = 1.0 pJ
    return static_cast<int32_t>(total_energy_pj_ * 1000);
}

std::vector<std::pair<std::string, int32_t>> FlashCIMController::get_metrics_fixed() const {
    // All metrics returned as fixed-point integers
    int32_t avg_energy = 0;
    if (total_operations_ > 0) {
        // (total_energy_pj_ / total_operations_) * 1000
        avg_energy = static_cast<int32_t>((total_energy_pj_ * 1000) / total_operations_);
    }
    
    return {
        {"total_energy_pj_fixed", static_cast<int32_t>(total_energy_pj_ * 1000)},
        {"total_operations", static_cast<int32_t>(total_operations_)},
        {"total_cycles", static_cast<int32_t>(total_cycles_)},
        {"avg_energy_per_op_fixed", avg_energy},
        {"blocks_total", static_cast<int32_t>(block_status_.size())},
        {"cells_per_block", static_cast<int32_t>(config_.cells_per_page * 8)},
    };
}

// ============================================================================
// Energy Calculation Helpers (Fixed-Point)
// ============================================================================

int32_t FlashCIMController::calculate_write_energy_fixed(size_t num_cells) const {
    // MLC flash write energy: ~10 pJ per cell
    // Fixed-point: 10 pJ * 1000 = 10000
    return static_cast<int32_t>(num_cells) * 10000;
}

int32_t FlashCIMController::calculate_read_energy_fixed(size_t num_cells) const {
    // MLC flash read energy: ~1 pJ per cell
    // Fixed-point: 1 pJ * 1000 = 1000
    return static_cast<int32_t>(num_cells) * 1000;
}

int32_t FlashCIMController::calculate_erase_energy_fixed(size_t num_blocks) const {
    // Block erase energy: ~1000 pJ per block
    // Fixed-point: 1000 pJ * 1000 = 1000000
    return static_cast<int32_t>(num_blocks) * 1000000;
}

int32_t FlashCIMController::calculate_cim_energy_fixed(size_t operations) const {
    // CIM operation energy: ~0.1 pJ per operation
    // Fixed-point: 0.1 pJ * 1000 = 100
    return static_cast<int32_t>(operations) * 100;
}

// ============================================================================
// ============================================================================
// Multi-Wordline Sensing Implementation
// ============================================================================

std::vector<std::vector<CellState>> FlashCIMController::multi_wordline_sense(const std::vector<size_t>& wordlines) {
    std::vector<std::vector<CellState>> results;
    results.reserve(wordlines.size());
    
    // Parallel sensing simulation - in hardware this happens simultaneously
    for (size_t wl : wordlines) {
        if (wl < flash_cells_.size()) {
            results.push_back(flash_cells_[wl]);
        } else {
            results.emplace_back();
        }
    }
    
    // Energy cost: parallel sensing reduces per-wordline cost by 70%
    // Fixed-point: 0.3 = 300/1000, use integer arithmetic
    int32_t read_energy = calculate_read_energy_fixed(config_.cells_per_page);
    int32_t parallel_cost = (static_cast<int32_t>(wordlines.size()) * read_energy * 300) / 1000;
    total_energy_pj_ += parallel_cost / 1000.0; // Convert back to double for member variable
    total_operations_++;
    
    return results;
}

std::vector<CellState> FlashCIMController::parallel_sense_8w(uint32_t block_id, size_t wordline_count) {
    wordline_count = std::min(wordline_count, static_cast<size_t>(8));
    
    std::vector<size_t> wordlines(wordline_count);
    std::iota(wordlines.begin(), wordlines.end(), block_id * 8);
    
    auto parallel_results = multi_wordline_sense(wordlines);
    
    // Merge parallel sense results
    std::vector<CellState> merged;
    for (const auto& wl_data : parallel_results) {
        merged.insert(merged.end(), wl_data.begin(), wl_data.end());
    }
    
    return merged;
}

// ============================================================================
// Threshold Voltage Logic Implementation
// ============================================================================

std::vector<FlashCIMController::ThresholdLevel> FlashCIMController::measure_threshold_distribution(uint32_t block_id) {
    std::vector<ThresholdLevel> levels;
    
    if (block_id >= block_status_.size()) {
        return levels;
    }
    
    const auto& cells = flash_cells_[block_id];
    levels.reserve(cells.size());
    
    // Simulate threshold voltage measurement
    for (CellState state : cells) {
        switch (state) {
            case CellState::ERASED: levels.push_back(ThresholdLevel::LEVEL_0); break;
            case CellState::LOW:    levels.push_back(ThresholdLevel::LEVEL_1); break;
            case CellState::MEDIUM: levels.push_back(ThresholdLevel::LEVEL_2); break;
            case CellState::HIGH:   levels.push_back(ThresholdLevel::LEVEL_3); break;
            default:                levels.push_back(ThresholdLevel::LEVEL_0); break;
        }
    }
    
    // Energy cost in fixed-point: 0.5 pJ per cell = 500 in fixed-point
    // (cells.size() * 500) / 1000 = cells.size() / 2
    total_energy_pj_ += static_cast<double>(cells.size()) * 0.5; // Threshold measurement cost
    total_operations_++;
    
    return levels;
}

std::vector<ternary::Trit> FlashCIMController::threshold_decode(const std::vector<ThresholdLevel>& threshold_levels) const {
    std::vector<ternary::Trit> trits;
    trits.reserve(threshold_levels.size());
    
    for (ThresholdLevel level : threshold_levels) {
        switch (level) {
            case ThresholdLevel::LEVEL_1: trits.push_back(ternary::Trit::NEGATIVE); break;
            case ThresholdLevel::LEVEL_2: trits.push_back(ternary::Trit::ZERO);     break;
            case ThresholdLevel::LEVEL_3: trits.push_back(ternary::Trit::POSITIVE); break;
            default:                       trits.push_back(ternary::Trit::ZERO);     break;
        }
    }
    
    return trits;
}

// ============================================================================
// Hardware Abstraction Layer Implementation
// ============================================================================

FlashCIMController::HardwareBackend FlashCIMController::get_backend() const {
    return current_backend_;
}

void FlashCIMController::set_backend(HardwareBackend backend) {
    current_backend_ = backend;
}

bool FlashCIMController::is_hardware_accelerated() const {
    return current_backend_ != HardwareBackend::SOFTWARE_SIMULATION;
}

ternary::Trit FlashCIMController::is_hardware_accelerated_trit() const {
    // Return ternary status: POSITIVE (1) = hardware, ZERO (0) = software, NEGATIVE (-1) = unknown
    if (!operational_) {
        return ternary::Trit::NEGATIVE;  // Unknown when not operational
    }
    return (current_backend_ != HardwareBackend::SOFTWARE_SIMULATION) 
        ? ternary::Trit::POSITIVE 
        : ternary::Trit::ZERO;
}

std::unique_ptr<FlashCIMController> create_flash_cim_controller(const FlashCIMConfig& config) {
    return std::make_unique<FlashCIMController>(config);
}

} // namespace q_mini_wasm_v2::core::flash_cim
