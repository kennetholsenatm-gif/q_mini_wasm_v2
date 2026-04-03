#!/usr/bin/env python3
"""
CLI for Auto-Improvement Agent System

This script provides a command-line interface for managing the auto-improvement cycle.
"""

import asyncio
import json
import sys
from pathlib import Path
from typing import Optional

import click
from rich.console import Console
from rich.table import Table
from rich.panel import Panel

from .improvement_cycle import ImprovementCycle
from .llm.gemini_service import GeminiService, GeminiConfig

console = Console()


@click.group()
def cli():
    """Auto-Improvement Agent System CLI"""
    pass


@cli.command()
@click.option('--config', '-c', help='Path to configuration file')
@click.option('--verbose', '-v', is_flag=True, help='Enable verbose output')
def status(config: Optional[str], verbose: bool):
    """Show current system status"""
    try:
        cycle = ImprovementCycle(config_path=config)
        status_data = cycle.get_status()
        
        # Display status
        console.print(Panel.fit(
            "[bold blue]Auto-Improvement System Status[/bold blue]",
            border_style="blue"
        ))
        
        table = Table(title="System Status")
        table.add_column("Property", style="cyan")
        table.add_column("Value", style="green")
        
        table.add_row("Running", str(status_data["running"]))
        table.add_row("Total Cycles", str(status_data["total_cycles"]))
        table.add_row("Successful Cycles", str(status_data["successful_cycles"]))
        table.add_row("Agents Initialized", str(status_data["agents_initialized"]))
        
        if verbose:
            table.add_row("LLM Rate Limits", json.dumps(
                status_data["llm_rate_limits"], indent=2
            ))
        
        console.print(table)
        
    except Exception as e:
        console.print(f"[red]Error: {e}[/red]")
        sys.exit(1)


@cli.command()
@click.option('--config', '-c', help='Path to configuration file')
@click.option('--async', '-a', is_flag=True, help='Run asynchronously')
def run_cycle(config: Optional[str], async_: bool):
    """Run a single improvement cycle"""
    async def _run():
        try:
            console.print(Panel.fit(
                "[bold green]Starting Improvement Cycle[/bold green]",
                border_style="green"
            ))
            
            cycle = ImprovementCycle(config_path=config)
            await cycle.initialize()
            
            console.print("[yellow]Running cycle...[/yellow]")
            result = await cycle.run_cycle()
            
            # Display results
            console.print(Panel.fit(
                f"[bold]Cycle Completed: {result.cycle_id}[/bold]",
                border_style="green" if result.phase.value == "completed" else "red"
            ))
            
            table = Table(title="Cycle Results")
            table.add_column("Metric", style="cyan")
            table.add_column("Value", style="green")
            
            table.add_row("Phase", result.phase.value)
            table.add_row("Patterns Identified", str(result.patterns_identified))
            table.add_row("Improvements Planned", str(result.improvements_planned))
            table.add_row("Improvements Implemented", str(result.improvements_implemented))
            table.add_row("Improvements Validated", str(result.improvements_validated))
            table.add_row("Success Rate", f"{result.success_rate:.1%}")
            
            if result.errors:
                table.add_row("Errors", "\n".join(result.errors))
            
            console.print(table)
            
            await cycle.shutdown()
            
        except Exception as e:
            console.print(f"[red]Error: {e}[/red]")
            sys.exit(1)
    
    if async_:
        asyncio.run(_run())
    else:
        # Run synchronously
        asyncio.run(_run())


@cli.command()
@click.option('--config', '-c', help='Path to configuration file')
def continuous(config: Optional[str]):
    """Run continuous improvement cycles"""
    async def _run_continuous():
        try:
            console.print(Panel.fit(
                "[bold yellow]Starting Continuous Improvement[/bold yellow]",
                border_style="yellow"
            ))
            
            cycle = ImprovementCycle(config_path=config)
            await cycle.initialize()
            
            cycle_count = 0
            while True:
                cycle_count += 1
                console.print(f"\n[cyan]Running cycle {cycle_count}...[/cyan]")
                
                try:
                    result = await cycle.run_cycle()
                    
                    if result.phase.value == "completed":
                        console.print(f"[green]Cycle {cycle_count} completed successfully[/green]")
                    else:
                        console.print(f"[red]Cycle {cycle_count} failed: {result.phase.value}[/red]")
                    
                    # Wait between cycles (e.g., 1 hour)
                    console.print("[yellow]Waiting 1 hour before next cycle...[/yellow]")
                    await asyncio.sleep(3600)
                    
                except KeyboardInterrupt:
                    console.print("\n[yellow]Stopping continuous improvement...[/yellow]")
                    break
                except Exception as e:
                    console.print(f"[red]Cycle {cycle_count} error: {e}[/red]")
                    await asyncio.sleep(60)  # Wait 1 minute on error
            
            await cycle.shutdown()
            console.print("[green]Continuous improvement stopped[/green]")
            
        except Exception as e:
            console.print(f"[red]Error: {e}[/red]")
            sys.exit(1)
    
    asyncio.run(_run_continuous())


@cli.command()
@click.option('--prompt', '-p', required=True, help='Test prompt')
@click.option('--config', '-c', help='Path to configuration file')
def test_llm(prompt: str, config: Optional[str]):
    """Test Gemini LLM integration"""
    async def _test():
        try:
            console.print(Panel.fit(
                "[bold blue]Testing Gemini LLM[/bold blue]",
                border_style="blue"
            ))
            
            # Load config
            config_path = config or "agents/config.json"
            config_data = {}
            if Path(config_path).exists():
                config_data = json.loads(Path(config_path).read_text())
            
            gemini_config = GeminiConfig(**config_data.get("gemini", {}))
            llm = GeminiService(config=gemini_config)
            
            console.print("[yellow]Initializing LLM...[/yellow]")
            await llm.initialize()
            
            console.print(f"[yellow]Sending prompt: {prompt[:50]}...[/yellow]")
            response = await llm.complete(prompt)
            
            console.print(Panel(
                response,
                title="LLM Response",
                border_style="green"
            ))
            
            # Show rate limit status
            rate_status = llm.get_rate_limit_status()
            console.print(f"\n[cyan]Rate Limit Status:[/cyan]")
            console.print(f"  RPM: {rate_status['rpm_used']}/{rate_status['rpm_limit']}")
            console.print(f"  TPM: {rate_status['tpm_used']}/{rate_status['tpm_limit']}")
            console.print(f"  Cache Size: {rate_status['cache_size']}")
            
            await llm.shutdown()
            
        except Exception as e:
            console.print(f"[red]Error: {e}[/red]")
            sys.exit(1)
    
    asyncio.run(_test())


@cli.command()
@click.option('--action', '-a', default='scan', 
              type=click.Choice(['scan', 'fix', 'report']),
              help='Action to perform')
@click.option('--card-id', '-c', help='Specific card ID to fix')
@click.option('--config', '-f', help='Path to Kanban configuration file')
@click.option('--verbose', '-v', is_flag=True, help='Enable verbose output')
def kanban_review(action: str, card_id: Optional[str], config: Optional[str], verbose: bool):
    """Fix Kanban boards stuck in Review"""
    async def _run():
        try:
            from .kanban_review_fix_agent import KanbanReviewFixAgent, AgentConfig
            
            console.print(Panel.fit(
                f"[bold blue]Kanban Review Fix Agent - {action.upper()}[/bold blue]",
                border_style="blue"
            ))
            
            # Create agent config
            agent_config = AgentConfig(
                name="KanbanReviewFixAgent",
                description="Monitors and fixes Kanban boards stuck in Review",
                system_prompt="You are an agent that monitors Kanban boards and fixes stuck review cards.",
                tools=["scan_review", "fix_card", "generate_report"]
            )
            
            # Create and initialize agent
            agent = KanbanReviewFixAgent(
                agent_config,
                kanban_config_path=config or "config/kanban-config.json"
            )
            await agent.initialize()
            
            # Execute action
            task = {"action": action}
            if card_id:
                task["card_id"] = card_id
            
            console.print(f"[yellow]Executing {action}...[/yellow]")
            result = await agent.execute_task(task)
            
            # Display results
            if result.success:
                console.print(Panel.fit(
                    "[bold green]Action Completed Successfully[/bold green]",
                    border_style="green"
                ))
                
                table = Table(title="Results")
                table.add_column("Metric", style="cyan")
                table.add_column("Value", style="green")
                
                if action == "scan":
                    table.add_row("Total Review Cards", str(result.data.get("total_review_cards", 0)))
                    table.add_row("Stuck Cards", str(result.data.get("stuck_cards", 0)))
                    table.add_row("Cards with Errors", str(result.data.get("error_cards", 0)))
                    table.add_row("Cards with Issues", str(result.data.get("issue_cards", 0)))
                elif action == "fix":
                    table.add_row("Total Stuck", str(result.data.get("total_stuck", 0)))
                    table.add_row("Fixed", str(result.data.get("fixed_count", 0)))
                elif action == "report":
                    summary = result.data.get("summary", {})
                    table.add_row("Total Cards", str(summary.get("total_review_cards", 0)))
                    table.add_row("Stuck Cards", str(summary.get("stuck_cards", 0)))
                    table.add_row("Avg Stuck Hours", f"{summary.get('avg_stuck_hours', 0):.1f}")
                
                console.print(table)
                
                if verbose and "recommendations" in result.data:
                    console.print("\n[bold]Recommendations:[/bold]")
                    for rec in result.data.get("recommendations", []):
                        console.print(f"  • {rec}")
            else:
                console.print(Panel.fit(
                    "[bold red]Action Failed[/bold red]",
                    border_style="red"
                ))
                for error in result.errors:
                    console.print(f"[red]  {error}[/red]")
            
            await agent.shutdown()
            
        except Exception as e:
            console.print(f"[red]Error: {e}[/red]")
            if verbose:
                import traceback
                console.print(traceback.format_exc())
            sys.exit(1)
    
    asyncio.run(_run())


@cli.command()
@click.option('--output', '-o', default='agents/config.json', help='Output file path')
def generate_config(output: str):
    """Generate default configuration file"""
    try:
        default_config = {
            "project": "q_mini_wasm_v2",
            "version": "1.0.0",
            "description": "Auto-improvement cycle configuration",
            
            "gemini": {
                "api_key_env": "GEMINI_API_KEY",
                "model": "gemini-3-flash-preview",
                "temperature": 0.7,
                "max_tokens": 8192,
                "rate_limits": {
                    "rpm": 15,
                    "tpm": 1000000,
                    "rpd": 1500
                },
                "batch_size": 10,
                "cache_ttl": 3600
            },
            
            "agents": {
                "research_agent": {
                    "name": "ResearchAgent",
                    "description": "Analyzes patterns and gathers information",
                    "system_prompt": "You are a research agent focused on identifying patterns and improvement opportunities.",
                    "tools": ["search_web", "analyze_patterns", "collect_metrics"],
                    "max_iterations": 5,
                    "improvement_cycle_frequency": "daily"
                },
                "analysis_agent": {
                    "name": "AnalysisAgent",
                    "description": "Evaluates performance and identifies bottlenecks",
                    "system_prompt": "You are an analysis agent that evaluates performance and identifies areas for improvement.",
                    "tools": ["evaluate_performance", "identify_bottlenecks", "suggest_improvements"],
                    "max_iterations": 3,
                    "improvement_cycle_frequency": "weekly"
                },
                "code_agent": {
                    "name": "CodeAgent",
                    "description": "Generates and refines code improvements",
                    "system_prompt": "You are a code generation agent that implements improvements.",
                    "tools": ["generate_code", "refactor_code", "validate_code"],
                    "max_iterations": 10,
                    "improvement_cycle_frequency": "on_demand"
                },
                "test_agent": {
                    "name": "TestAgent",
                    "description": "Validates improvements through testing",
                    "system_prompt": "You are a testing agent that validates improvements.",
                    "tools": ["run_tests", "generate_tests", "measure_performance"],
                    "max_iterations": 7,
                    "improvement_cycle_frequency": "per_improvement"
                }
            },
            
            "improvement_cycle": {
                "phases": [
                    {
                        "name": "analysis",
                        "description": "Analyze current performance",
                        "duration_hours": 24,
                        "success_criteria": {
                            "min_patterns_identified": 3,
                            "confidence_threshold": 0.8
                        }
                    },
                    {
                        "name": "planning",
                        "description": "Create improvement plan",
                        "duration_hours": 12,
                        "success_criteria": {
                            "plan_completeness": 0.9,
                            "risk_assessment_completed": true
                        }
                    },
                    {
                        "name": "implementation",
                        "description": "Implement improvements",
                        "duration_hours": 48,
                        "success_criteria": {
                            "code_quality_score": 0.85,
                            "test_coverage": 0.8
                        }
                    },
                    {
                        "name": "validation",
                        "description": "Validate improvements",
                        "duration_hours": 24,
                        "success_criteria": {
                            "all_tests_pass": True,
                            "performance_improvement": 0.1
                        }
                    },
                    {
                        "name": "deployment",
                        "description": "Deploy validated improvements",
                        "duration_hours": 8,
                        "success_criteria": {
                            "deployment_success": True,
                            "rollback_plan_exists": true
                        }
                    }
                ],
                "max_concurrent_improvements": 3,
                "improvement_cooldown_days": 7,
                "rollback_threshold": 0.95
            },
            
            "monitoring": {
                "metrics": [
                    "task_completion_rate",
                    "response_time",
                    "token_usage",
                    "error_rate",
                    "user_satisfaction"
                ],
                "alert_thresholds": {
                    "error_rate": 0.05,
                    "response_time_seconds": 30,
                    "token_usage_per_day": 1200
                },
                "retention_days": 30
            },
            
            "security": {
                "api_key_rotation_days": 30,
                "max_concurrent_requests": 5,
                "input_validation": "strict",
                "output_sanitization": true,
                "audit_logging": true
            }
        }
        
        output_path = Path(output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(default_config, indent=2),
            encoding="utf-8"
        )
        
        console.print(f"[green]Configuration generated: {output}[/green]")
        
    except Exception as e:
        console.print(f"[red]Error: {e}[/red]")
        sys.exit(1)


if __name__ == '__main__':
    cli()
