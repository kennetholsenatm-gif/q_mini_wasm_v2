#!/usr/bin/env python3
"""
Integration script for Gemini Agent Improver with Self-Learning MCP

Connects the Gemini agent to the existing self-learning MCP server
to enhance pattern analysis and tool suggestion capabilities.
"""

import os
import sys
import json
from pathlib import Path
from datetime import datetime

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

from agent import create_agent, run_analysis


class SelfLearningIntegrator:
    """Integrates Gemini agent with self-learning MCP server."""
    
    def __init__(self):
        self.agent = create_agent()
        self.mcp_dir = Path(__file__).parent.parent.parent.parent / ".mcp-servers"
        self.config_dir = Path(__file__).parent.parent.parent.parent / "config"
        self.output_dir = Path(__file__).parent / "output"
        self.output_dir.mkdir(exist_ok=True)
    
    def analyze_all_servers(self):
        """Analyze all MCP servers for improvements."""
        print("Analyzing all MCP servers...")
        
        mcp_files = list(self.mcp_dir.glob("*.json"))
        results = {}
        
        for mcp_file in mcp_files:
            server_name = mcp_file.stem
            print(f"\nAnalyzing {server_name}...")
            
            try:
                analysis = run_analysis(server_name, "all")
                results[server_name] = {
                    "timestamp": datetime.now().isoformat(),
                    "analysis": analysis
                }
            except Exception as e:
                print(f"  Error analyzing {server_name}: {e}")
                results[server_name] = {
                    "timestamp": datetime.now().isoformat(),
                    "error": str(e)
                }
        
        # Save results
        output_file = self.output_dir / f"analysis_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        with open(output_file, 'w') as f:
            json.dump(results, f, indent=2)
        
        print(f"\nAnalysis saved to: {output_file}")
        return results
    
    def enhance_self_learning(self):
        """Enhance the self-learning MCP server with Gemini intelligence."""
        print("Enhancing self-learning MCP server...")
        
        # Analyze the self-learning server
        analysis = run_analysis("qminiwasm-self-learning", "all")
        
        # Extract suggestions for enhancement
        enhancements = {
            "timestamp": datetime.now().isoformat(),
            "server": "qminiwasm-self-learning",
            "enhancements": []
        }
        
        # Process analysis results
        for step in analysis:
            if step.get("tool_calls"):
                for tool_call in step["tool_calls"]:
                    if tool_call.get("name") == "analyze_mcp_server":
                        result = json.loads(tool_call.get("result", "{}"))
                        if "suggestions" in result:
                            for suggestion in result["suggestions"]:
                                enhancements["enhancements"].append({
                                    "type": suggestion.get("type"),
                                    "suggestion": suggestion.get("suggestion"),
                                    "priority": suggestion.get("priority"),
                                    "source": "gemini_analysis"
                                })
        
        # Save enhancements
        output_file = self.output_dir / "self_learning_enhancements.json"
        with open(output_file, 'w') as f:
            json.dump(enhancements, f, indent=2)
        
        print(f"Enhancements saved to: {output_file}")
        return enhancements
    
    def generate_improvement_report(self):
        """Generate a comprehensive improvement report."""
        print("Generating improvement report...")
        
        report = {
            "timestamp": datetime.now().isoformat(),
            "summary": {
                "total_servers": 0,
                "analyzed_servers": 0,
                "total_suggestions": 0,
                "high_priority_suggestions": 0
            },
            "servers": {},
            "recommendations": []
        }
        
        # Analyze each server
        mcp_files = list(self.mcp_dir.glob("*.json"))
        report["summary"]["total_servers"] = len(mcp_files)
        
        for mcp_file in mcp_files:
            server_name = mcp_file.stem
            print(f"  Analyzing {server_name}...")
            
            try:
                analysis = run_analysis(server_name, "all")
                report["servers"][server_name] = analysis
                report["summary"]["analyzed_servers"] += 1
                
                # Count suggestions
                for step in analysis:
                    if step.get("tool_calls"):
                        for tool_call in step["tool_calls"]:
                            if tool_call.get("name") == "analyze_mcp_server":
                                result = json.loads(tool_call.get("result", "{}"))
                                suggestions = result.get("suggestions", [])
                                report["summary"]["total_suggestions"] += len(suggestions)
                                
                                for suggestion in suggestions:
                                    if suggestion.get("priority") == "high":
                                        report["summary"]["high_priority_suggestions"] += 1
                
            except Exception as e:
                print(f"    Error: {e}")
        
        # Generate recommendations
        if report["summary"]["high_priority_suggestions"] > 0:
            report["recommendations"].append({
                "priority": "high",
                "action": "Address high-priority suggestions immediately",
                "count": report["summary"]["high_priority_suggestions"]
            })
        
        if report["summary"]["total_suggestions"] > 10:
            report["recommendations"].append({
                "priority": "medium",
                "action": "Consider implementing automated improvement pipeline",
                "reason": "High volume of suggestions indicates systematic improvements needed"
            })
        
        # Save report
        output_file = self.output_dir / f"improvement_report_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        with open(output_file, 'w') as f:
            json.dump(report, f, indent=2)
        
        print(f"Report saved to: {output_file}")
        return report


def main():
    """Main integration script."""
    print("=" * 60)
    print("Gemini Agent Improver - Self-Learning Integration")
    print("=" * 60)
    
    integrator = SelfLearningIntegrator()
    
    if len(sys.argv) < 2:
        print("Usage: integrate.py <command>")
        print("Commands:")
        print("  analyze     - Analyze all MCP servers")
        print("  enhance     - Enhance self-learning MCP server")
        print("  report      - Generate improvement report")
        print("  all         - Run all integration tasks")
        sys.exit(1)
    
    command = sys.argv[1]
    
    try:
        if command == "analyze":
            integrator.analyze_all_servers()
        elif command == "enhance":
            integrator.enhance_self_learning()
        elif command == "report":
            integrator.generate_improvement_report()
        elif command == "all":
            print("\n1. Analyzing all servers...")
            integrator.analyze_all_servers()
            print("\n2. Enhancing self-learning...")
            integrator.enhance_self_learning()
            print("\n3. Generating report...")
            integrator.generate_improvement_report()
        else:
            print(f"Unknown command: {command}")
            sys.exit(1)
        
        print("\n✅ Integration completed successfully!")
        
    except Exception as e:
        print(f"\n❌ Integration failed: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
