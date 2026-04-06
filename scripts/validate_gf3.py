#!/usr/bin/env python3
"""
GF(3) Validation Tooling for q_mini_wasm_v2
Ensures Gottesman-Knill simulability and ternary purity
"""

import os
import re
import sys
import argparse
from pathlib import Path
from typing import List, Dict, Tuple, Set
from dataclasses import dataclass
from enum import Enum

class ValidationLevel(Enum):
    CRITICAL = "CRITICAL"
    WARNING = "WARNING"
    INFO = "INFO"

@dataclass
class ValidationIssue:
    level: ValidationLevel
    file_path: str
    line_number: int
    issue_type: str
    description: str
    code_snippet: str

class GF3Validator:
    def __init__(self):
        self.issues: List[ValidationIssue] = []
        self.binary_patterns = [
            r'\bdouble\b',
            r'\bfloat\b',
            r'\bf32\b',
            r'\bf64\b',
            r'\bstd::vector<double\b',
            r'\bstd::vector<float\b',
            r'\bstd::mt19937\b',
            r'\bstd::normal_distribution\b',
            r'\bstd::uniform_real_distribution\b',
            r'\brand\(\)',
            r'\bsrand\(\)',
            r'\btime\(NULL\)',
            r'\bstd::random_device\b',
        ]
        
        self.forbidden_functions = [
            r'\bsin\(',
            r'\bcos\(',
            r'\btan\(',
            r'\bexp\(',
            r'\blog\(',
            r'\bsqrt\(',
            r'\bpow\(',
            r'\bfabs\(',
            r'\bstd::abs\(',
            r'\bstd::fabs\(',
        ]
        
        self.required_gf3_patterns = [
            r'ternary::Trit',
            r'ternary::EnergyTrit',
            r'ternary::ProbTrit',
            r'ternary::trit_ops::',
            r'ternary::energy_ops::',
            r'ternary::prob_ops::',
            r'to_gf3\(',
            r'from_gf3\(',
        ]
        
        self.energy_patterns = [
            r'EnergyTrit',
            r'energy_level',
            r'ternary::energy_to_trit',
            r'ternary::trit_to_energy',
        ]
        
        self.probability_patterns = [
            r'ProbTrit',
            r'ternary::prob_to_trit',
            r'ternary::trit_to_prob',
        ]

    def validate_file(self, file_path: Path) -> List[ValidationIssue]:
        """Validate a single file for GF(3) compliance"""
        issues = []
        
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                lines = f.readlines()
        except Exception as e:
            return [ValidationIssue(
                ValidationLevel.CRITICAL,
                str(file_path),
                0,
                "FILE_READ_ERROR",
                f"Could not read file: {e}",
                ""
            )]
        
        for line_num, line in enumerate(lines, 1):
            line_stripped = line.strip()
            
            # Skip comments and empty lines
            if line_stripped.startswith('//') or line_stripped.startswith('*') or not line_stripped:
                continue
            
            # Check for binary pollution
            for pattern in self.binary_patterns:
                matches = re.finditer(pattern, line, re.IGNORECASE)
                for match in matches:
                    # Allow certain patterns in comments or specific contexts
                    if self._is_allowed_context(line, match.group()):
                        continue
                    
                    issues.append(ValidationIssue(
                        ValidationLevel.CRITICAL,
                        str(file_path),
                        line_num,
                        "BINARY_POLLUTION",
                        f"Found binary type: {match.group()}",
                        line_stripped
                    ))
            
            # Check for forbidden mathematical functions
            for pattern in self.forbidden_functions:
                matches = re.finditer(pattern, line, re.IGNORECASE)
                for match in matches:
                    issues.append(ValidationIssue(
                        ValidationLevel.WARNING,
                        str(file_path),
                        line_num,
                        "FORBIDDEN_FUNCTION",
                        f"Floating-point function detected: {match.group()}",
                        line_stripped
                    ))
            
            # Check for magic numbers (potential binary constants)
            magic_numbers = re.finditer(r'\b\d+\.\d+\b', line)
            for match in magic_numbers:
                issues.append(ValidationIssue(
                    ValidationLevel.WARNING,
                    str(file_path),
                    line_num,
                    "MAGIC_NUMBER",
                    f"Floating-point literal: {match.group()}",
                    line_stripped
                ))
        
        return issues

    def _is_allowed_context(self, line: str, match: str) -> bool:
        """Check if binary pattern is allowed in this context"""
        # Allow in comments
        if '//' in line and line.index('//') < line.index(match):
            return True
        
        # Allow in string literals
        if '"' in line and '"' in line[line.index(match):]:
            return True
        
        # Allow specific double usage in ternary conversion functions
        if 'ternary::energy_to_trit' in line or 'ternary::trit_to_energy' in line:
            return True
        
        return False

    def validate_directory(self, directory: Path, recursive: bool = True) -> List[ValidationIssue]:
        """Validate all C++ files in a directory"""
        all_issues = []
        
        if recursive:
            cpp_files = list(directory.rglob("*.cpp")) + list(directory.rglob("*.hpp"))
        else:
            cpp_files = list(directory.glob("*.cpp")) + list(directory.glob("*.hpp"))
        
        for file_path in cpp_files:
            file_issues = self.validate_file(file_path)
            all_issues.extend(file_issues)
        
        return all_issues

    def validate_gf3_completeness(self, directory: Path) -> List[ValidationIssue]:
        """Check that files use required GF(3) patterns"""
        issues = []
        
        cpp_files = list(directory.rglob("*.cpp")) + list(directory.rglob("*.hpp"))
        
        for file_path in cpp_files:
            if 'test' in file_path.name.lower():
                continue  # Skip test files
                
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()
            except:
                continue
            
            # Check if file should use GF(3) patterns (based on content)
            if any(keyword in content for keyword in ['expert', 'routing', 'energy', 'probability', 'learning']):
                has_gf3 = any(re.search(pattern, content) for pattern in self.required_gf3_patterns)
                
                if not has_gf3:
                    issues.append(ValidationIssue(
                        ValidationLevel.WARNING,
                        str(file_path),
                        0,
                        "MISSING_GF3_PATTERNS",
                        "File should use GF(3) patterns but none found",
                        ""
                    ))
        
        return issues

    def validate_energy_tracking(self, directory: Path) -> List[ValidationIssue]:
        """Validate energy tracking uses ternary systems"""
        issues = []
        
        cpp_files = list(directory.rglob("*.cpp")) + list(directory.rglob("*.hpp"))
        
        for file_path in cpp_files:
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    lines = f.readlines()
            except:
                continue
            
            for line_num, line in enumerate(lines, 1):
                line_stripped = line.strip()
                
                # Check for energy-related code
                if any(keyword in line_stripped for keyword in ['energy', 'power', 'consumption']):
                    # Should use ternary energy patterns
                    has_ternary_energy = any(pattern in line_stripped for pattern in self.energy_patterns)
                    has_binary_energy = any(pattern in line_stripped for pattern in ['double', 'float'])
                    
                    if has_binary_energy and not has_ternary_energy:
                        issues.append(ValidationIssue(
                            ValidationLevel.CRITICAL,
                            str(file_path),
                            line_num,
                            "BINARY_ENERGY_TRACKING",
                            "Energy tracking should use ternary EnergyTrit",
                            line_stripped
                        ))
        
        return issues

    def generate_report(self, issues: List[ValidationIssue]) -> str:
        """Generate a validation report"""
        if not issues:
            return "✅ All files passed GF(3) validation! No binary pollution detected."
        
        # Group issues by level
        critical_issues = [i for i in issues if i.level == ValidationLevel.CRITICAL]
        warning_issues = [i for i in issues if i.level == ValidationLevel.WARNING]
        info_issues = [i for i in issues if i.level == ValidationLevel.INFO]
        
        report = []
        report.append(f"🚨 GF(3) Validation Report")
        report.append(f"{'='*50}")
        report.append(f"Critical Issues: {len(critical_issues)}")
        report.append(f"Warnings: {len(warning_issues)}")
        report.append(f"Info: {len(info_issues)}")
        report.append(f"{'='*50}")
        
        if critical_issues:
            report.append(f"\n🔴 CRITICAL ISSUES (Must Fix):")
            for issue in critical_issues:
                report.append(f"  {issue.file_path}:{issue.line_number}")
                report.append(f"    {issue.issue_type}: {issue.description}")
                if issue.code_snippet:
                    report.append(f"    Code: {issue.code_snippet}")
                report.append("")
        
        if warning_issues:
            report.append(f"\n🟡 WARNINGS (Should Fix):")
            for issue in warning_issues:
                report.append(f"  {issue.file_path}:{issue.line_number}")
                report.append(f"    {issue.issue_type}: {issue.description}")
                if issue.code_snippet:
                    report.append(f"    Code: {issue.code_snippet}")
                report.append("")
        
        if info_issues:
            report.append(f"\n🔵 INFO:")
            for issue in info_issues:
                report.append(f"  {issue.file_path}:{issue.line_number}")
                report.append(f"    {issue.issue_type}: {issue.description}")
        
        # Summary
        report.append(f"\n📊 SUMMARY:")
        report.append(f"Total Issues: {len(issues)}")
        report.append(f"GF(3) Compliance: {'✅ PASS' if len(critical_issues) == 0 else '❌ FAIL'}")
        
        return "\n".join(report)

def main():
    parser = argparse.ArgumentParser(description="Validate GF(3) compliance in q_mini_wasm_v2")
    parser.add_argument("path", help="Path to validate (file or directory)")
    parser.add_argument("--recursive", action="store_true", default=True, help="Recursive directory scan")
    parser.add_argument("--output", help="Output report to file")
    parser.add_argument("--completeness", action="store_true", help="Check GF(3) pattern completeness")
    parser.add_argument("--energy", action="store_true", help="Validate energy tracking")
    
    args = parser.parse_args()
    
    validator = GF3Validator()
    path = Path(args.path)
    
    if not path.exists():
        print(f"❌ Error: Path {path} does not exist")
        sys.exit(1)
    
    if path.is_file():
        issues = validator.validate_file(path)
    else:
        issues = validator.validate_directory(path, args.recursive)
        
        if args.completeness:
            completeness_issues = validator.validate_gf3_completeness(path)
            issues.extend(completeness_issues)
        
        if args.energy:
            energy_issues = validator.validate_energy_tracking(path)
            issues.extend(energy_issues)
    
    report = validator.generate_report(issues)
    
    if args.output:
        with open(args.output, 'w') as f:
            f.write(report)
        print(f"📄 Report saved to {args.output}")
    else:
        print(report)
    
    # Exit with error code if critical issues found
    critical_count = len([i for i in issues if i.level == ValidationLevel.CRITICAL])
    sys.exit(1 if critical_count > 0 else 0)

if __name__ == "__main__":
    main()
