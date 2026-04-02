#!/usr/bin/env python3
"""
Script to track rejected improvements in the Kanban system.
This script helps categorize improvements and track rejection reasons.
"""

import json
import os
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Any

class RejectedImprovementTracker:
    """Tracks rejected improvements with reasons and feasibility analysis."""
    
    def __init__(self, config_dir: str = "config"):
        self.config_dir = Path(config_dir)
        self.rejected_file = self.config_dir / "rejected-improvements.json"
        self.load_rejected_improvements()
    
    def load_rejected_improvements(self):
        """Load existing rejected improvements."""
        if self.rejected_file.exists():
            with open(self.rejected_file, 'r', encoding='utf-8') as f:
                self.rejected_improvements = json.load(f)
        else:
            self.rejected_improvements = {
                "project": "q_mini_wasm_v2",
                "version": "1.0.0",
                "rejected_improvements": [],
                "rejection_reasons": {
                    "high_complexity": "Implementation complexity exceeds available resources",
                    "misalignment": "Does not align with project goals or architecture",
                    "maintenance_burden": "Would create excessive maintenance overhead",
                    "low_value": "Potential value does not justify implementation cost",
                    "security_risk": "Introduces security vulnerabilities or risks",
                    "stability_risk": "Could introduce system instability"
                },
                "feasibility_criteria": {
                    "technical_complexity": {
                        "low": "Can be implemented with existing expertise",
                        "moderate": "Requires some research or new skills",
                        "high": "Requires significant expertise or external help",
                        "very_high": "Beyond current team capabilities"
                    },
                    "resource_requirements": {
                        "minimal": "Less than 1 week of development",
                        "moderate": "1-4 weeks of development",
                        "significant": "1-3 months of development",
                        "extensive": "More than 3 months of development"
                    },
                    "alignment_score": {
                        "high": "Directly supports core project goals",
                        "medium": "Indirectly supports project goals",
                        "low": "Minimal alignment with project goals",
                        "none": "No alignment with project goals"
                    }
                }
            }
    
    def save_rejected_improvements(self):
        """Save rejected improvements to file."""
        with open(self.rejected_file, 'w', encoding='utf-8') as f:
            json.dump(self.rejected_improvements, f, indent=2, ensure_ascii=False)
    
    def add_rejected_improvement(
        self,
        improvement_name: str,
        module: str,
        description: str,
        rejection_reasons: List[str],
        complexity: str = "high",
        resource_requirements: str = "significant",
        alignment_score: str = "low",
        potential_value: str = "low",
        notes: str = ""
    ):
        """Add a new rejected improvement."""
        improvement = {
            "id": f"rejected_{len(self.rejected_improvements['rejected_improvements']) + 1:03d}",
            "name": improvement_name,
            "module": module,
            "description": description,
            "rejected_at": datetime.now().isoformat(),
            "rejection_reasons": rejection_reasons,
            "feasibility_assessment": {
                "technical_complexity": complexity,
                "resource_requirements": resource_requirements,
                "alignment_score": alignment_score,
                "potential_value": potential_value
            },
            "notes": notes,
            "status": "rejected",
            "review_date": None,
            "reconsideration_criteria": []
        }
        
        self.rejected_improvements["rejected_improvements"].append(improvement)
        self.save_rejected_improvements()
        return improvement
    
    def get_rejected_improvements_by_module(self, module: str) -> List[Dict[str, Any]]:
        """Get rejected improvements for a specific module."""
        return [
            imp for imp in self.rejected_improvements["rejected_improvements"]
            if imp["module"] == module
        ]
    
    def get_rejected_improvements_by_reason(self, reason: str) -> List[Dict[str, Any]]:
        """Get rejected improvements with a specific rejection reason."""
        return [
            imp for imp in self.rejected_improvements["rejected_improvements"]
            if reason in imp["rejection_reasons"]
        ]
    
    def schedule_review(self, improvement_id: str, review_date: str, criteria: List[str]):
        """Schedule a review for a rejected improvement."""
        for imp in self.rejected_improvements["rejected_improvements"]:
            if imp["id"] == improvement_id:
                imp["review_date"] = review_date
                imp["reconsideration_criteria"] = criteria
                imp["status"] = "pending_review"
                self.save_rejected_improvements()
                return True
        return False
    
    def generate_report(self) -> str:
        """Generate a report of rejected improvements."""
        report = []
        report.append("# Rejected Improvements Report")
        report.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        report.append(f"Total rejected improvements: {len(self.rejected_improvements['rejected_improvements'])}")
        report.append("")
        
        # Group by module
        modules = set(imp["module"] for imp in self.rejected_improvements["rejected_improvements"])
        for module in sorted(modules):
            report.append(f"## Module: {module}")
            improvements = self.get_rejected_improvements_by_module(module)
            for imp in improvements:
                report.append(f"### {imp['name']} ({imp['id']})")
                report.append(f"**Rejected at:** {imp['rejected_at']}")
                report.append(f"**Description:** {imp['description']}")
                report.append(f"**Rejection reasons:** {', '.join(imp['rejection_reasons'])}")
                report.append(f"**Complexity:** {imp['feasibility_assessment']['technical_complexity']}")
                report.append(f"**Resource requirements:** {imp['feasibility_assessment']['resource_requirements']}")
                report.append(f"**Alignment:** {imp['feasibility_assessment']['alignment_score']}")
                report.append(f"**Potential value:** {imp['feasibility_assessment']['potential_value']}")
                if imp.get("notes"):
                    report.append(f"**Notes:** {imp['notes']}")
                if imp.get("review_date"):
                    report.append(f"**Scheduled review:** {imp['review_date']}")
                report.append("")
        
        # Summary by rejection reason
        report.append("## Summary by Rejection Reason")
        for reason, description in self.rejected_improvements["rejection_reasons"].items():
            count = len(self.get_rejected_improvements_by_reason(reason))
            if count > 0:
                report.append(f"- **{reason}**: {count} improvements")
        
        return "\n".join(report)


def main():
    """Main function to demonstrate usage."""
    tracker = RejectedImprovementTracker()
    
    # Add some example rejected improvements
    tracker.add_rejected_improvement(
        improvement_name="Full Quantum Error Correction",
        module="quantum_core",
        description="Implement full quantum error correction codes for qutrit systems",
        rejection_reasons=["high_complexity", "misalignment"],
        complexity="very_high",
        resource_requirements="extensive",
        alignment_score="low",
        potential_value="medium",
        notes="Beyond current scope of ternary computing research"
    )
    
    tracker.add_rejected_improvement(
        improvement_name="Java JNI Bindings",
        module="dll_bridge",
        description="Add Java Native Interface bindings for C++ quantum engine",
        rejection_reasons=["high_complexity", "maintenance_burden"],
        complexity="very_high",
        resource_requirements="significant",
        alignment_score="low",
        potential_value="low",
        notes="Very high maintenance burden for limited user base"
    )
    
    tracker.add_rejected_improvement(
        improvement_name="GraphQL API",
        module="go_runtime",
        description="Add GraphQL API endpoint alongside existing gRPC/MCP",
        rejection_reasons=["misalignment", "low_value"],
        complexity="high",
        resource_requirements="moderate",
        alignment_score="none",
        potential_value="low",
        notes="Duplicate functionality, misaligned with MCP/gRPC architecture"
    )
    
    # Generate and print report
    report = tracker.generate_report()
    print(report)
    
    # Save report to file
    with open("rejected-improvements-report.md", "w", encoding="utf-8") as f:
        f.write(report)
    
    print(f"\nReport saved to rejected-improvements-report.md")
    print(f"Rejected improvements data saved to {tracker.rejected_file}")


if __name__ == "__main__":
    main()
