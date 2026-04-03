# Kanban Review Fix Agent

This agent monitors the Kanban board for items stuck in "Review" status and automatically handles items that are stuck due to errors/issues instead of pending approvals.

## Problem Statement

Kanban boards often have cards stuck in "Review" status for extended periods. This can happen for two reasons:

1. **Pending Approval**: The card is waiting for someone to review and approve it (normal workflow)
2. **Errors/Issues Found**: The card has errors or issues that need to be fixed before it can be approved (blocking)

When cards are stuck due to errors/issues, they block the workflow and need to be handled differently than cards simply waiting for approval.

## Solution

The Kanban Review Fix Agent:

1. **Monitors** the Kanban board for cards in review status
2. **Detects** cards that have been stuck for too long (>48 hours)
3. **Classifies** whether they're stuck due to:
   - Blocking errors (critical issues that prevent approval)
   - Non-blocking issues (minor issues that can be fixed)
   - Simply pending approval (no issues, just waiting)
4. **Takes appropriate action**:
   - Move cards with critical errors to "Rejected"
   - Create fix cards for non-blocking issues
   - Escalate cards pending approval for too long
   - Auto-fix simple issues (formatting, linting, etc.)

## Usage

### CLI Commands

```bash
# Scan for stuck cards
python -m agents.cli kanban-review --action scan

# Fix all stuck cards
python -m agents.cli kanban-review --action fix

# Generate a report
python -m agents.cli kanban-review --action report

# Fix a specific card
python -m agents.cli kanban-review --action fix --card-id <card-id>

# Use custom config
python -m agents.cli kanban-review --action scan --config path/to/kanban-config.json

# Verbose output
python -m agents.cli kanban-review --action scan --verbose
```

### Python API

```python
from agents import KanbanReviewFixAgent, AgentConfig

# Create agent
config = AgentConfig(
    name="KanbanReviewFixAgent",
    description="Monitors and fixes Kanban boards stuck in Review",
    system_prompt="You are an agent that monitors Kanban boards and fixes stuck review cards.",
    tools=["scan_review", "fix_card", "generate_report"]
)

agent = KanbanReviewFixAgent(config)
await agent.initialize()

# Scan for stuck cards
result = await agent.execute_task({"action": "scan"})
print(f"Found {result.data['stuck_cards']} stuck cards")

# Fix stuck cards
result = await agent.execute_task({"action": "fix"})
print(f"Fixed {result.data['fixed_count']} cards")

# Generate report
result = await agent.execute_task({"action": "report"})
print("Report saved to reports/kanban-review-fix-report.json")

await agent.shutdown()
```

## Configuration

The agent uses the following configuration:

- `max_stuck_hours`: Maximum hours before considering a card stuck (default: 48)
- `auto_fix_enabled`: Whether to attempt automatic fixes (default: True)
- `kanban_config_path`: Path to Kanban configuration file (default: "config/kanban-config.json")

## Actions

The agent can take the following actions on stuck cards:

### MOVE_TO_REJECTED
- **When**: Card has critical, unrecoverable errors
- **What**: Moves the card to the "Rejected" column
- **Why**: Prevents blocking issues from staying in the workflow

### CREATE_FIX_CARD
- **When**: Card has non-blocking issues
- **What**: Creates a new card to fix the issues
- **Why**: Ensures issues are tracked and fixed

### ESCALATE
- **When**: Card has been pending approval for too long (>96 hours)
- **What**: Escalates the card to team lead
- **Why**: Ensures approvals don't block workflow indefinitely

### AUTO_FIX
- **When**: Card has simple, auto-fixable issues (formatting, linting, etc.)
- **What**: Automatically fixes the issues
- **Why**: Reduces manual effort for simple fixes

### WAIT
- **When**: Card is still within acceptable review time
- **What**: Takes no action
- **Why**: Allows normal review process to continue

## GitHub Actions Integration

The agent can be run automatically via GitHub Actions:

```yaml
# .github/workflows/kanban-review-fix.yml
name: Kanban Review Fix

on:
  schedule:
    - cron: 0 8 * * 1-5  # Run at 8 AM UTC Monday-Friday
  workflow_dispatch:
    inputs:
      action:
        description: 'Action to perform'
        required: true
        default: 'scan'
        type: choice
        options:
          - scan
          - fix
          - report
```

This will:
1. Run automatically every weekday at 8 AM UTC
2. Allow manual triggering with different actions
3. Generate reports and create issues for stuck cards

## Reports

The agent generates reports in `reports/kanban-review-fix-report.json` with:

- **Summary**: Total cards, stuck cards, cards with errors/issues
- **Stuck Cards**: Details of each stuck card including age, issues, and recommendations
- **Actions Taken**: History of actions taken by the agent
- **Recommendations**: Suggestions for improving the review process

## Recommendations

Based on stuck card analysis, the agent provides recommendations:

1. **For cards with blocking errors**: Implement stricter pre-review checks
2. **For cards with non-blocking issues**: Implement auto-fix for common issues
3. **For cards pending approval too long**: Implement approval reminders or escalation
4. **For modules with many stuck cards**: Review the module's review process

5. **For cards ready to be done**: Implement auto-cleanup to move completed cards to Done column

## Auto-Cleanup Feature

The agent now includes an auto-cleanup feature for cards that are ready to be moved to the Done column:

### How it works
1. **Detection**: Cards are considered "ready to be done" when:
   - No errors
   - No issues
   - Not blocked
   - Have been in review for at least 1 hour (to ensure proper review process)

2. **Action**: When a card is ready to be done, the agent automatically moves it to the "Done" column

3. **Benefits**:
   - Keeps the Kanban board clean
   - Prevents completed tasks from accumulating in the review column
   - Reduces manual effort for moving completed cards
   - Ensures completed work is properly tracked

### Configuration
The auto-cleanup feature can be configured by:
- Adjusting the minimum review time (currently 1 hour)
- Enabling/disabling auto-fix functionality
- Configuring the kanban-config.json file

### Example
When a card is in review with:
- No errors
- No issues
- Not blocked
- Has been in review for more than 1 hour

The agent will automatically move it to the "Done" column during the next scan.

## Integration with Existing Kanban System

The agent integrates with the existing Kanban system by:

1. Reading from `config/kanban-config.json` for configuration
2. Checking `config/rejected-improvements.json` for cards pending review
3. Updating the rejected improvements list when moving cards to rejected
4. Creating fix cards in the Kanban system for issues found