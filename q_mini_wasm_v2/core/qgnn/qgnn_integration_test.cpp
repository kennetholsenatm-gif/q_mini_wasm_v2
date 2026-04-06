#include "graph_native.hpp"
#include "graph_moe_router.hpp"
#include "graph_migration_adapter.hpp"
#include "../moe/router.hpp"
#include "../ternary/trit.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <cassert>

namespace q_mini_wasm_v2::core::qgnn {

/**
 * @brief Comprehensive QGNN integration test
 * 
 * Validates graph-native data structures and migration system
 */
class QGNNIntegrationTest {
public:
    /**
     * @brief Run all QGNN integration tests
     */
    static bool run_all_tests() {
        std::cout << "🧪 Starting QGNN Integration Tests\n";
        std::cout << "=====================================\n\n";
        
        bool all_passed = true;
        
        all_passed &= test_graph_native_structures();
        all_passed &= test_graph_moe_router();
        all_passed &= test_migration_adapter();
        all_passed &= test_scalability();
        all_passed &= test_energy_efficiency();
        all_passed &= test_gf3_compliance();
        
        std::cout << "\n=====================================\n";
        std::cout << "QGNN Integration Tests: " << (all_passed ? "✅ PASSED" : "❌ FAILED") << "\n";
        
        return all_passed;
    }

private:
    /**
     * @brief Test graph-native data structures
     */
    static bool test_graph_native_structures() {
        std::cout << "📊 Testing Graph-Native Data Structures...\n";
        
        try {
            // Create graph
            auto graph = create_qgnn_graph();
            assert(graph != nullptr);
            
            // Add expert nodes
            std::vector<NodeID> expert_ids;
            for (size_t i = 0; i < 10; ++i) {
                NodeID expert_id = graph->add_expert_node();
                expert_ids.push_back(expert_id);
                assert(graph->has_node(expert_id));
            }
            
            // Add entanglement edges
            for (size_t i = 0; i < expert_ids.size() - 1; ++i) {
                graph->add_entanglement_edge(
                    expert_ids[i], expert_ids[i + 1],
                    ternary::Trit::POSITIVE,
                    ternary::EnergyTrit::LOW
                );
            }
            
            // Test graph statistics
            auto stats = graph->get_stats();
            assert(stats.node_count == 10);
            assert(stats.edge_count >= 9);  // At least the chain connections
            
            // Test expert access
            for (const auto& expert_id : expert_ids) {
                auto* expert = graph->get_expert_node(expert_id);
                assert(expert != nullptr);
                assert(expert->node_id == expert_id);
            }
            
            // Test neighbor access
            auto neighbors = graph->get_neighbors(expert_ids[0]);
            assert(!neighbors.empty());
            
            std::cout << "✅ Graph-Native Data Structures: PASSED\n\n";
            return true;
            
        } catch (const std::exception& e) {
            std::cout << "❌ Graph-Native Data Structures: FAILED - " << e.what() << "\n\n";
            return false;
        }
    }
    
    /**
     * @brief Test graph-based MoE router
     */
    static bool test_graph_moe_router() {
        std::cout << "🔄 Testing Graph MoE Router...\n";
        
        try {
            // Configure graph router
            GraphMoERouter::GraphConfig config;
            config.max_experts = 16;
            config.active_experts = 4;
            config.specialization_dim = 8;
            config.energy_budget = ternary::EnergyTrit::MEDIUM;
            config.load_threshold = 75;
            
            auto router = create_graph_moe_router(config);
            assert(router != nullptr);
            
            // Create test input
            std::vector<ternary::Trit> input(8, ternary::Trit::POSITIVE);
            input[2] = ternary::Trit::NEGATIVE;
            input[5] = ternary::Trit::ZERO;
            
            // Test routing
            auto result = router->route_quantum_graph(input, 4);
            assert(!result.selected_experts.empty());
            assert(result.selected_experts.size() <= 4);
            assert(result.routing_latency_us > 0);
            
            // Test load balancing
            auto load_balance_result = router->graph_load_balance();
            assert(!load_balance_result.selected_experts.empty());
            
            // Test statistics
            auto stats = router->get_graph_stats();
            assert(stats.node_count == 16);
            
            auto routing_stats = router->get_routing_stats();
            assert(routing_stats.total_routings >= 1);
            
            std::cout << "✅ Graph MoE Router: PASSED\n\n";
            return true;
            
        } catch (const std::exception& e) {
            std::cout << "❌ Graph MoE Router: FAILED - " << e.what() << "\n\n";
            return false;
        }
    }
    
    /**
     * @brief Test migration adapter
     */
    static bool test_migration_adapter() {
        std::cout << "🔄 Testing Migration Adapter...\n";
        
        try {
            // Configure legacy router
            moe::ExpertConfig legacy_config;
            legacy_config.total_experts = 12;
            legacy_config.active_experts = 3;
            legacy_config.routing_qutrits = 6;
            
            // Configure graph router
            GraphMoERouter::GraphConfig graph_config;
            graph_config.max_experts = 12;
            graph_config.active_experts = 3;
            graph_config.specialization_dim = 6;
            graph_config.energy_budget = ternary::EnergyTrit::MEDIUM;
            graph_config.load_threshold = 70;
            
            // Configure migration
            GraphMigrationAdapter::MigrationConfig migration_config;
            migration_config.use_graph_routing = false;
            migration_config.migration_ratio = 0.5;
            migration_config.enable_performance_logging = false;
            migration_config.migration_batch_size = 10;
            
            auto adapter = create_migration_adapter(
                legacy_config, graph_config, migration_config);
            assert(adapter != nullptr);
            
            // Create test input
            std::vector<ternary::Trit> input(6, ternary::Trit::POSITIVE);
            
            // Test unified routing
            auto result = adapter->route_unified(input, 3);
            assert(!result.empty());
            assert(result.size() <= 3);
            
            // Test array routing
            auto array_result = adapter->route_array(input, 3);
            assert(!array_result.empty());
            
            // Test graph routing
            auto graph_result = adapter->route_graph(input, 3);
            assert(!graph_result.empty());
            
            // Test migration progress
            double progress = adapter->get_migration_progress();
            assert(progress >= 0.0 && progress <= 1.0);
            
            // Test performance benchmark
            auto metrics = adapter->benchmark_performance(input, 10);
            assert(metrics.array_routing_latency_us > 0);
            assert(metrics.graph_routing_latency_us > 0);
            
            std::cout << "✅ Migration Adapter: PASSED\n\n";
            return true;
            
        } catch (const std::exception& e) {
            std::cout << "❌ Migration Adapter: FAILED - " << e.what() << "\n\n";
            return false;
        }
    }
    
    /**
     * @brief Test scalability with larger graphs
     */
    static bool test_scalability() {
        std::cout << "📈 Testing Scalability...\n";
        
        try {
            // Test with larger graph
            GraphMoERouter::GraphConfig config;
            config.max_experts = 243;
            config.active_experts = 8;
            config.specialization_dim = 16;
            config.energy_budget = ternary::EnergyTrit::MEDIUM;
            config.load_threshold = 80;
            
            auto router = create_graph_moe_router(config);
            assert(router != nullptr);
            
            // Create larger input
            std::vector<ternary::Trit> input(16, ternary::Trit::POSITIVE);
            for (size_t i = 0; i < input.size(); i += 3) {
                input[i] = ternary::Trit::NEGATIVE;
            }
            
            // Measure routing performance
            auto start = std::chrono::high_resolution_clock::now();
            auto result = router->route_quantum_graph(input, 8);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            // Should complete within reasonable time
            assert(duration.count() < 10000);  // Less than 10ms
            assert(!result.selected_experts.empty());
            assert(result.selected_experts.size() <= 8);
            
            // Test graph statistics
            auto stats = router->get_graph_stats();
            assert(stats.node_count == 100);
            assert(stats.avg_degree > 0);
            
            std::cout << "✅ Scalability: PASSED (" << duration.count() << " μs)\n\n";
            return true;
            
        } catch (const std::exception& e) {
            std::cout << "❌ Scalability: FAILED - " << e.what() << "\n\n";
            return false;
        }
    }
    
    /**
     * @brief Test energy efficiency
     */
    static bool test_energy_efficiency() {
        std::cout << "⚡ Testing Energy Efficiency...\n";
        
        try {
            GraphMoERouter::GraphConfig config;
            config.max_experts = 32;
            config.active_experts = 4;
            config.specialization_dim = 12;
            config.energy_budget = ternary::EnergyTrit::LOW;  // Low energy budget
            config.load_threshold = 60;
            
            auto router = create_graph_moe_router(config);
            assert(router != nullptr);
            
            std::vector<ternary::Trit> input(12, ternary::Trit::POSITIVE);
            
            // Test energy-aware routing
            auto result = router->energy_aware_routing(input, 4);
            assert(!result.selected_experts.empty());
            
            // Should respect energy budget
            assert(result.total_energy == ternary::EnergyTrit::LOW || 
                   result.total_energy == ternary::EnergyTrit::MEDIUM);
            
            // Test graph energy tracking
            auto stats = router->get_graph_stats();
            assert(stats.total_energy == ternary::EnergyTrit::LOW);
            
            std::cout << "✅ Energy Efficiency: PASSED\n\n";
            return true;
            
        } catch (const std::exception& e) {
            std::cout << "❌ Energy Efficiency: FAILED - " << e.what() << "\n\n";
            return false;
        }
    }
    
    /**
     * @brief Test GF(3) compliance
     */
    static bool test_gf3_compliance() {
        std::cout << "🔢 Testing GF(3) Compliance...\n";
        
        try {
            // Create graph and router
            auto graph = create_qgnn_graph();
            assert(graph != nullptr);
            
            GraphMoERouter::GraphConfig config;
            config.max_experts = 8;
            config.active_experts = 2;
            config.specialization_dim = 4;
            config.energy_budget = ternary::EnergyTrit::MEDIUM;
            config.load_threshold = 50;
            
            auto router = create_graph_moe_router(config);
            assert(router != nullptr);
            
            // Test ternary operations
            std::vector<ternary::Trit> input(4);
            input[0] = ternary::Trit::POSITIVE;
            input[1] = ternary::Trit::NEGATIVE;
            input[2] = ternary::Trit::ZERO;
            input[3] = ternary::Trit::POSITIVE;
            
            // Verify all operations use ternary types
            auto result = router->route_quantum_graph(input, 2);
            assert(!result.selected_experts.empty());
            
            // Check energy tracking uses ternary
            assert(result.total_energy == ternary::EnergyTrit::LOW ||
                   result.total_energy == ternary::EnergyTrit::MEDIUM ||
                   result.total_energy == ternary::EnergyTrit::HIGH);
            
            // Check confidence uses ternary probability
            assert(result.confidence_score == ternary::ProbTrit::LOW_PROB ||
                   result.confidence_score == ternary::ProbTrit::MED_PROB ||
                   result.confidence_score == ternary::ProbTrit::HIGH_PROB);
            
            // Test ternary arithmetic in graph
            for (size_t i = 0; i < 3; ++i) {
                NodeID node1 = graph->add_expert_node();
                NodeID node2 = graph->add_expert_node();
                
                graph->add_entanglement_edge(
                    node1, node2,
                    ternary::Trit::POSITIVE,
                    ternary::EnergyTrit::LOW
                );
                
                auto* expert1 = graph->get_expert_node(node1);
                auto* expert2 = graph->get_expert_node(node2);
                
                assert(expert1 != nullptr);
                assert(expert2 != nullptr);
                assert(expert1->energy_level == ternary::EnergyTrit::MEDIUM);
                assert(expert2->energy_level == ternary::EnergyTrit::MEDIUM);
            }
            
            std::cout << "✅ GF(3) Compliance: PASSED\n\n";
            return true;
            
        } catch (const std::exception& e) {
            std::cout << "❌ GF(3) Compliance: FAILED - " << e.what() << "\n\n";
            return false;
        }
    }
};

} // namespace q_mini_wasm_v2::core::qgnn

/**
 * @brief Main function for QGNN integration tests
 */
int main() {
    return q_mini_wasm_v2::core::qgnn::QGNNIntegrationTest::run_all_tests() ? 0 : 1;
}
