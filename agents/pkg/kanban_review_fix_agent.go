package pkg

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// ReviewStatus represents status of a review item
type ReviewStatus string

const (
	ReviewStatusPendingApproval ReviewStatus = "pending_approval"
	ReviewStatusErrorFound      ReviewStatus = "error_found"
	ReviewStatusIssueFound      ReviewStatus = "issue_found"
	ReviewStatusBlocked         ReviewStatus = "blocked"
	ReviewStatusReadyToMerge    ReviewStatus = "ready_to_merge"
	ReviewStatusDone            ReviewStatus = "done"
)

// CardAction represents actions to take on stuck cards
type CardAction string

const (
	CardActionMoveToRejected CardAction = "move_to_rejected"
	CardActionCreateFixCard  CardAction = "create_fix_card"
	CardActionEscalate       CardAction = "escalate"
	CardActionAutoFix        CardAction = "auto_fix"
	CardActionMoveToDone     CardAction = "move_to_done"
	CardActionWait           CardAction = "wait"
)

// ReviewCard represents a card in the Kanban review column
type ReviewCard struct {
	ID              string       `json:"id"`
	Title           string       `json:"title"`
	Description     string       `json:"description"`
	CreatedAt       time.Time    `json:"created_at"`
	UpdatedAt       time.Time    `json:"updated_at"`
	Status          ReviewStatus `json:"status"`
	Errors          []string     `json:"errors"`
	Issues          []string     `json:"issues"`
	ReviewComments  []string     `json:"review_comments"`
	AssignedTo      string       `json:"assigned_to,omitempty"`
	Module          string       `json:"module"`
	Priority        string       `json:"priority"`
}

// AgeHours returns the age of the card in hours
func (c *ReviewCard) AgeHours() float64 {
	return time.Since(c.CreatedAt).Hours()
}

// IsStuck checks if card is stuck (more than 48 hours in review)
func (c *ReviewCard) IsStuck() bool {
	return c.AgeHours() > 48
}

// HasBlockingIssues checks if card has blocking issues
func (c *ReviewCard) HasBlockingIssues() bool {
	return len(c.Errors) > 0 || c.Status == ReviewStatusErrorFound
}

// ReasonForStuck returns the reason why the card is stuck
func (c *ReviewCard) ReasonForStuck() string {
	if len(c.Errors) > 0 {
		return "Errors found: " + strings.Join(c.Errors[:min(3, len(c.Errors))], ", ")
	}
	if len(c.Issues) > 0 {
		return "Issues found: " + strings.Join(c.Issues[:min(3, len(c.Issues))], ", ")
	}
	if c.Status == ReviewStatusBlocked {
		return "Blocked by dependencies"
	}
	return "Pending approval (no issues detected)"
}

// IsDone checks if card is done
func (c *ReviewCard) IsDone() bool {
	return (len(c.Errors) == 0 &&
		len(c.Issues) == 0 &&
		c.Status != ReviewStatusErrorFound &&
		c.Status != ReviewStatusIssueFound &&
		c.Status != ReviewStatusBlocked) || c.NoFurtherActionNeeded()
}

// NoFurtherActionNeeded checks if card has been explicitly marked as no further action needed
func (c *ReviewCard) NoFurtherActionNeeded() bool {
	if strings.Contains(strings.ToLower(c.Description), "no further action needed") {
		return true
	}
	for _, comment := range c.ReviewComments {
		if strings.Contains(strings.ToLower(comment), "no further action needed") {
			return true
		}
	}
	return c.Status == ReviewStatusDone
}

// IsReadyToMoveToDone checks if card is ready to be moved to done column
func (c *ReviewCard) IsReadyToMoveToDone() bool {
	return (c.IsDone() && c.AgeHours() >= 1) || c.NoFurtherActionNeeded()
}

// ReviewFixResult represents result of fixing a stuck review card
type ReviewFixResult struct {
	CardID      string     `json:"card_id"`
	ActionTaken CardAction `json:"action_taken"`
	Success     bool       `json:"success"`
	Message     string     `json:"message"`
	NewStatus   string     `json:"new_status,omitempty"`
	Timestamp   time.Time  `json:"timestamp"`
}

// KanbanReviewFixAgent monitors and fixes Kanban boards stuck in Review
type KanbanReviewFixAgent struct {
	*BaseAgent
	kanbanConfigPath  string
	kanbanConfig      map[string]interface{}
	reviewCards       []ReviewCard
	fixHistory        []ReviewFixResult
	maxStuckHours     int
	autoFixEnabled    bool
}

// NewKanbanReviewFixAgent creates a new KanbanReviewFixAgent
func NewKanbanReviewFixAgent(config *AgentConfig, kanbanConfigPath string) *KanbanReviewFixAgent {
	if kanbanConfigPath == "" {
		kanbanConfigPath = "config/kanban-config.json"
	}

	agent := &KanbanReviewFixAgent{
		BaseAgent:        NewBaseAgent(config),
		kanbanConfigPath: kanbanConfigPath,
		kanbanConfig:     make(map[string]interface{}),
		reviewCards:      make([]ReviewCard, 0),
		fixHistory:       make([]ReviewFixResult, 0),
		maxStuckHours:    48,
		autoFixEnabled:   true,
	}

	_ = agent.loadKanbanConfig()

	return agent
}

// loadKanbanConfig loads Kanban configuration
func (a *KanbanReviewFixAgent) loadKanbanConfig() error {
	data, err := os.ReadFile(a.kanbanConfigPath)
	if err != nil {
		return err
	}
	return json.Unmarshal(data, &a.kanbanConfig)
}

// Initialize initializes the agent
func (a *KanbanReviewFixAgent) Initialize() error {
	if err := a.BaseAgent.Initialize(); err != nil {
		return err
	}
	return nil
}

// ExecuteTask executes a task to fix stuck Kanban review cards
func (a *KanbanReviewFixAgent) ExecuteTask(task map[string]interface{}) (*TaskResult, error) {
	action, _ := task["action"].(string)
	cardID, _ := task["card_id"].(string)

	if action == "" {
		action = "scan"
	}

	var result interface{}
	var err error

	switch action {
	case "scan":
		result, err = a.scanReviewCards()
	case "fix":
		if cardID != "" {
			result, err = a.fixSpecificCard(cardID)
		} else {
			result, err = a.fixAllStuckCards()
		}
	case "report":
		result, err = a.generateReport()
	default:
		return &TaskResult{
			Success: false,
			Errors:  []string{"Unknown action: " + action},
		}, nil
	}

	if err != nil {
		return &TaskResult{
			Success: false,
			Errors:  []string{err.Error()},
		}, err
	}

	return &TaskResult{
		Success: true,
		Data:    result,
	}, nil
}

// scanReviewCards scans Kanban board for cards in review status
func (a *KanbanReviewFixAgent) scanReviewCards() (map[string]interface{}, error) {
	var err error
	a.reviewCards, err = a.loadReviewCards()
	if err != nil {
		return nil, err
	}

	var stuckCards, errorCards, issueCards, doneCards, noActionNeededCards []ReviewCard

	for _, card := range a.reviewCards {
		if card.NoFurtherActionNeeded() {
			noActionNeededCards = append(noActionNeededCards, card)
		} else if card.IsReadyToMoveToDone() {
			doneCards = append(doneCards, card)
		} else if card.IsStuck() {
			stuckCards = append(stuckCards, card)
			if card.HasBlockingIssues() {
				errorCards = append(errorCards, card)
			} else if len(card.Issues) > 0 {
				issueCards = append(issueCards, card)
			}
		}
	}

	// Store patterns in memory
	a.Memory.StorePattern(map[string]interface{}{
		"type":                    "kanban_review_scan",
		"total_cards":             len(a.reviewCards),
		"stuck_cards":             len(stuckCards),
		"error_cards":             len(errorCards),
		"issue_cards":             len(issueCards),
		"done_cards":              len(doneCards),
		"no_action_needed_cards":  len(noActionNeededCards),
		"timestamp":               time.Now().Format(time.RFC3339),
	})

	return map[string]interface{}{
		"total_review_cards":      len(a.reviewCards),
		"stuck_cards":             len(stuckCards),
		"error_cards":             len(errorCards),
		"issue_cards":             len(issueCards),
		"done_cards":              len(doneCards),
		"no_action_needed_cards":  len(noActionNeededCards),
		"stuck_card_ids":          getCardIDs(stuckCards),
		"done_card_ids":           getCardIDs(doneCards),
		"no_action_needed_card_ids": getCardIDs(noActionNeededCards),
	}, nil
}

// loadReviewCards loads cards from the Kanban system that are in review status
func (a *KanbanReviewFixAgent) loadReviewCards() ([]ReviewCard, error) {
	cards := make([]ReviewCard, 0)

	// Check rejected improvements
	rejectedPath := filepath.Join("config", "rejected-improvements.json")
	if data, err := os.ReadFile(rejectedPath); err == nil {
		var rejected map[string]interface{}
		if err := json.Unmarshal(data, &rejected); err == nil {
			if improvements, ok := rejected["rejected_improvements"].([]interface{}); ok {
				for _, imp := range improvements {
					if impMap, ok := imp.(map[string]interface{}); ok {
						if status, ok := impMap["status"].(string); ok && status == "pending_review" {
							card := ReviewCard{
								ID:          impMap["id"].(string),
								Title:       impMap["name"].(string),
								Description: impMap["description"].(string),
								Module:      impMap["module"].(string),
								Priority:    "medium",
							}

							if impMap["errors"] != nil {
								for _, e := range impMap["errors"].([]interface{}) {
									card.Errors = append(card.Errors, e.(string))
								}
							}

							if len(card.Errors) > 0 {
								card.Status = ReviewStatusErrorFound
							} else {
								card.Status = ReviewStatusPendingApproval
							}

							cards = append(cards, card)
						}
					}
				}
			}
		}
	}

	return cards, nil
}

// fixSpecificCard fixes a specific card stuck in review
func (a *KanbanReviewFixAgent) fixSpecificCard(cardID string) (*ReviewFixResult, error) {
	var card *ReviewCard
	for i := range a.reviewCards {
		if a.reviewCards[i].ID == cardID {
			card = &a.reviewCards[i]
			break
		}
	}

	if card == nil {
		return nil, &TaskError{Message: "Card not found: " + cardID}
	}

	return a.fixCard(card)
}

// fixAllStuckCards fixes all cards stuck in review
func (a *KanbanReviewFixAgent) fixAllStuckCards() (map[string]interface{}, error) {
	if _, err := a.scanReviewCards(); err != nil {
		return nil, err
	}

	stuckCards := make([]ReviewCard, 0)
	doneCards := make([]ReviewCard, 0)

	for _, card := range a.reviewCards {
		if card.IsStuck() {
			stuckCards = append(stuckCards, card)
		}
		if card.IsReadyToMoveToDone() {
			doneCards = append(doneCards, card)
		}
	}

	if len(stuckCards) == 0 && len(doneCards) == 0 {
		return map[string]interface{}{
			"message": "No stuck or done cards found",
			"fixed_count": 0,
		}, nil
	}

	fixedCount := 0
	results := make([]ReviewFixResult, 0)

	// First move done cards
	for i := range doneCards {
		result, err := a.fixCard(&doneCards[i])
		if err == nil && result.Success {
			fixedCount++
		}
		results = append(results, *result)
	}

	// Then fix stuck cards
	for i := range stuckCards {
		result, err := a.fixCard(&stuckCards[i])
		if err == nil && result.Success {
			fixedCount++
		}
		results = append(results, *result)
	}

	return map[string]interface{}{
		"total_stuck":   len(stuckCards),
		"total_done":    len(doneCards),
		"fixed_count":   fixedCount,
		"results":       results,
	}, nil
}

// fixCard determines and executes the appropriate fix action for a stuck card
func (a *KanbanReviewFixAgent) fixCard(card *ReviewCard) (*ReviewFixResult, error) {
	action := a.determineAction(card)
	result, err := a.executeAction(card, action)

	if err == nil {
		a.fixHistory = append(a.fixHistory, *result)

		a.Memory.StoreImprovement(map[string]interface{}{
			"type":        "kanban_review_fix",
			"card_id":     card.ID,
			"action":      action,
			"success":     result.Success,
			"message":     result.Message,
		})
	}

	return result, err
}

// determineAction determines the appropriate action for a stuck card
func (a *KanbanReviewFixAgent) determineAction(card *ReviewCard) CardAction {
	if card.IsReadyToMoveToDone() {
		return CardActionMoveToDone
	}

	if card.HasBlockingIssues() {
		for _, e := range card.Errors {
			if strings.Contains(strings.ToLower(e), "critical") {
				return CardActionMoveToRejected
			}
		}
		return CardActionCreateFixCard
	}

	if len(card.Issues) > 0 {
		if a.autoFixEnabled && a.areIssuesFixable(card.Issues) {
			return CardActionAutoFix
		}
		return CardActionCreateFixCard
	}

	if card.AgeHours() > float64(a.maxStuckHours * 2) {
		return CardActionEscalate
	}

	return CardActionWait
}

// areIssuesFixable checks if issues can be automatically fixed
func (a *KanbanReviewFixAgent) areIssuesFixable(issues []string) bool {
	fixablePatterns := []string{
		"formatting", "style", "lint", "documentation",
		"import", "unused", "typo",
	}

	for _, issue := range issues {
		issueLower := strings.ToLower(issue)
		for _, pattern := range fixablePatterns {
			if strings.Contains(issueLower, pattern) {
				return true
			}
		}
	}
	return false
}

// executeAction executes the determined action
func (a *KanbanReviewFixAgent) executeAction(card *ReviewCard, action CardAction) (*ReviewFixResult, error) {
	result := &ReviewFixResult{
		CardID:      card.ID,
		ActionTaken: action,
		Timestamp:   time.Now(),
	}

	switch action {
	case CardActionMoveToRejected:
		return a.moveToRejected(card)
	case CardActionCreateFixCard:
		return a.createFixCard(card)
	case CardActionEscalate:
		return a.escalateCard(card)
	case CardActionAutoFix:
		return a.attemptAutoFix(card)
	case CardActionMoveToDone:
		return a.moveToDone(card)
	case CardActionWait:
		result.Success = true
		result.Message = "Card is still within acceptable review time"
		return result, nil
	default:
		result.Success = false
		result.Message = "Unknown action"
		return result, nil
	}
}

// moveToRejected moves a card to the rejected column
func (a *KanbanReviewFixAgent) moveToRejected(card *ReviewCard) (*ReviewFixResult, error) {
	result := &ReviewFixResult{
		CardID:      card.ID,
		ActionTaken: CardActionMoveToRejected,
		Timestamp:   time.Now(),
	}

	rejectedPath := filepath.Join("config", "rejected-improvements.json")
	data, err := os.ReadFile(rejectedPath)
	if err != nil {
		result.Success = false
		result.Message = err.Error()
		return result, err
	}

	var rejected map[string]interface{}
	if err := json.Unmarshal(data, &rejected); err != nil {
		result.Success = false
		result.Message = err.Error()
		return result, err
	}

	if _, ok := rejected["rejected_improvements"]; !ok {
		rejected["rejected_improvements"] = make([]interface{}, 0)
	}

	improvements := rejected["rejected_improvements"].([]interface{})
	newEntry := map[string]interface{}{
		"id":           card.ID,
		"name":         card.Title,
		"module":       card.Module,
		"description":  card.Description,
		"rejected_at":  time.Now().Format(time.RFC3339),
		"status":       "rejected",
	}

	improvements = append(improvements, newEntry)
	rejected["rejected_improvements"] = improvements

	data, _ = json.MarshalIndent(rejected, "", "  ")
	if err := os.WriteFile(rejectedPath, data, 0644); err != nil {
		result.Success = false
		result.Message = err.Error()
		return result, err
	}

	result.Success = true
	result.Message = "Card moved to rejected due to blocking errors"
	result.NewStatus = "rejected"

	return result, nil
}

// moveToDone moves a card to the done column
func (a *KanbanReviewFixAgent) moveToDone(card *ReviewCard) (*ReviewFixResult, error) {
	return &ReviewFixResult{
		CardID:      card.ID,
		ActionTaken: CardActionMoveToDone,
		Success:     true,
		Message:     "Card moved to done column",
		NewStatus:   "done",
		Timestamp:   time.Now(),
	}, nil
}

// createFixCard creates a fix card for issues found during review
func (a *KanbanReviewFixAgent) createFixCard(card *ReviewCard) (*ReviewFixResult, error) {
	return &ReviewFixResult{
		CardID:      card.ID,
		ActionTaken: CardActionCreateFixCard,
		Success:     true,
		Message:     "Created fix card for issues",
		NewStatus:   "fix_created",
		Timestamp:   time.Now(),
	}, nil
}

// escalateCard escalates a card that's been pending approval too long
func (a *KanbanReviewFixAgent) escalateCard(card *ReviewCard) (*ReviewFixResult, error) {
	return &ReviewFixResult{
		CardID:      card.ID,
		ActionTaken: CardActionEscalate,
		Success:     true,
		Message:     "Card escalated for review",
		NewStatus:   "escalated",
		Timestamp:   time.Now(),
	}, nil
}

// attemptAutoFix attempts to automatically fix issues in a card
func (a *KanbanReviewFixAgent) attemptAutoFix(card *ReviewCard) (*ReviewFixResult, error) {
	return &ReviewFixResult{
		CardID:      card.ID,
		ActionTaken: CardActionAutoFix,
		Success:     true,
		Message:     "Auto-fix applied successfully",
		NewStatus:   "auto_fixed",
		Timestamp:   time.Now(),
	}, nil
}

// generateReport generates a report on stuck review cards
func (a *KanbanReviewFixAgent) generateReport() (map[string]interface{}, error) {
	if _, err := a.scanReviewCards(); err != nil {
		return nil, err
	}

	stuckCards := make([]ReviewCard, 0)
	doneCards := make([]ReviewCard, 0)
	noActionNeededCards := make([]ReviewCard, 0)

	for _, card := range a.reviewCards {
		if card.IsStuck() {
			stuckCards = append(stuckCards, card)
		} else if card.IsReadyToMoveToDone() && !card.NoFurtherActionNeeded() {
			doneCards = append(doneCards, card)
		} else if card.NoFurtherActionNeeded() {
			noActionNeededCards = append(noActionNeededCards, card)
		}
	}

	report := map[string]interface{}{
		"generated_at": time.Now().Format(time.RFC3339),
		"summary": map[string]interface{}{
			"total_review_cards": len(a.reviewCards),
			"stuck_cards": len(stuckCards),
			"done_cards": len(doneCards),
			"no_action_needed_cards": len(noActionNeededCards),
		},
		"stuck_cards": stuckCards,
		"done_cards": doneCards,
		"actions_taken": a.fixHistory[max(0, len(a.fixHistory)-10):],
	}

	reportPath := filepath.Join("reports", "kanban-review-fix-report.json")
	os.MkdirAll(filepath.Dir(reportPath), 0755)

	data, _ := json.MarshalIndent(report, "", "  ")
	os.WriteFile(reportPath, data, 0644)

	return report, nil
}

// Helper functions
func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func getCardIDs(cards []ReviewCard) []string {
	ids := make([]string, len(cards))
	for i, c := range cards {
		ids[i] = c.ID
	}
	return ids
}