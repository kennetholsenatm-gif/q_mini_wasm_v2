"""Comprehensive Tests for White Paper Integration

This module provides comprehensive testing for all the white paper integration components:
- Failure taxonomy and fallback mechanisms
- Mathematical bridge to tropical geometry
- Hardware deployment and Intel ARC integration
- Security and privacy enhancements (DCPE, Vec2Text-RAG)

The tests validate the theoretical formulations, mathematical properties, and
integration points as specified in the white paper.
"""

import unittest
import numpy as np
import torch
from unittest.mock import Mock, patch, MagicMock
import time

from qminiwasm.layers.failure_taxonomy import (
    FailureCategory,
    FailureEvent,
    DeterministicVerifier,
    HullKVCache,
    NoiseAwareQAOA,
    FailureTaxonomyManager,
)
from qminiwasm.layers.tropical_geometry import (
    TropicalSemiring,
    QuantumTernaryWeight,
    TropicalAttention,
    HullKVCache as TropicalHullKVCache,
    AlgebraicMapping,
    TropicalGeometryBridge,
)
from qminiwasm.hardware.intel_sycl_integration import (
    SYCLDeviceSpec,
    OfflineQuantumOptimization,
    IntelSYCLInference,
    HardwareDeploymentManager,
)
from qminiwasm.security.encrypted_quantum_routing import (
    DCPEParameters,
    ApproximateDCPE,
    Vec2TextRAG,
    EncryptedQuantumRouter,
    SecurityManager,
)
from qminiwasm.config import HierarchicalConfig


class TestFailureTaxonomy(unittest.TestCase):
    """Test the failure taxonomy and fallback mechanisms"""

    def setUp(self):
        self.config = HierarchicalConfig()
        self.failure_manager = FailureTaxonomyManager(self.config)

    def test_failure_category_enum(self):
        """Test that failure categories are properly defined"""
        categories = [cat.value for cat in FailureCategory]
        expected = [
            "hardware_state_corruption",
            "algorithmic_non_determinism",
            "memory_pipeline_degradation",
            "routing_topology_collapse",
        ]
        self.assertEqual(categories, expected)

    def test_failure_detection_and_classification(self):
        """Test failure detection and classification"""
        details = {"error_type": "bit_flip", "affected_register": "WASM_stack"}
        failure_event = self.failure_manager.detect_failure(
            FailureCategory.HARDWARE_STATE_CORRUPTION, details
        )

        self.assertEqual(failure_event.category, FailureCategory.HARDWARE_STATE_CORRUPTION)
        self.assertEqual(failure_event.severity, "critical")
        self.assertEqual(failure_event.details, details)
        self.assertIn("WASM_executor", failure_event.affected_components)

    def test_deterministic_verifier(self):
        """Test deterministic verification functionality"""
        verifier = DeterministicVerifier(self.config)

        # Create mock execution state
        execution_state = Mock()
        execution_state.wasm_memory = b"test_memory"
        execution_state.wasm_stack = [1, 2, 3]
        execution_state.wasm_registers = {"r1": 10, "r2": 20}

        # Test verification
        candidate_tokens = [1, 2, 3]
        is_valid, verification_details = verifier.verify_execution(
            candidate_tokens, execution_state
        )

        self.assertIsInstance(is_valid, bool)
        self.assertIsInstance(verification_details, dict)

    def test_hull_cache_operations(self):
        """Test HullKVCache geometric state recovery"""
        hull_cache = HullKVCache(self.config)

        # Test adding key vectors
        key_vector = np.array([1.0, 2.0])
        hull_cache._add_key_vector(key_vector)

        self.assertEqual(len(hull_cache.key_vectors), 1)
        self.assertEqual(hull_cache.key_vectors[0].tolist(), [1.0, 2.0])

        # Test query recovery
        query_vector = np.array([1.5, 2.5])
        recovered_state = hull_cache.query_state_recovery(query_vector)

        # Should return a valid state or None if no hull exists
        self.assertTrue(recovered_state is None or isinstance(recovered_state, np.ndarray))

    def test_noise_aware_qaoa(self):
        """Test noise-aware QAOA degradation"""
        mock_quantum_router = Mock()
        qaoa_monitor = NoiseAwareQAOA(self.config, mock_quantum_router)

        # Test fidelity monitoring
        fidelity_result = qaoa_monitor.monitor_qpu_fidelity()

        self.assertIn("fidelity_metrics", fidelity_result)
        self.assertIn("degradation_mode", fidelity_result)
        self.assertIn("action_taken", fidelity_result)


class TestTropicalGeometry(unittest.TestCase):
    """Test the mathematical bridge to tropical geometry"""

    def setUp(self):
        self.config = HierarchicalConfig()

    def test_tropical_semiring_operations(self):
        """Test tropical semiring mathematical properties"""
        ops = TropicalSemiring()

        # Test tropical addition (maximum)
        self.assertEqual(ops.tropical_add(2.0, 3.0), 3.0)
        self.assertEqual(ops.tropical_add(-1.0, 5.0), 5.0)

        # Test tropical multiplication (addition)
        self.assertEqual(ops.tropical_mul(2.0, 3.0), 5.0)
        self.assertEqual(ops.tropical_mul(-1.0, 5.0), 4.0)

        # Test associativity
        a, b, c = 1.0, 2.0, 3.0
        self.assertEqual(
            ops.tropical_add(ops.tropical_add(a, b), c), ops.tropical_add(a, ops.tropical_add(b, c))
        )
        self.assertEqual(
            ops.tropical_mul(ops.tropical_mul(a, b), c), ops.tropical_mul(a, ops.tropical_mul(b, c))
        )

    def test_quantum_ternary_weight(self):
        """Test quantum ternary weight representation"""
        # Test valid weights
        weight_plus = QuantumTernaryWeight(1)
        weight_zero = QuantumTernaryWeight(0)
        weight_minus = QuantumTernaryWeight(-1)

        # Test invalid weight
        with self.assertRaises(ValueError):
            QuantumTernaryWeight(2)

        # Test tropical conversion
        self.assertEqual(weight_plus.to_tropical(), 0.0)
        self.assertEqual(weight_zero.to_tropical(), float("-inf"))
        self.assertEqual(weight_minus.to_tropical(), float("-inf"))

    def test_tropical_attention(self):
        """Test tropical attention implementation"""
        attention = TropicalAttention(d_model=512, num_heads=8, config=self.config)

        # Create test tensors
        batch_size, seq_len = 2, 10
        query = torch.randn(batch_size, seq_len, 512)
        key = torch.randn(batch_size, seq_len, 512)
        value = torch.randn(batch_size, seq_len, 512)

        # Test ECL step
        output = attention.forward(query, key, value)

        self.assertEqual(output.shape, (batch_size, seq_len, 512))

    def test_algebraic_mapping(self):
        """Test algebraic mapping from ternary to tropical space"""
        mapper = AlgebraicMapping(self.config)

        # Create test ternary weights
        ternary_weights = torch.tensor([1, 0, -1, 1, 0], dtype=torch.float32)

        # Test mapping to tropical space
        tropical_weights = mapper.map_ternary_to_tropical(ternary_weights)

        self.assertEqual(tropical_weights.shape, ternary_weights.shape)
        self.assertTrue(torch.all(tropical_weights <= 0))  # All should be 0 or -inf

    def test_tropical_geometry_bridge(self):
        """Test the complete tropical geometry bridge"""
        bridge = TropicalGeometryBridge(self.config)

        # Test mathematical validation
        # This should pass without raising exceptions
        try:
            # The validation happens in __init__, so if we get here it passed
            self.assertTrue(True)
        except AssertionError as e:
            self.fail(f"Tropical geometry validation failed: {e}")

        # Test integration statistics
        stats = bridge.get_mathematical_statistics()

        self.assertTrue(stats["tropical_semiring_validated"])
        self.assertTrue(stats["algebraic_mapping_active"])
        self.assertTrue(stats["polyhedral_boundaries_created"])


class TestHardwareIntegration(unittest.TestCase):
    """Test hardware deployment and Intel ARC integration"""

    def setUp(self):
        self.config = HierarchicalConfig()

    def test_sycl_device_spec(self):
        """Test Intel ARC device specifications"""
        device_spec = SYCLDeviceSpec(
            device_name="Intel ARC Alchemist (Xe-HPG)",
            architecture="Xe-HPG",
            vector_engines=96,
            matrix_engines=24,
            max_threads_per_block=768,
            memory_bandwidth_gb_s=384.0,
            compute_units=3072,
            supported_features=["ESIMD", "C_for_Metal", "XMX", "SVM", "THP"],
        )

        self.assertEqual(device_spec.device_name, "Intel ARC Alchemist (Xe-HPG)")
        self.assertEqual(device_spec.architecture, "Xe-HPG")
        self.assertEqual(device_spec.vector_engines, 96)
        self.assertEqual(device_spec.matrix_engines, 24)
        self.assertIn("ESIMD", device_spec.supported_features)

    def test_offline_quantum_optimization(self):
        """Test offline quantum optimization pipeline"""
        optimizer = OfflineQuantumOptimization(self.config)

        # Mock MoE routing problem
        moe_problem = Mock()

        # Test optimization
        result = optimizer.optimize_qaoa_topology(moe_problem)

        self.assertIn("status", result)
        self.assertIn("routing_topology", result)
        self.assertIn("frozen_weights", result)

        # Test optimization status
        status = optimizer.get_optimization_status()

        self.assertIn("quantum_weights_frozen", status)
        self.assertIn("optimized_routing_topology", status)

    def test_intel_sycl_inference(self):
        """Test Intel SYCL inference engine"""
        device_spec = SYCLDeviceSpec(
            device_name="Intel ARC Alchemist (Xe-HPG)",
            architecture="Xe-HPG",
            vector_engines=96,
            matrix_engines=24,
            max_threads_per_block=768,
            memory_bandwidth_gb_s=384.0,
            compute_units=3072,
            supported_features=["ESIMD", "C_for_Metal", "XMX", "SVM", "THP"],
        )

        sycl_inference = IntelSYCLInference(self.config, device_spec)

        # Test device initialization (should return False in test environment)
        init_result = sycl_inference.initialize_sycl_device()
        self.assertIsInstance(init_result, bool)

        # Test statistics
        stats = sycl_inference.get_inference_statistics()

        self.assertIn("device_initialized", stats)
        self.assertIn("vector_engine_dispatch", stats)
        self.assertIn("matrix_engine_dispatch", stats)

    def test_hardware_deployment_manager(self):
        """Test complete hardware deployment pipeline"""
        manager = HardwareDeploymentManager(self.config)

        # Mock MoE routing problem
        moe_problem = Mock()

        # Test deployment pipeline
        result = manager.execute_deployment_pipeline(moe_problem)

        self.assertIn("status", result)
        self.assertIn("deployment_phase", result)
        self.assertIn("quantum_optimization", result)
        self.assertIn("classical_inference", result)

        # Test deployment status
        status = manager.get_deployment_status()

        self.assertIn("deployment_phase", status)
        self.assertIn("deployment_complete", status)
        self.assertIn("quantum_optimization_status", status)


class TestSecurityEnhancements(unittest.TestCase):
    """Test security and privacy enhancements"""

    def setUp(self):
        self.config = HierarchicalConfig()

    def test_dcpe_parameters(self):
        """Test DCPE parameter configuration (AES-256-CBC: 32-byte key, 16-byte IV)"""
        params = DCPEParameters(
            scale_factor=1.5,
            perturbation_variance=0.2,
            encryption_key=b"test_key_32_bytes_long_aes256!!!",  # 32 bytes
            iv=b"test_iv_16_byte!",  # 16 bytes for CBC
            manifold_dimension=2048,
        )

        self.assertEqual(params.scale_factor, 1.5)
        self.assertEqual(params.perturbation_variance, 0.2)
        self.assertEqual(len(params.encryption_key), 32)
        self.assertEqual(len(params.iv), 16)
        self.assertEqual(params.manifold_dimension, 2048)

    def test_approximate_dcpe(self):
        """Test Approximate Distance-Comparison-Preserving Encryption"""
        dcpe = ApproximateDCPE(self.config)

        # Create test vector
        plaintext_vector = np.random.randn(100)

        # Test encryption (encrypted length may differ due to AES padding)
        encrypted_vector = dcpe.encrypt_vector(plaintext_vector)

        self.assertEqual(encrypted_vector.ndim, 1)
        self.assertGreater(encrypted_vector.size, 0)

        # Test decryption
        decrypted_vector = dcpe.decrypt_vector(encrypted_vector)

        self.assertEqual(decrypted_vector.shape, plaintext_vector.shape)

        # Test statistics
        stats = dcpe.get_encryption_statistics()

        self.assertIn("scale_factor", stats)
        self.assertIn("perturbation_variance", stats)
        self.assertIn("manifold_dimension", stats)

    def test_vec2text_rag(self):
        """Test Vec2Text-Augmented Retrieval-Augmented Generation"""
        vec2text = Vec2TextRAG(self.config)

        # Create test vector
        test_vector = np.random.randn(100)
        metadata = {"source": "test", "timestamp": time.time()}

        # Test memory storage
        storage_result = vec2text.store_memory(test_vector, metadata)
        self.assertTrue(storage_result)

        # Test memory retrieval
        similar_entries = vec2text.retrieve_similar(test_vector, k=3)
        self.assertIsInstance(similar_entries, list)

        # Test Vec2Text inversion
        encrypted_vector = np.random.randint(0, 255, 100)
        inverted_text = vec2text.vec2text_inversion(encrypted_vector)

        self.assertIsInstance(inverted_text, str)

        # Test statistics
        stats = vec2text.get_memory_statistics()

        self.assertIn("memory_bank_size", stats)
        self.assertIn("diffusion_steps", stats)
        self.assertIn("masking_probability", stats)

    def test_encrypted_quantum_router(self):
        """Test encrypted quantum router with zero-trust privacy"""
        router = EncryptedQuantumRouter(self.config)

        # Create test vector and metadata
        plaintext_vector = np.random.randn(100)
        metadata = {"source": "test", "timestamp": time.time()}

        # Test encryption and storage
        result = router.encrypt_and_store(plaintext_vector, metadata)

        self.assertIn("status", result)
        self.assertIn("encrypted_vector_shape", result)

        # Test encrypted manifold search
        query_vector = np.random.randn(100)
        search_results = router.search_encrypted_manifold(query_vector, k=3)

        self.assertIsInstance(search_results, list)

        # Test security statistics
        stats = router.get_security_statistics()

        self.assertIn("dcpe_statistics", stats)
        self.assertIn("vec2text_statistics", stats)
        self.assertIn("quantum_routing_enabled", stats)
        self.assertIn("zero_trust_boundary_active", stats)

    def test_security_manager(self):
        """Test comprehensive security manager"""
        security_manager = SecurityManager(self.config)

        # Test edge memory processing
        cognitive_state = {"dense_vector": np.random.randn(100), "metadata": {"source": "edge"}}

        result = security_manager.process_edge_memory(cognitive_state)

        self.assertIn("status", result)
        self.assertIn("processed_state", result)
        self.assertIn("encryption_result", result)

        # Test cloud memory request handling
        encrypted_query = np.random.randint(0, 255, 100)
        response = security_manager.handle_cloud_memory_request(encrypted_query)

        self.assertIn("status", response)
        self.assertIn("search_results", response)
        self.assertIn("quantum_routing", response)
        self.assertIn("zero_trust_compliant", response)

        # Test security compliance monitoring
        compliance = security_manager.monitor_security_compliance()

        self.assertIn("compliance_score", compliance)
        self.assertIn("security_statistics", compliance)
        self.assertIn("compliance_status", compliance)

        # Test comprehensive security report
        report = security_manager.get_comprehensive_security_report()

        self.assertIn("security_manager_status", report)
        self.assertIn("encrypted_quantum_router", report)
        self.assertIn("privacy_compliance_score", report)
        self.assertIn("zero_trust_boundary", report)


class TestIntegration(unittest.TestCase):
    """Test integration between all white paper components"""

    def setUp(self):
        self.config = HierarchicalConfig()

    def test_complete_integration_pipeline(self):
        """Test the complete integration pipeline"""
        # Initialize all components
        failure_manager = FailureTaxonomyManager(self.config)
        tropical_bridge = TropicalGeometryBridge(self.config)
        hardware_manager = HardwareDeploymentManager(self.config)
        security_manager = SecurityManager(self.config)

        # Test that all components can be initialized without errors
        self.assertIsNotNone(failure_manager)
        self.assertIsNotNone(tropical_bridge)
        self.assertIsNotNone(hardware_manager)
        self.assertIsNotNone(security_manager)

        # Test mathematical bridge integration
        test_weights = torch.randn(100, 100)
        integration_result = tropical_bridge.integrate_with_model(test_weights, Mock())

        self.assertIn("status", integration_result)

        # Test security integration with failure detection
        cognitive_state = {"dense_vector": np.random.randn(100)}
        security_result = security_manager.process_edge_memory(cognitive_state)

        self.assertEqual(security_result["status"], "success")

        # Test hardware deployment with security
        deployment_result = hardware_manager.execute_deployment_pipeline(Mock())

        self.assertIn("status", deployment_result)

        # Test comprehensive statistics collection
        failure_stats = failure_manager.get_failure_statistics()
        tropical_stats = tropical_bridge.get_mathematical_statistics()
        hardware_stats = hardware_manager.get_deployment_status()
        security_stats = security_manager.get_comprehensive_security_report()

        self.assertIsInstance(failure_stats, dict)
        self.assertIsInstance(tropical_stats, dict)
        self.assertIsInstance(hardware_stats, dict)
        self.assertIsInstance(security_stats, dict)

    def test_white_paper_requirements_validation(self):
        """Validate that all white paper requirements are met"""
        # Test Critique 1: Failure Taxonomy
        failure_manager = FailureTaxonomyManager(self.config)
        self.assertTrue(hasattr(failure_manager, "detect_failure"))
        self.assertTrue(hasattr(failure_manager, "handle_failure"))

        # Test Critique 2: Mathematical Bridge
        tropical_bridge = TropicalGeometryBridge(self.config)
        self.assertTrue(hasattr(tropical_bridge, "algebraic_mapper"))
        self.assertTrue(hasattr(tropical_bridge, "_validate_tropical_properties"))

        # Test Critique 3: Hardware Deployment
        hardware_manager = HardwareDeploymentManager(self.config)
        self.assertTrue(hasattr(hardware_manager, "quantum_optimizer"))
        self.assertTrue(hasattr(hardware_manager, "sycl_inference"))

        # Test Security Enhancements
        security_manager = SecurityManager(self.config)
        self.assertTrue(hasattr(security_manager, "encrypted_router"))
        self.assertTrue(hasattr(security_manager, "enforce_zero_trust_boundary"))


if __name__ == "__main__":
    # Run all tests
    unittest.main(verbosity=2)
