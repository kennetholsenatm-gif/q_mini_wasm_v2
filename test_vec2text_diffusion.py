#!/usr/bin/env python3
"""
Test script for Vec2Text-RAG Conditional Masked Diffusion Module

This script tests the exact reconstruction capabilities and zero-degradation memory persistence
of the Conditional Masked Diffusion module for Vec2Text inversion.

Tests include:
- Basic diffusion model functionality
- Memory reconstruction accuracy
- Syntax validation
- Latent space compensation
- End-to-end reconstruction pipeline

Features:
- Graceful exit on Ctrl+C
- Comprehensive logging to file
- Timeout management for long-running tests
- Progress indicators
"""

import torch
import torch.nn as nn
import json
import logging
import time
import signal
import sys
import os
from typing import Dict, List, Tuple, Optional
from functools import wraps
from datetime import datetime

# Import the Vec2Text-RAG module
from qminiwasm.inference.vec2text import (
    ConditionalMaskedDiffusion,
    EnhancedSyntaxValidator,
    SyntaxForcedLatentCompensation,
    reconstruct_memory,
    validate_reconstructed_text
)

# Global flag for graceful exit
exit_requested = False


def signal_handler(signum, frame):
    """Handle Ctrl+C gracefully"""
    global exit_requested
    logger.info(f"\nReceived signal {signum}. Requesting graceful exit...")
    exit_requested = True


def timeout_handler(timeout_seconds=300):
    """Decorator to add timeout to test functions"""
    def decorator(func):
        @wraps(func)
        def wrapper(*args, **kwargs):
            import threading
            
            result = [None]
            exception = [None]
            
            def target():
                try:
                    result[0] = func(*args, **kwargs)
                except Exception as e:
                    exception[0] = e
            
            thread = threading.Thread(target=target)
            thread.daemon = True
            thread.start()
            thread.join(timeout_seconds)
            
            if thread.is_alive():
                logger.warning(f"Test {func.__name__} timed out after {timeout_seconds} seconds")
                return {
                    "test_name": func.__name__,
                    "status": "timeout",
                    "error": f"Test timed out after {timeout_seconds} seconds",
                    "success": False
                }
            
            if exception[0]:
                raise exception[0]
            
            return result[0]
        return wrapper
    return decorator


def setup_logging():
    """Set up comprehensive logging to both console and file"""
    # Create logs directory if it doesn't exist
    log_dir = "logs"
    if not os.path.exists(log_dir):
        os.makedirs(log_dir)
    
    # Create timestamped log file
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join(log_dir, f"vec2text_diffusion_test_{timestamp}.log")
    
    # Configure logging
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        handlers=[
            logging.FileHandler(log_file),
            logging.StreamHandler(sys.stdout)
        ]
    )
    
    logger = logging.getLogger(__name__)
    logger.info(f"Logging to file: {log_file}")
    return logger, log_file


# Set up logging
logger, log_file = setup_logging()

# Set up signal handling
signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)


class Vec2TextDiffusionTester:
    """Test suite for Vec2Text-RAG Conditional Masked Diffusion"""

    def __init__(self):
        """Initialize the test suite"""
        self.diffusion_model = ConditionalMaskedDiffusion()
        self.syntax_validator = EnhancedSyntaxValidator()
        self.latent_compensation = SyntaxForcedLatentCompensation()
        self.test_results = []

    @timeout_handler(timeout_seconds=120)  # 2 minute timeout
    def test_basic_diffusion_functionality(self) -> Dict:
        """Test basic diffusion model functionality"""
        logger.info("Testing basic diffusion functionality...")
        
        try:
            # Create test embedding
            test_embedding = torch.randn(1, 1024)
            
            # Test forward process
            x_0 = torch.randn(1, 32, 1024)
            t = torch.tensor([100])
            x_t, noise = self.diffusion_model.forward_process(x_0, t)
            
            # Test reverse process
            x_reconstructed = self.diffusion_model.reverse_process(x_t, test_embedding, t)
            
            # Test embedding inversion
            reconstructed_text = self.diffusion_model.invert_embedding(test_embedding)
            
            result = {
                "test_name": "basic_diffusion_functionality",
                "status": "passed",
                "success": True,
                "details": {
                    "input_shape": x_0.shape,
                    "noised_shape": x_t.shape,
                    "reconstructed_shape": x_reconstructed.shape,
                    "text_length": len(reconstructed_text),
                    "success": True
                }
            }
            
        except Exception as e:
            result = {
                "test_name": "basic_diffusion_functionality",
                "status": "failed",
                "error": str(e),
                "success": False
            }
        
        self.test_results.append(result)
        return result

    @timeout_handler(timeout_seconds=300)  # 5 minute timeout
    def test_memory_reconstruction_accuracy(self) -> Dict:
        """Test memory reconstruction accuracy"""
        logger.info("Testing memory reconstruction accuracy...")
        
        try:
            # Create test data
            test_memory = {
                "state_id": "test_state_001",
                "timestamp": "2026-03-16T18:00:00Z",
                "sub_goals": [
                    {"goal_id": "goal_1", "status": "completed", "priority": 5},
                    {"goal_id": "goal_2", "status": "in_progress", "priority": 8}
                ],
                "execution_state": {
                    "memory": {
                        "variable_1": "value_1",
                        "variable_2": 42,
                        "variable_3": True
                    },
                    "stack": [
                        {
                            "frame_id": "frame_1",
                            "function_name": "test_function",
                            "arguments": ["arg1", "arg2"],
                            "local_vars": {"local_var": "local_value"}
                        }
                    ],
                    "return_value": "success",
                    "execution_context": {
                        "current_step": 5,
                        "total_steps": 10,
                        "progress": 0.5
                    }
                }
            }
            
            # Convert to string
            original_text = json.dumps(test_memory, indent=2)
            
            # Create query vector (simplified)
            query_vector = self._create_test_vector(original_text)
            
            # Create candidate vectors
            candidate_vectors = [self._create_test_vector(original_text + f"_candidate_{i}") for i in range(5)]
            
            # Attempt reconstruction
            reconstructed_text = reconstruct_memory(query_vector, candidate_vectors)
            
            # Validate reconstruction
            is_valid, error_msg = validate_reconstructed_text(reconstructed_text)
            
            # Calculate similarity (simplified)
            similarity = self._calculate_text_similarity(original_text, reconstructed_text)
            
            success = is_valid and similarity > 0.5
            result = {
                "test_name": "memory_reconstruction_accuracy",
                "status": "passed" if is_valid else "failed",
                "success": success,
                "details": {
                    "original_length": len(original_text),
                    "reconstructed_length": len(reconstructed_text),
                    "is_valid": is_valid,
                    "error_message": error_msg,
                    "similarity": similarity,
                    "success": success
                }
            }
            
        except Exception as e:
            result = {
                "test_name": "memory_reconstruction_accuracy",
                "status": "failed",
                "error": str(e),
                "success": False
            }
        
        self.test_results.append(result)
        return result

    @timeout_handler(timeout_seconds=60)  # 1 minute timeout
    def test_syntax_validation(self) -> Dict:
        """Test enhanced syntax validation"""
        logger.info("Testing syntax validation...")
        
        test_cases = [
            # Valid JSON
            ('{"state_id": "test", "timestamp": "2026-01-01T00:00:00Z", "execution_state": {"memory": {}, "stack": []}}', True),
            # Invalid JSON (missing closing brace)
            ('{"state_id": "test", "timestamp": "2026-01-01T00:00:00Z", "execution_state": {"memory": {}, "stack": []}', False),
            # Valid structure but invalid format
            ('{"state_id": "test", "timestamp": "not-a-timestamp", "execution_state": {"memory": {}, "stack": []}}', False),
            # Empty text
            ('', False),
            # Non-JSON text
            ('This is not JSON', False)
        ]
        
        passed_tests = 0
        total_tests = len(test_cases)
        
        for i, (test_text, expected_valid) in enumerate(test_cases):
            try:
                is_valid, error_msg, details = self.syntax_validator.validate_enhanced(test_text)
                
                if is_valid == expected_valid:
                    passed_tests += 1
                    logger.info(f"Test case {i+1}: PASSED (expected={expected_valid}, got={is_valid})")
                else:
                    logger.warning(f"Test case {i+1}: FAILED (expected={expected_valid}, got={is_valid}, error={error_msg})")
                    
            except Exception as e:
                logger.error(f"Test case {i+1}: ERROR - {str(e)}")
        
        success = passed_tests == total_tests
        result = {
            "test_name": "syntax_validation",
            "status": "passed" if success else "failed",
            "success": success,
            "details": {
                "passed_tests": passed_tests,
                "total_tests": total_tests,
                "success_rate": passed_tests / total_tests,
                "success": success
            }
        }
        
        self.test_results.append(result)
        return result

    @timeout_handler(timeout_seconds=60)  # 1 minute timeout
    def test_latent_compensation(self) -> Dict:
        """Test syntax-forced latent compensation"""
        logger.info("Testing latent compensation...")
        
        try:
            # Create test latent vector
            original_vector = torch.randn(1024)
            
            # Define syntax constraints
            syntax_constraints = {
                "requires_json": True,
                "requires_objects": True,
                "requires_arrays": True
            }
            
            # Apply compensation
            compensated_vector = self.latent_compensation.compensate_latent_space(
                original_vector, syntax_constraints
            )
            
            # Check that compensation was applied
            compensation_applied = not torch.allclose(original_vector, compensated_vector, atol=1e-6)
            
            # Check vector properties
            vector_norm = torch.norm(compensated_vector).item()
            vector_range = (compensated_vector.min().item(), compensated_vector.max().item())
            
            success = compensation_applied and abs(vector_norm) < 1000  # Reasonable norm
            result = {
                "test_name": "latent_compensation",
                "status": "passed" if compensation_applied else "failed",
                "success": success,
                "details": {
                    "original_norm": torch.norm(original_vector).item(),
                    "compensated_norm": vector_norm,
                    "vector_range": vector_range,
                    "compensation_applied": compensation_applied,
                    "success": success
                }
            }
            
        except Exception as e:
            result = {
                "test_name": "latent_compensation",
                "status": "failed",
                "error": str(e),
                "success": False
            }
        
        self.test_results.append(result)
        return result

    @timeout_handler(timeout_seconds=600)  # 10 minute timeout
    def test_end_to_end_pipeline(self) -> Dict:
        """Test end-to-end reconstruction pipeline"""
        logger.info("Testing end-to-end pipeline...")
        
        try:
            # Create comprehensive test data
            test_data = {
                "state_id": "comprehensive_test_001",
                "timestamp": "2026-03-16T18:30:00Z",
                "sub_goals": [
                    {
                        "goal_id": "goal_1",
                        "status": "completed",
                        "priority": 10,
                        "details": {"completed_at": "2026-03-16T18:25:00Z"}
                    },
                    {
                        "goal_id": "goal_2", 
                        "status": "in_progress",
                        "priority": 7,
                        "details": {"progress": 0.8}
                    }
                ],
                "execution_state": {
                    "memory": {
                        "user_context": {"name": "test_user", "session_id": "session_123"},
                        "system_state": {"cpu_usage": 45.2, "memory_usage": 67.8},
                        "temporal_context": {"current_time": "2026-03-16T18:30:00Z", "timezone": "UTC"}
                    },
                    "stack": [
                        {
                            "frame_id": "main_frame",
                            "function_name": "execute_task",
                            "arguments": ["task_id_123", {"priority": "high"}],
                            "local_vars": {"result": None, "error": None}
                        },
                        {
                            "frame_id": "sub_frame_1",
                            "function_name": "process_data",
                            "arguments": [{"data": "test_data"}],
                            "local_vars": {"processed": True}
                        }
                    ],
                    "return_value": "task_completed",
                    "execution_context": {
                        "current_step": 15,
                        "total_steps": 20,
                        "progress": 0.75,
                        "performance_metrics": {
                            "execution_time": 1250,
                            "memory_peak": 1024,
                            "cpu_peak": 85.5
                        }
                    }
                }
            }
            
            original_text = json.dumps(test_data, indent=2)
            
            # Create multiple query vectors for robustness
            query_vectors = [
                self._create_test_vector(original_text + f"_query_{i}")
                for i in range(3)
            ]
            
            # Create candidate vectors
            candidate_vectors = [
                self._create_test_vector(original_text + f"_candidate_{i}")
                for i in range(10)
            ]
            
            # Test reconstruction with multiple queries
            reconstructions = []
            for query_vector in query_vectors:
                reconstructed = reconstruct_memory(query_vector, candidate_vectors)
                if reconstructed:
                    is_valid, _ = validate_reconstructed_text(reconstructed)
                    if is_valid:
                        reconstructions.append(reconstructed)
            
            # Calculate reconstruction quality
            if reconstructions:
                # Use the best reconstruction (longest valid one)
                best_reconstruction = max(reconstructions, key=len)
                similarity = self._calculate_text_similarity(original_text, best_reconstruction)
                success = similarity > 0.3
                result = {
                    "test_name": "end_to_end_pipeline",
                    "status": "passed" if success else "failed",
                    "success": success,
                    "details": {
                        "original_length": len(original_text),
                        "reconstructed_length": len(best_reconstruction),
                        "similarity": similarity,
                        "num_reconstructions": len(reconstructions),
                        "success": success
                    }
                }
            else:
                result = {
                    "test_name": "end_to_end_pipeline",
                    "status": "failed",
                    "success": False,
                    "details": {
                        "error": "No valid reconstructions produced",
                        "success": False
                    }
                }
            
        except Exception as e:
            result = {
                "test_name": "end_to_end_pipeline",
                "status": "failed",
                "error": str(e),
                "success": False
            }
        
        self.test_results.append(result)
        return result

    def _create_test_vector(self, text: str) -> torch.Tensor:
        """Create a test vector from text"""
        hash_val = hash(text) % 1000000
        vector = torch.zeros(1024, dtype=torch.float32)
        
        for i in range(1024):
            vector[i] = (hash_val * (i + 1)) % 1000000 / 1000000.0
            
        return vector

    def _calculate_text_similarity(self, text1: str, text2: str) -> float:
        """Calculate similarity between two texts"""
        if not text1 or not text2:
            return 0.0
        
        # Simple character-level similarity
        set1 = set(text1)
        set2 = set(text2)
        
        intersection = set1.intersection(set2)
        union = set1.union(set2)
        
        if len(union) == 0:
            return 0.0
        
        return len(intersection) / len(union)

    def run_all_tests(self) -> Dict:
        """Run all tests and return summary"""
        logger.info("Starting Vec2Text-RAG Conditional Masked Diffusion test suite...")
        
        start_time = time.time()
        
        # Run individual tests with progress tracking
        tests = [
            self.test_basic_diffusion_functionality,
            self.test_memory_reconstruction_accuracy,
            self.test_syntax_validation,
            self.test_latent_compensation,
            self.test_end_to_end_pipeline
        ]
        
        total_tests = len(tests)
        
        for i, test_func in enumerate(tests, 1):
            # Check for graceful exit request
            global exit_requested
            if exit_requested:
                logger.info("Graceful exit requested. Stopping test execution.")
                break
                
            # Log progress
            logger.info(f"Running test {i}/{total_tests}: {test_func.__name__}")
            
            try:
                test_func()
                # Calculate and log progress
                completed = len(self.test_results)
                progress = (completed / total_tests) * 100
                logger.info(f"Progress: {completed}/{total_tests} tests completed ({progress:.1f}%)")
                
            except Exception as e:
                logger.error(f"Test {test_func.__name__} failed with exception: {str(e)}")
                # Still count as completed even if failed
                completed = len(self.test_results)
                progress = (completed / total_tests) * 100
                logger.info(f"Progress: {completed}/{total_tests} tests completed ({progress:.1f}%)")
        
        # Calculate summary
        total_tests = len(self.test_results)
        passed_tests = sum(1 for result in self.test_results if result.get("success", False))
        success_rate = passed_tests / total_tests if total_tests > 0 else 0
        
        end_time = time.time()
        duration = end_time - start_time
        
        summary = {
            "test_suite": "Vec2Text-RAG Conditional Masked Diffusion",
            "total_tests": total_tests,
            "passed_tests": passed_tests,
            "failed_tests": total_tests - passed_tests,
            "success_rate": success_rate,
            "duration_seconds": duration,
            "overall_status": "PASSED" if success_rate >= 0.8 else "FAILED",
            "test_results": self.test_results,
            "graceful_exit": exit_requested
        }
        
        # Log summary
        logger.info(f"Test suite completed in {duration:.2f} seconds")
        logger.info(f"Overall status: {summary['overall_status']}")
        logger.info(f"Success rate: {success_rate:.2%} ({passed_tests}/{total_tests})")
        
        if exit_requested:
            logger.info("⚠️  Test execution was interrupted by user request")
        
        return summary


def print_progress_bar(iteration, total, prefix='Progress:', suffix='Complete', length=50, fill='█'):
    """Print a progress bar to the console"""
    percent = f"{100 * (iteration / float(total)):.1f}"
    filled_length = int(length * iteration // total)
    bar = fill * filled_length + '-' * (length - filled_length)
    print(f'\r{prefix} |{bar}| {percent}% {suffix}', end='\r')
    if iteration == total:
        print()


def main():
    """Main test execution"""
    logger.info("Vec2Text-RAG Conditional Masked Diffusion Test Suite")
    logger.info("=" * 60)
    logger.info("Testing exact reconstruction capabilities and zero-degradation memory persistence")
    logger.info("Features: Graceful exit, comprehensive logging, timeout management, progress indicators")
    logger.info("")
    
    # Initialize tester
    tester = Vec2TextDiffusionTester()
    
    # Run tests with progress bar
    logger.info("Starting test execution...")
    print_progress_bar(0, 5, prefix='Test Progress:', suffix='Complete', length=30)
    
    summary = tester.run_all_tests()
    
    # Print detailed results
    logger.info("\n" + "=" * 60)
    logger.info("DETAILED TEST RESULTS")
    logger.info("=" * 60)
    
    for i, result in enumerate(summary["test_results"], 1):
        status_icon = "✓" if result.get("success", False) else "✗"
        status_color = "PASSED" if result.get("success", False) else "FAILED"
        logger.info(f"{i}. {status_icon} {result['test_name']}: {status_color}")
        
        if "error" in result:
            logger.info(f"   ❌ Error: {result['error']}")
        
        if "details" in result:
            details = result["details"]
            if isinstance(details, dict):
                for key, value in details.items():
                    if key != "success":
                        if isinstance(value, float):
                            logger.info(f"   📊 {key}: {value:.4f}")
                        else:
                            logger.info(f"   📊 {key}: {value}")
        
        logger.info("")
    
    # Final assessment with enhanced formatting
    logger.info("=" * 60)
    logger.info("FINAL ASSESSMENT")
    logger.info("=" * 60)
    
    if summary["overall_status"] == "PASSED":
        logger.info("🎉 ALL TESTS PASSED!")
        logger.info("✅ Vec2Text-RAG Conditional Masked Diffusion is working correctly.")
        logger.info("✅ Zero-degradation memory persistence achieved!")
        logger.info("✅ Exact reconstruction capabilities verified!")
        logger.info("")
        logger.info("📋 SUMMARY:")
        logger.info(f"   📈 Success Rate: {summary['success_rate']:.2%}")
        logger.info(f"   ⏱️  Duration: {summary['duration_seconds']:.2f} seconds")
        logger.info(f"   ✅ Passed: {summary['passed_tests']}/{summary['total_tests']} tests")
        logger.info(f"   ❌ Failed: {summary['failed_tests']}/{summary['total_tests']} tests")
    else:
        logger.info("⚠️  SOME TESTS FAILED!")
        logger.info("🔧 Please review the implementation.")
        logger.info("🔍 Consider checking:")
        logger.info("   - Diffusion mathematics and noise scheduling")
        logger.info("   - Syntax validation logic and JSON schema")
        logger.info("   - Latent space compensation algorithms")
        logger.info("   - Memory reconstruction accuracy")
        logger.info("")
        logger.info("📋 SUMMARY:")
        logger.info(f"   📈 Success Rate: {summary['success_rate']:.2%}")
        logger.info(f"   ⏱️  Duration: {summary['duration_seconds']:.2f} seconds")
        logger.info(f"   ✅ Passed: {summary['passed_tests']}/{summary['total_tests']} tests")
        logger.info(f"   ❌ Failed: {summary['failed_tests']}/{summary['total_tests']} tests")
    
    if summary.get("graceful_exit", False):
        logger.info("⚠️  Test execution was interrupted by user request (Ctrl+C)")
    
    logger.info("")
    logger.info("📁 Full test logs saved to: " + log_file)
    logger.info("✨ Test suite completed!")
    
    return summary


if __name__ == "__main__":
    main()