"""Integration Testing for Encrypted Quantum Routing System

This module provides comprehensive integration tests for the enhanced QAOA-based routing
over encrypted vectors, including end-to-end validation, performance benchmarking,
and system integration testing.
"""

import time
import asyncio
import logging
import json
from typing import List, Dict, Tuple, Any, Optional
from dataclasses import asdict
import numpy as np
import torch
import matplotlib.pyplot as plt
import seaborn as sns

from qminiwasm.quantum.router import EnhancedQuantumRouter
from qminiwasm.quantum.qubo import (
    encrypted_qubo_hamiltonian,
    encrypted_qubo_to_ising,
    encrypted_affinity_from_compressed,
    encrypted_distance_matrix_to_affinity,
    encrypted_qubo_optimization,
)
from qminiwasm.quantum.performance_optimizer import (
    EncryptedRoutingOptimizer,
    PerformanceMetrics,
    EncryptedRoutingCache,
)
from qminiwasm.security.crypto import encrypt_vector, decrypt_vector
from test_encrypted_quantum_routing import EncryptedQuantumRoutingTester

logger = logging.getLogger(__name__)


class IntegrationTestSuite:
    """Comprehensive integration test suite for encrypted quantum routing."""

    def __init__(self):
        """Initialize the integration test suite."""
        self.router = EnhancedQuantumRouter()
        self.optimizer = EncryptedRoutingOptimizer(self.router)
        self.tester = EncryptedQuantumRoutingTester()
        self.test_results = {}
        self.performance_baselines = {}

    def generate_large_scale_test_data(
        self, num_queries: int = 1000, num_database_vectors: int = 10000, vector_dim: int = 512
    ) -> Tuple[List[torch.Tensor], List[torch.Tensor]]:
        """Generate large-scale test data for integration testing.

        Args:
            num_queries: Number of query vectors
            num_database_vectors: Number of database vectors
            vector_dim: Dimensionality of vectors

        Returns:
            (query_vectors, database_vectors) for testing
        """
        logger.info(
            f"Generating large-scale test data: {num_queries} queries, {num_database_vectors} database vectors, dim={vector_dim}"
        )

        # Generate query vectors
        query_vectors = []
        for _ in range(num_queries):
            vector = torch.randn(vector_dim)
            query_vectors.append(vector)

        # Generate database vectors
        database_vectors = []
        for _ in range(num_database_vectors):
            vector = torch.randn(vector_dim)
            database_vectors.append(vector)

        return query_vectors, database_vectors

    async def test_end_to_end_encrypted_routing(
        self, num_queries: int = 100, num_database_vectors: int = 1000, k: int = 10
    ) -> Dict[str, Any]:
        """Test end-to-end encrypted quantum routing.

        Args:
            num_queries: Number of query vectors to test
            num_database_vectors: Number of database vectors
            k: Number of nearest neighbors to find

        Returns:
            Test results and performance metrics
        """
        logger.info("Testing end-to-end encrypted quantum routing...")

        # Generate test data
        query_vectors, database_vectors = self.generate_large_scale_test_data(
            num_queries, num_database_vectors, vector_dim=256
        )

        # Encrypt vectors
        encrypted_queries = [encrypt_vector(q) for q in query_vectors]
        encrypted_database = [encrypt_vector(d) for d in database_vectors]

        # Test regular routing (baseline)
        regular_results = []
        regular_times = []

        for i, query in enumerate(query_vectors[:10]):  # Test subset for baseline
            start_time = time.perf_counter()
            result = self.router.find_k_nearest_neighbors(
                query.numpy(), [d.numpy() for d in database_vectors], k
            )
            end_time = time.perf_counter()

            regular_results.append(result)
            regular_times.append((end_time - start_time) * 1e6)  # Convert to microseconds

        # Test encrypted routing
        encrypted_results = []
        encrypted_times = []

        start_time = time.perf_counter()
        encrypted_results = await self.optimizer.optimize_for_microsecond_performance(
            encrypted_queries[:10], encrypted_database, k
        )
        end_time = time.perf_counter()

        total_encrypted_time = (end_time - start_time) * 1e6
        avg_encrypted_time = total_encrypted_time / len(encrypted_queries[:10])

        # Calculate accuracy
        accuracy = self._calculate_routing_accuracy(regular_results, encrypted_results["results"])

        # Store results
        results = {
            "regular_routing": {
                "avg_time_us": np.mean(regular_times),
                "min_time_us": np.min(regular_times),
                "max_time_us": np.max(regular_times),
            },
            "encrypted_routing": {
                "avg_time_us": avg_encrypted_time,
                "total_time_us": total_encrypted_time,
                "performance_report": encrypted_results["performance_report"],
            },
            "accuracy": accuracy,
            "success": accuracy > 0.8,  # 80% accuracy threshold
        }

        self.test_results["end_to_end_routing"] = results
        logger.info(
            f"End-to-end test completed. Accuracy: {accuracy:.4f}, Encrypted avg time: {avg_encrypted_time:.2f}μs"
        )

        return results

    def _calculate_routing_accuracy(
        self, regular_results: List[List[int]], encrypted_results: List[List[int]]
    ) -> float:
        """Calculate routing accuracy by comparing results.

        Args:
            regular_results: Results from regular routing
            encrypted_results: Results from encrypted routing

        Returns:
            Accuracy score between 0 and 1
        """
        if len(regular_results) != len(encrypted_results):
            return 0.0

        total_overlap = 0
        total_possible = 0

        for reg, enc in zip(regular_results, encrypted_results):
            overlap = len(set(reg) & set(enc))
            total_overlap += overlap
            total_possible += len(reg)

        return total_overlap / total_possible if total_possible > 0 else 0.0

    async def test_performance_scaling(
        self, test_sizes: List[int] = [100, 500, 1000, 5000, 10000]
    ) -> Dict[str, Any]:
        """Test performance scaling with different dataset sizes.

        Args:
            test_sizes: List of dataset sizes to test

        Returns:
            Performance scaling results
        """
        logger.info("Testing performance scaling...")

        scaling_results = {}

        for size in test_sizes:
            logger.info(f"Testing with dataset size: {size}")

            # Generate test data
            query_vectors, database_vectors = self.generate_large_scale_test_data(
                num_queries=50, num_database_vectors=size, vector_dim=256
            )

            # Encrypt vectors
            encrypted_queries = [encrypt_vector(q) for q in query_vectors]
            encrypted_database = [encrypt_vector(d) for d in database_vectors]

            # Test encrypted routing
            start_time = time.perf_counter()
            results = await self.optimizer.optimize_for_microsecond_performance(
                encrypted_queries, encrypted_database, k=5
            )
            end_time = time.perf_counter()

            total_time = (end_time - start_time) * 1e6
            avg_time_per_query = total_time / len(encrypted_queries)

            # Get performance metrics
            performance_report = results["performance_report"]

            scaling_results[size] = {
                "total_time_us": total_time,
                "avg_time_per_query_us": avg_time_per_query,
                "queries_processed": len(encrypted_queries),
                "throughput_queries_per_sec": len(encrypted_queries) * 1e6 / total_time,
                "performance_stats": performance_report["performance_stats"],
            }

            logger.info(f"Size {size}: {avg_time_per_query:.2f}μs avg per query")

        self.test_results["performance_scaling"] = scaling_results
        return scaling_results

    def test_cache_effectiveness(self) -> Dict[str, Any]:
        """Test cache effectiveness for repeated queries.

        Returns:
            Cache effectiveness results
        """
        logger.info("Testing cache effectiveness...")

        # Generate test data
        query_vectors, database_vectors = self.generate_large_scale_test_data(
            num_queries=100, num_database_vectors=1000, vector_dim=128
        )

        # Encrypt vectors
        encrypted_queries = [encrypt_vector(q) for q in query_vectors]
        encrypted_database = [encrypt_vector(d) for d in database_vectors]

        # Create cache and test repeated queries
        cache = EncryptedRoutingCache(max_size=50)

        # First pass - cache misses
        cache_miss_times = []
        for query in encrypted_queries[:25]:
            start_time = time.perf_counter()
            result = self.router.find_k_nearest_neighbors_encrypted(query, encrypted_database, k=5)
            end_time = time.perf_counter()

            cache_miss_times.append((end_time - start_time) * 1e6)
            cache.put(query, 5, result)

        # Second pass - cache hits
        cache_hit_times = []
        for query in encrypted_queries[:25]:
            start_time = time.perf_counter()
            result = cache.get(query, 5)
            end_time = time.perf_counter()

            if result is not None:
                cache_hit_times.append((end_time - start_time) * 1e6)

        # Calculate effectiveness
        avg_cache_miss_time = np.mean(cache_miss_times) if cache_miss_times else 0
        avg_cache_hit_time = np.mean(cache_hit_times) if cache_hit_times else 0
        speedup = (
            avg_cache_miss_time / avg_cache_hit_time if avg_cache_hit_time > 0 else float("inf")
        )

        results = {
            "avg_cache_miss_time_us": avg_cache_miss_time,
            "avg_cache_hit_time_us": avg_cache_hit_time,
            "cache_speedup": speedup,
            "cache_hit_rate": len(cache_hit_times) / len(encrypted_queries[:25]),
            "success": speedup > 10,  # 10x speedup threshold
        }

        self.test_results["cache_effectiveness"] = results
        logger.info(f"Cache effectiveness test completed. Speedup: {speedup:.2f}x")

        return results

    async def test_microsecond_performance(self, num_queries: int = 1000) -> Dict[str, Any]:
        """Test microsecond-level performance requirements.

        Args:
            num_queries: Number of queries to test

        Returns:
            Microsecond performance results
        """
        logger.info("Testing microsecond-level performance...")

        # Generate test data
        query_vectors, database_vectors = self.generate_large_scale_test_data(
            num_queries=num_queries, num_database_vectors=5000, vector_dim=128
        )

        # Encrypt vectors
        encrypted_queries = [encrypt_vector(q) for q in query_vectors]
        encrypted_database = [encrypt_vector(d) for d in database_vectors]

        # Test with different batch sizes
        batch_sizes = [10, 50, 100, 500]
        performance_results = {}

        for batch_size in batch_sizes:
            logger.info(f"Testing with batch size: {batch_size}")

            start_time = time.perf_counter()
            results = await self.optimizer.optimize_for_microsecond_performance(
                encrypted_queries[:batch_size], encrypted_database, k=3
            )
            end_time = time.perf_counter()

            total_time = (end_time - start_time) * 1e6
            avg_time_per_query = total_time / batch_size

            performance_results[batch_size] = {
                "total_time_us": total_time,
                "avg_time_per_query_us": avg_time_per_query,
                "queries_per_second": batch_size * 1e6 / total_time,
                "p95_latency_us": results["performance_report"]["performance_stats"][
                    "p95_latency_us"
                ],
                "p99_latency_us": results["performance_report"]["performance_stats"][
                    "p99_latency_us"
                ],
            }

            logger.info(f"Batch size {batch_size}: {avg_time_per_query:.2f}μs avg latency")

        # Check if microsecond requirements are met
        meets_requirements = all(
            result["avg_time_per_query_us"] < 1000  # Less than 1ms (1000μs)
            for result in performance_results.values()
        )

        results = {
            "performance_results": performance_results,
            "meets_microsecond_requirements": meets_requirements,
            "best_batch_size": min(
                performance_results.keys(),
                key=lambda k: performance_results[k]["avg_time_per_query_us"],
            ),
            "success": meets_requirements,
        }

        self.test_results["microsecond_performance"] = results
        return results

    def generate_integration_report(self) -> Dict[str, Any]:
        """Generate comprehensive integration test report.

        Returns:
            Integration test report
        """
        logger.info("Generating integration test report...")

        report = {
            "test_summary": {
                "total_tests": len(self.test_results),
                "passed_tests": sum(
                    1 for result in self.test_results.values() if result.get("success", False)
                ),
                "failed_tests": sum(
                    1 for result in self.test_results.values() if not result.get("success", False)
                ),
            },
            "detailed_results": self.test_results,
            "performance_summary": {},
            "recommendations": [],
        }

        # Extract performance metrics
        if "end_to_end_routing" in self.test_results:
            end_to_end = self.test_results["end_to_end_routing"]
            report["performance_summary"]["end_to_end"] = {
                "regular_routing_avg_us": end_to_end["regular_routing"]["avg_time_us"],
                "encrypted_routing_avg_us": end_to_end["encrypted_routing"]["avg_time_us"],
                "routing_accuracy": end_to_end["accuracy"],
                "speedup_factor": end_to_end["regular_routing"]["avg_time_us"]
                / end_to_end["encrypted_routing"]["avg_time_us"],
            }

        if "performance_scaling" in self.test_results:
            scaling = self.test_results["performance_scaling"]
            report["performance_summary"]["scaling"] = {
                "largest_dataset_size": max(scaling.keys()),
                "best_throughput": max(
                    result["throughput_queries_per_sec"] for result in scaling.values()
                ),
                "scaling_efficiency": (
                    "Good"
                    if all(result["avg_time_per_query_us"] < 1000 for result in scaling.values())
                    else "Needs improvement"
                ),
            }

        if "cache_effectiveness" in self.test_results:
            cache = self.test_results["cache_effectiveness"]
            report["performance_summary"]["cache"] = {
                "cache_speedup": cache["cache_speedup"],
                "cache_hit_rate": cache["cache_hit_rate"],
                "effectiveness": (
                    "Excellent"
                    if cache["cache_speedup"] > 50
                    else "Good" if cache["cache_speedup"] > 10 else "Needs improvement"
                ),
            }

        if "microsecond_performance" in self.test_results:
            microsecond = self.test_results["microsecond_performance"]
            report["performance_summary"]["microsecond"] = {
                "meets_requirements": microsecond["meets_microsecond_requirements"],
                "best_batch_size": microsecond["best_batch_size"],
                "avg_latency_us": microsecond["performance_results"][
                    microsecond["best_batch_size"]
                ]["avg_time_per_query_us"],
            }

        # Generate recommendations
        recommendations = []

        if "end_to_end_routing" in self.test_results:
            accuracy = self.test_results["end_to_end_routing"]["accuracy"]
            if accuracy < 0.9:
                recommendations.append(
                    "Consider improving routing accuracy through better QUBO formulation"
                )

        if "cache_effectiveness" in self.test_results:
            speedup = self.test_results["cache_effectiveness"]["cache_speedup"]
            if speedup < 20:
                recommendations.append("Optimize cache hit rate and reduce cache miss overhead")

        if "microsecond_performance" in self.test_results:
            meets_req = self.test_results["microsecond_performance"][
                "meets_microsecond_requirements"
            ]
            if not meets_req:
                recommendations.append("Optimize for better microsecond-level performance")

        report["recommendations"] = recommendations

        return report

    def save_test_results(self, filename: str = "integration_test_results.json"):
        """Save test results to JSON file.

        Args:
            filename: Output filename
        """
        report = self.generate_integration_report()

        with open(filename, "w") as f:
            json.dump(report, f, indent=2, default=str)

        logger.info(f"Test results saved to {filename}")

    async def run_full_integration_suite(self) -> Dict[str, Any]:
        """Run the complete integration test suite.

        Returns:
            Complete test results and report
        """
        logger.info("Running full integration test suite...")

        # Run all tests
        test_functions = [
            ("End-to-End Routing", lambda: self.test_end_to_end_encrypted_routing()),
            ("Performance Scaling", lambda: self.test_performance_scaling()),
            ("Cache Effectiveness", lambda: self.test_cache_effectiveness()),
            ("Microsecond Performance", lambda: self.test_microsecond_performance()),
        ]

        for test_name, test_func in test_functions:
            logger.info(f"Running test: {test_name}")
            try:
                if asyncio.iscoroutinefunction(test_func):
                    await test_func()
                else:
                    test_func()
                logger.info(f"✓ {test_name}: PASSED")
            except Exception as e:
                logger.error(f"✗ {test_name}: FAILED - {str(e)}")

        # Generate and save report
        report = self.generate_integration_report()
        self.save_test_results()

        # Print summary
        logger.info("\n" + "=" * 60)
        logger.info("INTEGRATION TEST SUMMARY")
        logger.info("=" * 60)
        logger.info(f"Total Tests: {report['test_summary']['total_tests']}")
        logger.info(f"Passed: {report['test_summary']['passed_tests']}")
        logger.info(f"Failed: {report['test_summary']['failed_tests']}")
        logger.info(
            f"Success Rate: {report['test_summary']['passed_tests']/report['test_summary']['total_tests']*100:.1f}%"
        )

        if report["recommendations"]:
            logger.info("\nRecommendations:")
            for rec in report["recommendations"]:
                logger.info(f"  - {rec}")

        logger.info("=" * 60)

        return report


def main():
    """Main integration test execution function."""
    logger.info("Starting encrypted quantum routing integration tests...")

    tester = IntegrationTestSuite()
    results = asyncio.run(tester.run_full_integration_suite())

    # Exit with appropriate code
    success = results["test_summary"]["passed_tests"] == results["test_summary"]["total_tests"]
    exit_code = 0 if success else 1

    logger.info(f"Integration test suite completed with exit code: {exit_code}")
    return exit_code


if __name__ == "__main__":
    exit_code = main()
    exit(exit_code)
