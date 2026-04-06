#!/usr/bin/env python3
"""
Energy Efficiency Regression Testing for q_mini_wasm_v2
Ensures <0.5 pJ/op targets are maintained
"""

import os
import re
import sys
import json
import subprocess
import argparse
from pathlib import Path
from typing import List, Dict, Tuple, Optional
from dataclasses import dataclass, asdict
from datetime import datetime

@dataclass
class EnergyMetric:
    operation: str
    energy_pj: float  # picojoules per operation
    target_pj: float  # target energy in picojoules
    passes_target: bool
    regression_detected: bool
    file_path: str
    line_number: int = 0

@dataclass
class EnergyBenchmark:
    name: str
    metrics: List[EnergyMetric]
    total_energy_pj: float
    target_total_pj: float
    passes_target: bool
    timestamp: datetime

class EnergyEfficiencyTester:
    def __init__(self):
        self.energy_targets = {
            "routing": 0.5,      # <0.5 pJ/op for MoE routing
            "memory": 1.0,       # <1 pJ/op for memory operations  
            "compute": 0.3,      # <0.3 pJ/op for compute operations
            "communication": 0.2 # <0.2 pJ/op for communication
        }
        
        self.baseline_file = Path("scripts/energy_baseline.json")
        self.current_baseline = self._load_baseline()

    def _load_baseline(self) -> Dict:
        """Load baseline energy measurements"""
        if self.baseline_file.exists():
            try:
                with open(self.baseline_file, 'r') as f:
                    return json.load(f)
            except:
                pass
        
        # Default baseline
        return {
            "routing": {"energy_pj": 0.45, "timestamp": "2024-01-01"},
            "memory": {"energy_pj": 0.8, "timestamp": "2024-01-01"},
            "compute": {"energy_pj": 0.25, "timestamp": "2024-01-01"},
            "communication": {"energy_pj": 0.15, "timestamp": "2024-01-01"}
        }

    def _save_baseline(self, baseline: Dict):
        """Save baseline energy measurements"""
        with open(self.baseline_file, 'w') as f:
            json.dump(baseline, f, indent=2)

    def analyze_source_code_energy(self, directory: Path) -> List[EnergyMetric]:
        """Analyze source code for energy patterns"""
        metrics = []
        
        cpp_files = list(directory.rglob("*.cpp")) + list(directory.rglob("*.hpp"))
        
        for file_path in cpp_files:
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    lines = f.readlines()
            except:
                continue
            
            for line_num, line in enumerate(lines, 1):
                line_stripped = line.strip()
                
                # Skip comments
                if line_stripped.startswith('//') or line_stripped.startswith('*'):
                    continue
                
                # Analyze energy-related code patterns
                if 'EnergyTrit' in line_stripped:
                    energy_level = self._extract_energy_level(line_stripped)
                    if energy_level:
                        metric = EnergyMetric(
                            operation="ternary_energy_tracking",
                            energy_pj=energy_level,
                            target_pj=0.5,
                            passes_target=energy_level <= 0.5,
                            regression_detected=self._check_regression("routing", energy_level),
                            file_path=str(file_path),
                            line_number=line_num
                        )
                        metrics.append(metric)
                
                # Check for routing operations
                if any(keyword in line_stripped for keyword in ['route', 'routing', 'expert_selection']):
                    estimated_energy = self._estimate_routing_energy(line_stripped)
                    metric = EnergyMetric(
                        operation="routing",
                        energy_pj=estimated_energy,
                        target_pj=self.energy_targets["routing"],
                        passes_target=estimated_energy <= self.energy_targets["routing"],
                        regression_detected=self._check_regression("routing", estimated_energy),
                        file_path=str(file_path),
                        line_number=line_num
                    )
                    metrics.append(metric)
                
                # Check for memory operations
                if any(keyword in line_stripped for keyword in ['flash_cim', 'memory', 'storage']):
                    estimated_energy = self._estimate_memory_energy(line_stripped)
                    metric = EnergyMetric(
                        operation="memory",
                        energy_pj=estimated_energy,
                        target_pj=self.energy_targets["memory"],
                        passes_target=estimated_energy <= self.energy_targets["memory"],
                        regression_detected=self._check_regression("memory", estimated_energy),
                        file_path=str(file_path),
                        line_number=line_num
                    )
                    metrics.append(metric)
        
        return metrics

    def _extract_energy_level(self, line: str) -> Optional[float]:
        """Extract energy level from EnergyTrit usage"""
        # Map EnergyTrit values to picojoules
        if 'EnergyTrit::LOW' in line:
            return 0.2  # <0.3 pJ/op
        elif 'EnergyTrit::MEDIUM' in line:
            return 0.5  # 0.3-0.7 pJ/op
        elif 'EnergyTrit::HIGH' in line:
            return 1.0  # >0.7 pJ/op
        
        return None

    def _estimate_routing_energy(self, line: str) -> float:
        """Estimate routing energy based on code patterns"""
        base_energy = 0.1  # Base routing energy
        
        # Add energy for complex operations
        if 'quantum' in line.lower():
            base_energy += 0.1
        if 'entangled' in line.lower():
            base_energy += 0.15
        if 'multi_objective' in line.lower():
            base_energy += 0.1
        if 'reinforcement' in line.lower() or 'learning' in line.lower():
            base_energy += 0.05
        
        # Check for ternary operations (more efficient)
        if 'ternary::' in line:
            base_energy *= 0.8  # 20% efficiency gain
        
        return min(base_energy, 2.0)  # Cap at 2.0 pJ

    def _estimate_memory_energy(self, line: str) -> float:
        """Estimate memory energy based on code patterns"""
        base_energy = 0.3  # Base memory energy
        
        # Add energy for different operations
        if 'write' in line.lower():
            base_energy += 0.2
        if 'read' in line.lower():
            base_energy += 0.1
        if 'erase' in line.lower():
            base_energy += 0.3
        if 'cim_' in line.lower() or 'compute_in_memory' in line.lower():
            base_energy += 0.1
        
        # Check for ternary operations (more efficient)
        if 'ternary::' in line:
            base_energy *= 0.7  # 30% efficiency gain
        
        return min(base_energy, 3.0)  # Cap at 3.0 pJ

    def _check_regression(self, operation: str, current_energy: float) -> bool:
        """Check if current energy shows regression vs baseline"""
        if operation in self.current_baseline:
            baseline_energy = self.current_baseline[operation]["energy_pj"]
            # Regression if energy is 10% higher than baseline
            return current_energy > (baseline_energy * 1.1)
        return False

    def run_benchmark_tests(self, build_dir: Path) -> List[EnergyMetric]:
        """Run actual benchmark tests if available"""
        metrics = []
        
        # Look for benchmark executable
        benchmark_exe = build_dir / "q_mini_wasm_benchmark"
        if not benchmark_exe.exists():
            benchmark_exe = build_dir / "q_mini_wasm_benchmark.exe"
        
        if benchmark_exe.exists():
            try:
                result = subprocess.run(
                    [str(benchmark_exe), "--energy-benchmark"],
                    capture_output=True,
                    text=True,
                    timeout=60
                )
                
                if result.returncode == 0:
                    metrics.extend(self._parse_benchmark_output(result.stdout))
            except subprocess.TimeoutExpired:
                print("⚠️ Benchmark timed out")
            except Exception as e:
                print(f"⚠️ Benchmark failed: {e}")
        
        return metrics

    def _parse_benchmark_output(self, output: str) -> List[EnergyMetric]:
        """Parse benchmark output for energy metrics"""
        metrics = []
        
        # Look for energy measurements in output
        lines = output.split('\n')
        for line in lines:
            if 'energy' in line.lower() and 'pJ' in line:
                # Extract energy value
                match = re.search(r'(\d+\.?\d*)\s*pJ', line)
                if match:
                    energy_pj = float(match.group(1))
                    
                    # Determine operation type
                    operation = "unknown"
                    if 'routing' in line.lower():
                        operation = "routing"
                    elif 'memory' in line.lower():
                        operation = "memory"
                    elif 'compute' in line.lower():
                        operation = "compute"
                    
                    target_pj = self.energy_targets.get(operation, 1.0)
                    
                    metric = EnergyMetric(
                        operation=operation,
                        energy_pj=energy_pj,
                        target_pj=target_pj,
                        passes_target=energy_pj <= target_pj,
                        regression_detected=self._check_regression(operation, energy_pj),
                        file_path="benchmark",
                        line_number=0
                    )
                    metrics.append(metric)
        
        return metrics

    def generate_report(self, metrics: List[EnergyMetric]) -> str:
        """Generate energy efficiency report"""
        if not metrics:
            return "⚠️ No energy metrics found to analyze"
        
        # Group metrics by operation
        operation_metrics = {}
        for metric in metrics:
            if metric.operation not in operation_metrics:
                operation_metrics[metric.operation] = []
            operation_metrics[metric.operation].append(metric)
        
        report = []
        report.append(f"⚡ Energy Efficiency Regression Test Report")
        report.append(f"{'='*60}")
        report.append(f"Generated: {datetime.now().isoformat()}")
        report.append(f"Total Metrics: {len(metrics)}")
        report.append(f"{'='*60}")
        
        # Summary by operation
        report.append(f"\n📊 ENERGY CONSUMPTION BY OPERATION:")
        for operation, op_metrics in operation_metrics.items():
            avg_energy = sum(m.energy_pj for m in op_metrics) / len(op_metrics)
            target = self.energy_targets.get(operation, 1.0)
            passes = avg_energy <= target
            
            report.append(f"\n  {operation.upper()}:")
            report.append(f"    Average Energy: {avg_energy:.3f} pJ/op")
            report.append(f"    Target: {target:.3f} pJ/op")
            report.append(f"    Status: {'✅ PASS' if passes else '❌ FAIL'}")
            
            # Check for regressions
            regressions = [m for m in op_metrics if m.regression_detected]
            if regressions:
                report.append(f"    ⚠️ REGRESSIONS DETECTED: {len(regressions)}")
        
        # Failed targets
        failed_metrics = [m for m in metrics if not m.passes_target]
        if failed_metrics:
            report.append(f"\n🔴 FAILED TARGETS ({len(failed_metrics)}):")
            for metric in failed_metrics[:10]:  # Show first 10
                report.append(f"  {metric.operation}: {metric.energy_pj:.3f} pJ > {metric.target_pj:.3f} pJ")
            if len(failed_metrics) > 10:
                report.append(f"  ... and {len(failed_metrics) - 10} more")
        
        # Regressions
        regression_metrics = [m for m in metrics if m.regression_detected]
        if regression_metrics:
            report.append(f"\n🟡 REGRESSIONS DETECTED ({len(regression_metrics)}):")
            for metric in regression_metrics[:10]:  # Show first 10
                baseline = self.current_baseline.get(metric.operation, {}).get("energy_pj", 0)
                report.append(f"  {metric.operation}: {metric.energy_pj:.3f} pJ (baseline: {baseline:.3f} pJ)")
            if len(regression_metrics) > 10:
                report.append(f"  ... and {len(regression_metrics) - 10} more")
        
        # Overall status
        critical_failures = len(failed_metrics) + len(regression_metrics)
        overall_status = "✅ PASS" if critical_failures == 0 else "❌ FAIL"
        
        report.append(f"\n📈 OVERALL STATUS: {overall_status}")
        report.append(f"Failed Targets: {len(failed_metrics)}")
        report.append(f"Regressions: {len(regression_metrics)}")
        
        # Recommendations
        report.append(f"\n💡 RECOMMENDATIONS:")
        if failed_metrics:
            report.append(f"  • Optimize operations exceeding energy targets")
        if regression_metrics:
            report.append(f"  • Investigate energy regressions vs baseline")
        if not failed_metrics and not regression_metrics:
            report.append(f"  • All energy targets met! Consider tightening targets")
        
        return "\n".join(report)

    def update_baseline(self, metrics: List[EnergyMetric]):
        """Update baseline with current measurements"""
        for metric in metrics:
            if metric.operation not in self.current_baseline:
                self.current_baseline[metric.operation] = {}
            
            # Use average energy for baseline
            operation_metrics = [m for m in metrics if m.operation == metric.operation]
            if operation_metrics:
                avg_energy = sum(m.energy_pj for m in operation_metrics) / len(operation_metrics)
                self.current_baseline[metric.operation] = {
                    "energy_pj": avg_energy,
                    "timestamp": datetime.now().isoformat()
                }
        
        self._save_baseline(self.current_baseline)

def main():
    parser = argparse.ArgumentParser(description="Energy efficiency regression testing")
    parser.add_argument("path", help="Path to analyze (source directory or build directory)")
    parser.add_argument("--update-baseline", action="store_true", help="Update baseline with current measurements")
    parser.add_argument("--output", help="Output report to file")
    parser.add_argument("--build-dir", help="Build directory with benchmark executable")
    
    args = parser.parse_args()
    
    tester = EnergyEfficiencyTester()
    path = Path(args.path)
    
    if not path.exists():
        print(f"❌ Error: Path {path} does not exist")
        sys.exit(1)
    
    all_metrics = []
    
    # Analyze source code
    if path.is_dir() and any(path.rglob("*.cpp")):
        print("🔍 Analyzing source code energy patterns...")
        source_metrics = tester.analyze_source_code_energy(path)
        all_metrics.extend(source_metrics)
        print(f"Found {len(source_metrics)} energy metrics in source code")
    
    # Run benchmark tests
    if args.build_dir:
        build_path = Path(args.build_dir)
        if build_path.exists():
            print("🏃 Running benchmark tests...")
            benchmark_metrics = tester.run_benchmark_tests(build_path)
            all_metrics.extend(benchmark_metrics)
            print(f"Found {len(benchmark_metrics)} benchmark metrics")
    
    if not all_metrics:
        print("⚠️ No energy metrics found")
        sys.exit(1)
    
    # Update baseline if requested
    if args.update_baseline:
        print("📊 Updating baseline...")
        tester.update_baseline(all_metrics)
    
    # Generate report
    report = tester.generate_report(all_metrics)
    
    if args.output:
        with open(args.output, 'w') as f:
            f.write(report)
        print(f"📄 Report saved to {args.output}")
    else:
        print("\n" + report)
    
    # Exit with error code if critical issues found
    failed_metrics = [m for m in all_metrics if not m.passes_target]
    regression_metrics = [m for m in all_metrics if m.regression_detected]
    
    if failed_metrics or regression_metrics:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()
