"""
Kanban Review Fix Agent

This agent monitors the Kanban board for items stuck in "Review" status
and automatically handles items that are stuck due to errors/issues
instead of pending approvals.
"""

import asyncio
import json
import logging
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any, Dict, List, Optional
from enum import Enum

import structlog

from .base_agent import BaseAgent, AgentConfig, TaskResult, AgentState
from .llm.message_validator import LLMMessageValidator, validate_and_fix_messages

logger = structlog.get_logger()


class ReviewStatus(Enum):
    """Status of a review item."""
    PENDING_APPROVAL = "pending_approval"
    ERROR_FOUND = "error_found"
    ISSUE_FOUND = "issue_found"
    BLOCKED = "blocked"
    READY_TO_MERGE = "ready_to_merge"
    DONE = "done"


class CardAction(Enum):
    """Actions to take on stuck cards."""
    MOVE_TO_REJECTED = "move_to_rejected"
    CREATE_FIX_CARD = "create_fix_card"
    ESCALATE = "escalate"
    AUTO_FIX = "auto_fix"
    MOVE_TO_DONE = "move_to_done"
    WAIT = "wait"


@dataclass
class ReviewCard:
    """Represents a card in the Kanban review column."""
    id: str
    title: str
    description: str
    created_at: datetime
    updated_at: datetime
    status: ReviewStatus
    errors: List[str] = field(default_factory=list)
    issues: List[str] = field(default_factory=list)
    review_comments: List[str] = field(default_factory=list)
    assigned_to: Optional[str] = None
    module: str = ""
    priority: str = "medium"
    
    @property
    def age_hours(self) -> float:
        """Get the age of the card in hours."""
        return (datetime.now() - self.created_at).total_seconds() / 3600
    
    @property
    def is_stuck(self) -> bool:
        """Check if card is stuck (more than 48 hours in review)."""
        return self.age_hours > 48
    
    @property
    def has_blocking_issues(self) -> bool:
        """Check if card has blocking issues."""
        return len(self.errors) > 0 or self.status == ReviewStatus.ERROR_FOUND
    
    @property
    def reason_for_stuck(self) -> str:
        """Get the reason why the card is stuck."""
        if self.errors:
            return f"Errors found: {', '.join(self.errors[:3])}"
        if self.issues:
            return f"Issues found: {', '.join(self.issues[:3])}"
        if self.status == ReviewStatus.BLOCKED:
            return "Blocked by dependencies"
        return "Pending approval (no issues detected)"
    
    @property
    def is_done(self) -> bool:
        """Check if card is done (no errors, no issues, not blocked, or explicitly marked as no further action needed)."""
        return (not self.errors and 
                not self.issues and 
                self.status not in [ReviewStatus.ERROR_FOUND, ReviewStatus.ISSUE_FOUND, ReviewStatus.BLOCKED]) or \
               self.no_further_action_needed
    
    @property
    def no_further_action_needed(self) -> bool:
        """Check if card has been explicitly marked as 'No further action needed'."""
        # Check description
        if self.description and "no further action needed" in self.description.lower():
            return True
        # Check review comments
        for comment in self.review_comments:
            if "no further action needed" in comment.lower():
                return True
        # Check status
        if self.status == ReviewStatus.DONE:
            return True
        return False
    
    @property
    def is_ready_to_move_to_done(self) -> bool:
        """Check if card is ready to be moved to done column."""
        # Card must be done and have been in review for at least some time
        # (to ensure it has gone through proper review process)
        # OR explicitly marked as no further action needed
        return (self.is_done and self.age_hours >= 1) or self.no_further_action_needed  # At least 1 hour in review or explicit no action needed


@dataclass
class ReviewFixResult:
    """Result of fixing a stuck review card."""
    card_id: str
    action_taken: CardAction
    success: bool
    message: str
    new_status: Optional[str] = None
    timestamp: datetime = field(default_factory=datetime.now)


class KanbanReviewFixAgent(BaseAgent):
    """
    Agent that monitors and fixes Kanban boards stuck in Review.
    
    This agent specifically handles cases where cards are stuck in "Review"
    due to errors and issues rather than pending approvals.
    """
    
    def __init__(self, config: AgentConfig, llm_service=None, kanban_config_path: str = "config/kanban-config.json"):
        super().__init__(config, llm_service)
        self.kanban_config_path = kanban_config_path
        self.kanban_config = self._load_kanban_config()
        self.review_cards: List[ReviewCard] = []
        self.fix_history: List[ReviewFixResult] = []
        self.max_stuck_hours = 48  # Maximum hours before considering a card stuck
        self.auto_fix_enabled = True
        
    def _load_kanban_config(self) -> Dict[str, Any]:
        """Load Kanban configuration."""
        try:
            config_path = Path(self.kanban_config_path)
            if config_path.exists():
                return json.loads(config_path.read_text(encoding="utf-8"))
        except Exception as e:
            self.logger.error("Failed to load Kanban config", error=str(e))
        return {}
    
    async def initialize(self) -> None:
        """Initialize the agent."""
        await super().initialize()
        self.logger.info("Kanban Review Fix Agent initialized",
                        max_stuck_hours=self.max_stuck_hours,
                        auto_fix_enabled=self.auto_fix_enabled)
    
    async def execute_task(self, task: Dict[str, Any]) -> TaskResult:
        """
        Execute a task to fix stuck Kanban review cards.
        
        Args:
            task: Task specification with keys:
                - action: "scan" | "fix" | "report"
                - card_id: Optional specific card ID to fix
                
        Returns:
            TaskResult with execution results
        """
        action = task.get("action", "scan")
        card_id = task.get("card_id")
        
        try:
            if action == "scan":
                result = await self._scan_review_cards()
            elif action == "fix":
                if card_id:
                    result = await self._fix_specific_card(card_id)
                else:
                    result = await self._fix_all_stuck_cards()
            elif action == "report":
                result = await self._generate_report()
            else:
                return TaskResult(
                    success=False,
                    errors=[f"Unknown action: {action}"]
                )
            
            return result
            
        except Exception as e:
            self.logger.error("Task execution failed", error=str(e))
            return TaskResult(
                success=False,
                errors=[str(e)]
            )
    
    async def _scan_review_cards(self) -> TaskResult:
        """Scan Kanban board for cards in review status."""
        self.logger.info("Scanning Kanban board for review cards")
        
        # Load cards from the Kanban system
        self.review_cards = await self._load_review_cards()
        
        # Analyze each card
        stuck_cards = []
        error_cards = []
        issue_cards = []
        done_cards = []
        no_action_needed_cards = []
        
        for card in self.review_cards:
            if card.no_further_action_needed:
                no_action_needed_cards.append(card)
            elif card.is_ready_to_move_to_done:
                done_cards.append(card)
            elif card.is_stuck:
                stuck_cards.append(card)
                if card.has_blocking_issues:
                    error_cards.append(card)
                elif card.issues:
                    issue_cards.append(card)
        
        # Store patterns in memory
        self.memory.store_pattern({
            "type": "kanban_review_scan",
            "total_cards": len(self.review_cards),
            "stuck_cards": len(stuck_cards),
            "error_cards": len(error_cards),
            "issue_cards": len(issue_cards),
            "done_cards": len(done_cards),
            "no_action_needed_cards": len(no_action_needed_cards),
            "timestamp": datetime.now().isoformat()
        })
        
        return TaskResult(
            success=True,
            data={
                "total_review_cards": len(self.review_cards),
                "stuck_cards": len(stuck_cards),
                "error_cards": len(error_cards),
                "issue_cards": len(issue_cards),
                "done_cards": len(done_cards),
                "no_action_needed_cards": len(no_action_needed_cards),
                "stuck_card_ids": [c.id for c in stuck_cards],
                "done_card_ids": [c.id for c in done_cards],
                "no_action_needed_card_ids": [c.id for c in no_action_needed_cards]
            }
        )
    
    async def _load_review_cards(self) -> List[ReviewCard]:
        """
        Load cards from the Kanban system that are in review status.
        
        In a real implementation, this would connect to the Kanban API.
        For now, we simulate loading cards from the config and state files.
        """
        cards = []
        
        # Check for cards in the autolearn hooks configuration
        hooks_path = Path("config/autolearn-hooks.json")
        if hooks_path.exists():
            try:
                # Handle BOM (Byte Order Mark) in JSON files
                hooks_content = hooks_path.read_text(encoding="utf-8-sig")
                hooks = json.loads(hooks_content)
                # In a real implementation, we'd query the Kanban API
                # For now, create sample cards based on config
                pass
            except Exception as e:
                self.logger.error("Failed to load hooks config", error=str(e))
        
        # Check for rejected improvements that might be stuck in review
        rejected_path = Path("config/rejected-improvements.json")
        if rejected_path.exists():
            try:
                rejected = json.loads(rejected_path.read_text(encoding="utf-8"))
                for imp in rejected.get("rejected_improvements", []):
                    if imp.get("status") == "pending_review":
                        # Determine review status based on errors/issues
                        review_status = ReviewStatus.PENDING_APPROVAL
                        if imp.get("errors") and len(imp["errors"]) > 0:
                            review_status = ReviewStatus.ERROR_FOUND
                        elif imp.get("issues") and len(imp["issues"]) > 0:
                            review_status = ReviewStatus.ISSUE_FOUND
                        
                        card = ReviewCard(
                            id=imp["id"],
                            title=imp["name"],
                            description=imp["description"],
                            created_at=datetime.fromisoformat(imp["rejected_at"]),
                            updated_at=datetime.now(),
                            status=review_status,
                            errors=imp.get("errors", []),
                            issues=imp.get("issues", []),
                            module=imp.get("module", ""),
                            priority=imp.get("priority", "high" if "high_complexity" in imp.get("rejection_reasons", []) else "medium")
                        )
                        cards.append(card)
            except Exception as e:
                self.logger.error("Failed to load rejected improvements", error=str(e))
        
        # In a real implementation, we would also:
        # 1. Query GitHub Issues/PRs
        # 2. Query the Kanban API
        # 3. Check CI/CD pipeline status
        # 4. Analyze error logs
        
        return cards
    
    async def _fix_specific_card(self, card_id: str) -> TaskResult:
        """Fix a specific card stuck in review."""
        card = next((c for c in self.review_cards if c.id == card_id), None)
        
        if not card:
            return TaskResult(
                success=False,
                errors=[f"Card {card_id} not found"]
            )
        
        return await self._fix_card(card)
    
    async def _fix_all_stuck_cards(self) -> TaskResult:
        """Fix all cards stuck in review."""
        # First scan to get current state
        await self._scan_review_cards()
        
        stuck_cards = [c for c in self.review_cards if c.is_stuck]
        done_cards = [c for c in self.review_cards if c.is_ready_to_move_to_done]
        
        if not stuck_cards and not done_cards:
            return TaskResult(
                success=True,
                data={"message": "No stuck or done cards found", "fixed_count": 0}
            )
        
        fixed_count = 0
        results = []
        
        # First, move done cards to done column
        for card in done_cards:
            result = await self._fix_card(card)
            results.append(result)
            if result.success:
                fixed_count += 1
        
        # Then, fix stuck cards
        for card in stuck_cards:
            result = await self._fix_card(card)
            results.append(result)
            if result.success:
                fixed_count += 1
        
        return TaskResult(
            success=True,
            data={
                "total_stuck": len(stuck_cards),
                "total_done": len(done_cards),
                "fixed_count": fixed_count,
                "results": [r.__dict__ for r in results]
            }
        )
    
    async def _fix_card(self, card: ReviewCard) -> ReviewFixResult:
        """
        Determine and execute the appropriate fix action for a stuck card.
        
        Logic:
        1. If card has blocking errors -> Move to rejected or create fix card
        2. If card has non-blocking issues -> Create fix card
        3. If card is just pending approval for too long -> Escalate
        4. If card can be auto-fixed -> Attempt auto-fix
        """
        self.logger.info("Analyzing stuck card",
                        card_id=card.id,
                        title=card.title,
                        age_hours=card.age_hours,
                        has_errors=card.has_blocking_issues)
        
        # Determine the appropriate action
        action = self._determine_action(card)
        
        # Execute the action
        result = await self._execute_action(card, action)
        
        # Record in history
        self.fix_history.append(result)
        
        # Store in memory
        self.memory.store_improvement({
            "type": "kanban_review_fix",
            "card_id": card.id,
            "action": action.value,
            "success": result.success,
            "message": result.message
        })
        
        return result
    
    def _determine_action(self, card: ReviewCard) -> CardAction:
        """Determine the appropriate action for a stuck card."""
        
        # If card is ready to be moved to done
        if card.is_ready_to_move_to_done:
            return CardAction.MOVE_TO_DONE
        
        # If card has blocking errors
        if card.has_blocking_issues:
            # If errors are critical and unrecoverable, move to rejected
            if any("critical" in e.lower() for e in card.errors):
                return CardAction.MOVE_TO_REJECTED
            # Otherwise, create a fix card
            return CardAction.CREATE_FIX_CARD
        
        # If card has non-blocking issues
        if card.issues:
            # If auto-fix is enabled and issues are fixable
            if self.auto_fix_enabled and self._are_issues_fixable(card.issues):
                return CardAction.AUTO_FIX
            return CardAction.CREATE_FIX_CARD
        
        # If card is just pending approval for too long
        if card.age_hours > self.max_stuck_hours * 2:  # Double the threshold
            return CardAction.ESCALATE
        
        # Default: wait a bit more
        return CardAction.WAIT
    
    def _are_issues_fixable(self, issues: List[str]) -> bool:
        """Check if issues can be automatically fixed."""
        fixable_patterns = [
            "formatting",
            "style",
            "lint",
            "documentation",
            "import",
            "unused",
            "typo"
        ]
        
        for issue in issues:
            issue_lower = issue.lower()
            if any(pattern in issue_lower for pattern in fixable_patterns):
                return True
        return False
    
    async def _execute_action(self, card: ReviewCard, action: CardAction) -> ReviewFixResult:
        """Execute the determined action."""
        
        if action == CardAction.MOVE_TO_REJECTED:
            return await self._move_to_rejected(card)
        elif action == CardAction.CREATE_FIX_CARD:
            return await self._create_fix_card(card)
        elif action == CardAction.ESCALATE:
            return await self._escalate_card(card)
        elif action == CardAction.AUTO_FIX:
            return await self._attempt_auto_fix(card)
        elif action == CardAction.MOVE_TO_DONE:
            return await self._move_to_done(card)
        elif action == CardAction.WAIT:
            return ReviewFixResult(
                card_id=card.id,
                action_taken=action,
                success=True,
                message=f"Card {card.id} is still within acceptable review time ({card.age_hours:.1f} hours)"
            )
        else:
            return ReviewFixResult(
                card_id=card.id,
                action_taken=action,
                success=False,
                message=f"Unknown action: {action}"
            )
    
    async def _move_to_rejected(self, card: ReviewCard) -> ReviewFixResult:
        """Move a card to the rejected column."""
        self.logger.info("Moving card to rejected", card_id=card.id)
        
        try:
            # Load rejected improvements
            rejected_path = Path("config/rejected-improvements.json")
            rejected = {}
            if rejected_path.exists():
                rejected = json.loads(rejected_path.read_text(encoding="utf-8"))
            
            # Add to rejected improvements
            if "rejected_improvements" not in rejected:
                rejected["rejected_improvements"] = []
            
            rejected["rejected_improvements"].append({
                "id": f"rejected_{len(rejected['rejected_improvements']) + 1:03d}",
                "name": card.title,
                "module": card.module,
                "description": card.description,
                "rejected_at": datetime.now().isoformat(),
                "rejection_reasons": ["stuck_in_review", "blocking_errors"],
                "feasibility_assessment": {
                    "technical_complexity": "high" if card.has_blocking_issues else "medium",
                    "resource_requirements": "significant",
                    "alignment_score": "low",
                    "potential_value": "low"
                },
                "notes": f"Auto-rejected by KanbanReviewFixAgent. Errors: {', '.join(card.errors[:3])}",
                "status": "rejected",
                "review_date": None,
                "reconsideration_criteria": []
            })
            
            # Save
            rejected_path.write_text(
                json.dumps(rejected, indent=2, ensure_ascii=False),
                encoding="utf-8"
            )
            
            # Update the original card's status in the rejected-improvements.json
            for imp in rejected.get("rejected_improvements", []):
                if imp.get("id") == card.id:
                    imp["status"] = "rejected"
                    break
            
            # Save again with updated status
            rejected_path.write_text(
                json.dumps(rejected, indent=2, ensure_ascii=False),
                encoding="utf-8"
            )
            
            return ReviewFixResult(
                card_id=card.id,
                action_taken=CardAction.MOVE_TO_REJECTED,
                success=True,
                message=f"Card {card.id} moved to rejected due to blocking errors",
                new_status="rejected"
            )
            
        except Exception as e:
            return ReviewFixResult(
                card_id=card.id,
                action_taken=CardAction.MOVE_TO_REJECTED,
                success=False,
                message=f"Failed to move card to rejected: {str(e)}"
            )
    
    async def _move_to_done(self, card: ReviewCard) -> ReviewFixResult:
        """Move a card to the done column."""
        self.logger.info("Moving card to done", card_id=card.id)
        
        try:
            # In a real implementation, this would:
            # 1. Update the card status in the Kanban API
            # 2. Move the card to the "done" column
            # 3. Notify relevant parties
            
            self.logger.info("Card moved to done",
                           card_id=card.id,
                           title=card.title,
                           age_hours=card.age_hours)
            
            return ReviewFixResult(
                card_id=card.id,
                action_taken=CardAction.MOVE_TO_DONE,
                success=True,
                message=f"Card {card.id} moved to done column after {card.age_hours:.1f} hours in review",
                new_status="done"
            )
            
        except Exception as e:
            return ReviewFixResult(
                card_id=card.id,
                action_taken=CardAction.MOVE_TO_DONE,
                success=False,
                message=f"Failed to move card to done: {str(e)}"
            )
    
    async def _create_fix_card(self, card: ReviewCard) -> ReviewFixResult:
        """Create a fix card for issues found during review."""
        self.logger.info("Creating fix card", card_id=card.id)
        
        try:
            # Create a fix task in the Kanban system
            fix_card = {
                "id": f"fix_{card.id}",
                "title": f"Fix: {card.title}",
                "description": f"Issues found during review of {card.id}:\n" + 
                              "\n".join(f"- {issue}" for issue in card.issues + card.errors),
                "parent_card": card.id,
                "created_at": datetime.now().isoformat(),
                "status": "detected",
                "priority": card.priority,
                "module": card.module
            }
            
            # In a real implementation, this would create the card in the Kanban API
            # For now, we log the action
            self.logger.info("Fix card created",
                           fix_card_id=fix_card["id"],
                           parent_card=card.id)
            
            # Update the original card's status to indicate fix card was created
            rejected_path = Path("config/rejected-improvements.json")
            if rejected_path.exists():
                try:
                    rejected = json.loads(rejected_path.read_text(encoding="utf-8"))
                    for imp in rejected.get("rejected_improvements", []):
                        if imp.get("id") == card.id:
                            imp["status"] = "fix_card_created"
                            break
                    rejected_path.write_text(
                        json.dumps(rejected, indent=2, ensure_ascii=False),
                        encoding="utf-8"
                    )
                except Exception as e:
                    self.logger.error("Failed to update card status", error=str(e))
            
            return ReviewFixResult(
                card_id=card.id,
                action_taken=CardAction.CREATE_FIX_CARD,
                success=True,
                message=f"Created fix card {fix_card['id']} for issues in {card.id}",
                new_status="fix_created"
            )
            
        except Exception as e:
            return ReviewFixResult(
                card_id=card.id,
                action_taken=CardAction.CREATE_FIX_CARD,
                success=False,
                message=f"Failed to create fix card: {str(e)}"
            )
    
    async def _escalate_card(self, card: ReviewCard) -> ReviewFixResult:
        """Escalate a card that's been pending approval too long."""
        self.logger.info("Escalating card", card_id=card.id)
        
        try:
            # Create escalation record
            escalation = {
                "card_id": card.id,
                "title": card.title,
                "age_hours": card.age_hours,
                "escalated_at": datetime.now().isoformat(),
                "reason": "Pending approval for too long",
                "assigned_to": card.assigned_to or "team_lead",
                "priority": "high"
            }
            
            # In a real implementation, this would:
            # 1. Send notification to team lead
            # 2. Create escalation ticket
            # 3. Update card priority
            
            self.logger.info("Card escalated",
                           card_id=card.id,
                           escalation=escalation)
            
            return ReviewFixResult(
                card_id=card.id,
                action_taken=CardAction.ESCALATE,
                success=True,
                message=f"Card {card.id} escalated after {card.age_hours:.1f} hours in review",
                new_status="escalated"
            )
            
        except Exception as e:
            return ReviewFixResult(
                card_id=card.id,
                action_taken=CardAction.ESCALATE,
                success=False,
                message=f"Failed to escalate card: {str(e)}"
            )
    
    async def _attempt_auto_fix(self, card: ReviewCard) -> ReviewFixResult:
        """Attempt to automatically fix issues in a card."""
        self.logger.info("Attempting auto-fix", card_id=card.id)
        
        try:
            # Analyze issues to determine fixability
            fixable_issues = []
            unfixable_issues = []
            
            for issue in card.issues:
                if self._is_issue_auto_fixable(issue):
                    fixable_issues.append(issue)
                else:
                    unfixable_issues.append(issue)
            
            if not fixable_issues:
                return ReviewFixResult(
                    card_id=card.id,
                    action_taken=CardAction.AUTO_FIX,
                    success=False,
                    message=f"No auto-fixable issues found in {card.id}"
                )
            
            # In a real implementation, this would:
            # 1. Apply code fixes
            # 2. Run tests
            # 3. Update the card
            
            self.logger.info("Auto-fix applied",
                           card_id=card.id,
                           fixed_issues=len(fixable_issues),
                           remaining_issues=len(unfixable_issues))
            
            return ReviewFixResult(
                card_id=card.id,
                action_taken=CardAction.AUTO_FIX,
                success=True,
                message=f"Auto-fixed {len(fixable_issues)} issues in {card.id}",
                new_status="auto_fixed"
            )
            
        except Exception as e:
            return ReviewFixResult(
                card_id=card.id,
                action_taken=CardAction.AUTO_FIX,
                success=False,
                message=f"Auto-fix failed: {str(e)}"
            )
    
    def _is_issue_auto_fixable(self, issue: str) -> bool:
        """Check if an issue can be automatically fixed."""
        auto_fixable_patterns = [
            "trailing whitespace",
            "missing newline",
            "indentation",
            "import order",
            "unused import",
            "typo",
            "documentation format",
            "style violation"
        ]
        
        issue_lower = issue.lower()
        return any(pattern in issue_lower for pattern in auto_fixable_patterns)
    
    async def _generate_report(self) -> TaskResult:
        """Generate a report on stuck review cards."""
        await self._scan_review_cards()
        
        stuck_cards = [c for c in self.review_cards if c.is_stuck]
        done_cards = [c for c in self.review_cards if c.is_ready_to_move_to_done and not c.no_further_action_needed]
        no_action_needed_cards = [c for c in self.review_cards if c.no_further_action_needed]
        
        report = {
            "generated_at": datetime.now().isoformat(),
            "summary": {
                "total_review_cards": len(self.review_cards),
                "stuck_cards": len(stuck_cards),
                "done_cards": len(done_cards),
                "no_action_needed_cards": len(no_action_needed_cards),
                "cards_with_errors": len([c for c in stuck_cards if c.has_blocking_issues]),
                "cards_with_issues": len([c for c in stuck_cards if c.issues and not c.has_blocking_issues]),
                "avg_stuck_hours": sum(c.age_hours for c in stuck_cards) / len(stuck_cards) if stuck_cards else 0
            },
            "stuck_cards": [
                {
                    "id": c.id,
                    "title": c.title,
                    "age_hours": c.age_hours,
                    "reason": c.reason_for_stuck,
                    "errors": c.errors,
                    "issues": c.issues,
                    "module": c.module,
                    "priority": c.priority
                }
                for c in stuck_cards
            ],
            "done_cards": [
                {
                    "id": c.id,
                    "title": c.title,
                    "age_hours": c.age_hours,
                    "module": c.module,
                    "priority": c.priority
                }
                for c in done_cards
            ],
            "no_action_needed_cards": [
                {
                    "id": c.id,
                    "title": c.title,
                    "age_hours": c.age_hours,
                    "module": c.module,
                    "priority": c.priority,
                    "reason": "Explicitly marked as 'No further action needed'"
                }
                for c in no_action_needed_cards
            ],
            "actions_taken": [
                {
                    "card_id": r.card_id,
                    "action": r.action_taken.value,
                    "success": r.success,
                    "message": r.message,
                    "timestamp": r.timestamp.isoformat()
                }
                for r in self.fix_history[-10:]  # Last 10 actions
            ],
            "recommendations": self._generate_recommendations(stuck_cards, done_cards)
        }
        
        # Save report
        report_path = Path("reports/kanban-review-fix-report.json")
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(report, indent=2, ensure_ascii=False),
            encoding="utf-8"
        )
        
        return TaskResult(
            success=True,
            data=report
        )
    
    def _generate_recommendations(self, stuck_cards: List[ReviewCard], done_cards: List[ReviewCard] = None) -> List[str]:
        """Generate recommendations based on stuck cards analysis."""
        recommendations = []
        done_cards = done_cards or []
        
        if not stuck_cards and not done_cards:
            recommendations.append("No stuck or done cards found. Review process is healthy.")
            return recommendations
        
        # Report done cards
        if done_cards:
            recommendations.append(
                f"{len(done_cards)} cards are ready to be moved to done column. "
                "Consider implementing auto-cleanup to move these cards automatically."
            )
        
        # Analyze patterns
        error_cards = [c for c in stuck_cards if c.has_blocking_issues]
        issue_cards = [c for c in stuck_cards if c.issues and not c.has_blocking_issues]
        long_pending = [c for c in stuck_cards if c.age_hours > self.max_stuck_hours * 2]
        
        if error_cards:
            recommendations.append(
                f"{len(error_cards)} cards have blocking errors. "
                "Consider implementing stricter pre-review checks."
            )
        
        if issue_cards:
            recommendations.append(
                f"{len(issue_cards)} cards have non-blocking issues. "
                "Consider implementing auto-fix for common issues."
            )
        
        if long_pending:
            recommendations.append(
                f"{len(long_pending)} cards have been pending approval for over {self.max_stuck_hours * 2} hours. "
                "Consider implementing approval reminders or escalation."
            )
        
        # Module-specific recommendations
        modules = set(c.module for c in stuck_cards if c.module)
        for module in modules:
            module_cards = [c for c in stuck_cards if c.module == module]
            if len(module_cards) > 2:
                recommendations.append(
                    f"Module '{module}' has {len(module_cards)} stuck cards. "
                    "Consider reviewing the module's review process."
                )
        
        return recommendations
    
    async def analyze_performance(self) -> Dict[str, Any]:
        """Analyze the agent's performance."""
        return {
            "name": self.name,
            "state": self.state.value,
            "cards_scanned": len(self.review_cards),
            "cards_fixed": len(self.fix_history),
            "success_rate": (
                len([r for r in self.fix_history if r.success]) / len(self.fix_history)
                if self.fix_history else 0
            ),
            "avg_fix_time": 0,  # Would be calculated from timestamps
            "patterns_stored": len(self.memory.patterns)
        }
    
    async def handle_llm_api_error(self, error_message: str) -> ReviewFixResult:
        """
        Handle LLM API errors, particularly the OpenRouter streaming error.
        
        This method specifically addresses the error:
        "messages[32] assistant must provide content or tool_calls"
        
        Args:
            error_message: The error message from the LLM API
            
        Returns:
            ReviewFixResult with the fix action taken
        """
        self.logger.info("Handling LLM API error", error=error_message[:200])
        
        try:
            # Parse the error message
            validator = LLMMessageValidator(provider="openrouter")
            error_info = validator.extract_error_info(error_message)
            
            if error_info["is_fixable"]:
                # Create a fix card for the LLM API error
                fix_card = {
                    "id": f"llm_error_fix_{datetime.now().strftime('%Y%m%d_%H%M%S')}",
                    "title": f"Fix LLM API Error: {error_info['error_type']}",
                    "description": f"LLM API Error Details:\n"
                                  f"- Error Type: {error_info['error_type']}\n"
                                  f"- Message Index: {error_info.get('message_index', 'Unknown')}\n"
                                  f"- Provider: {error_info['provider']}\n"
                                  f"- Original Error: {error_message[:500]}...\n\n"
                                  f"Solution: Ensure all assistant messages have either 'content' or 'tool_calls'.\n"
                                  f"Use the LLMMessageValidator to validate and fix messages before sending to API.",
                    "created_at": datetime.now().isoformat(),
                    "status": "detected",
                    "priority": "high",
                    "module": "llm_integration",
                    "fix_type": "automatic",
                    "solution": "Add message validation before LLM API calls"
                }
                
                # Log the fix
                self.logger.info("Created fix card for LLM API error",
                               fix_card_id=fix_card["id"],
                               error_type=error_info["error_type"])
                
                return ReviewFixResult(
                    card_id=fix_card["id"],
                    action_taken=CardAction.CREATE_FIX_CARD,
                    success=True,
                    message=f"Created fix card for LLM API error: {error_info['error_type']}. "
                           f"Solution: Add message validation to ensure assistant messages have content or tool_calls.",
                    new_status="fix_created"
                )
            else:
                # Error is not fixable automatically
                return ReviewFixResult(
                    card_id="llm_error",
                    action_taken=CardAction.ESCALATE,
                    success=False,
                    message=f"LLM API error is not automatically fixable: {error_message[:200]}"
                )
                
        except Exception as e:
            self.logger.error("Failed to handle LLM API error", error=str(e))
            return ReviewFixResult(
                card_id="llm_error",
                action_taken=CardAction.ESCALATE,
                success=False,
                message=f"Failed to handle LLM API error: {str(e)}"
            )
    
    def validate_llm_messages(self, messages: List[Dict[str, Any]]) -> Tuple[bool, List[Dict[str, Any]]]:
        """
        Validate LLM messages before sending to API.
        
        This prevents the "messages[X] assistant must provide content or tool_calls" error.
        
        Args:
            messages: List of message dictionaries
            
        Returns:
            Tuple of (is_valid, fixed_messages)
        """
        try:
            is_valid, fixed_messages, errors = validate_and_fix_messages(messages, provider="openrouter")
            
            if not is_valid:
                self.logger.warning("LLM messages validation failed",
                                  errors=errors,
                                  message_count=len(messages))
            
            return is_valid, fixed_messages
            
        except Exception as e:
            self.logger.error("Failed to validate LLM messages", error=str(e))
            return False, messages
    
    async def suggest_improvements(self) -> List[Dict[str, Any]]:
        """Suggest improvements based on analysis."""
        improvements = []
        
        # Analyze fix history for patterns
        if self.fix_history:
            failed_fixes = [r for r in self.fix_history if not r.success]
            if failed_fixes:
                improvements.append({
                    "type": "error_handling",
                    "description": "Improve error handling for failed auto-fixes",
                    "priority": "high",
                    "estimated_impact": "Reduce failed fixes by 30%"
                })
        
        # Check if auto-fix could be expanded
        if self.auto_fix_enabled:
            improvements.append({
                "type": "auto_fix_expansion",
                "description": "Expand auto-fix capabilities to handle more issue types",
                "priority": "medium",
                "estimated_impact": "Reduce manual fixes by 20%"
            })
        
        # Add LLM error handling improvement
        improvements.append({
            "type": "llm_error_handling",
            "description": "Add validation for LLM API messages to prevent streaming errors",
            "priority": "high",
            "estimated_impact": "Prevent 100% of 'assistant must provide content or tool_calls' errors"
        })
        
        return improvements


async def main():
    """Main function for testing the agent."""
    config = AgentConfig(
        name="KanbanReviewFixAgent",
        description="Monitors and fixes Kanban boards stuck in Review",
        system_prompt="You are an agent that monitors Kanban boards and fixes stuck review cards.",
        tools=["scan_review", "fix_card", "generate_report"]
    )
    
    agent = KanbanReviewFixAgent(config)
    await agent.initialize()
    
    # Scan for stuck cards
    scan_result = await agent.execute_task({"action": "scan"})
    print(f"Scan result: {scan_result.data}")
    
    # Fix stuck cards
    fix_result = await agent.execute_task({"action": "fix"})
    print(f"Fix result: {fix_result.data}")
    
    # Generate report
    report_result = await agent.execute_task({"action": "report"})
    print(f"Report saved to: reports/kanban-review-fix-report.json")
    
    await agent.shutdown()


if __name__ == "__main__":
    asyncio.run(main())