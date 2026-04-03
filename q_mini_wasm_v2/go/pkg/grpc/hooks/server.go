// Package hooks implements gRPC hook services
package hooks

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"os/exec"
	"strings"
	"sync"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Server implements the HookService gRPC server
type Server struct {
	UnimplementedHookServiceServer
	configPath string
	mu         sync.RWMutex
	config     *HookConfiguration
}

// HookConfiguration represents the hooks configuration
type HookConfiguration struct {
	GitHooks     GitHooksConfig     `json:"git_hooks"`
	QuantumHooks QuantumHooksConfig `json:"quantum_specific_hooks"`
}

type GitHooksConfig struct {
	PreCommit  HookConfig `json:"pre_commit"`
	CommitMsg  HookConfig `json:"commit_msg"`
	PrePush    HookConfig `json:"pre_push"`
	PostCommit HookConfig `json:"post_commit"`
	PostMerge  HookConfig `json:"post_merge"`
}

type QuantumHooksConfig struct {
	StabilizerCorrectness  HookConfig `json:"stabilizer_correctness"`
	CliffordGateValidation HookConfig `json:"clifford_gate_validation"`
	EnergyEfficiency       HookConfig `json:"energy_efficiency"`
}

type HookConfig struct {
	Enabled bool         `json:"enabled"`
	Checks  []HookCheck  `json:"checks,omitempty"`
	Actions []HookAction `json:"actions,omitempty"`
}

type HookCheck struct {
	Name        string `json:"name"`
	Command     string `json:"command"`
	Description string `json:"description"`
	Required    bool   `json:"required"`
}

type HookAction struct {
	Name        string `json:"name"`
	Command     string `json:"command"`
	Description string `json:"description"`
	Required    bool   `json:"required"`
}

// NewServer creates a new HookService server
func NewServer(configPath string) *Server {
	server := &Server{
		configPath: configPath,
	}
	
	// Load configuration
	if err := server.loadConfig(); err != nil {
		log.Printf("Warning: Failed to load hooks config: %v", err)
	}
	
	return server
}

// loadConfig loads the hook configuration from JSON
func (s *Server) loadConfig() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	data, err := os.ReadFile(s.configPath)
	if err != nil {
		return fmt.Errorf("failed to read config: %w", err)
	}
	
	var config HookConfiguration
	if err := json.Unmarshal(data, &config); err != nil {
		return fmt.Errorf("failed to parse config: %w", err)
	}
	
	s.config = &config
	return nil
}

// ExecutePreCommit runs pre-commit checks
func (s *Server) ExecutePreCommit(ctx context.Context, req *PreCommitRequest) (*HookResponse, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.config == nil || !s.config.GitHooks.PreCommit.Enabled {
		return &HookResponse{
			Success: true,
			Message: "Pre-commit hook disabled",
		}, nil
	}
	
	var errors []string
	var warnings []string
	
	for _, check := range s.config.GitHooks.PreCommit.Checks {
		select {
		case <-ctx.Done():
			return nil, status.Error(codes.Canceled, "context canceled")
		default:
		}
		
		cmd := exec.CommandContext(ctx, "sh", "-c", check.Command)
		cmd.Dir = req.RepoPath
		
		output, err := cmd.CombinedOutput()
		if err != nil {
			if check.Required {
				errors = append(errors, fmt.Sprintf("%s: %s", check.Name, string(output)))
			} else {
				warnings = append(warnings, fmt.Sprintf("%s: %s", check.Name, string(output)))
			}
		}
	}
	
	success := len(errors) == 0
	message := "Pre-commit checks passed"
	if !success {
		message = "Pre-commit checks failed"
	}
	
	return &HookResponse{
		Success:  success,
		Message:  message,
		Errors:   errors,
		Warnings: warnings,
	}, nil
}

// ValidateCommitMessage validates commit message format
func (s *Server) ValidateCommitMessage(ctx context.Context, req *CommitMessageRequest) (*HookResponse, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.config == nil || !s.config.GitHooks.CommitMsg.Enabled {
		return &HookResponse{
			Success: true,
			Message: "Commit message validation disabled",
		}, nil
	}
	
	// Validate format
	prefixes := []string{"feat", "fix", "docs", "style", "refactor", "test", "chore", "quantum"}
	
	validPrefix := false
	for _, prefix := range prefixes {
		if strings.HasPrefix(req.Message, prefix+"(") {
			validPrefix = true
			break
		}
	}
	
	if !validPrefix {
		return &HookResponse{
			Success: false,
			Message: "Invalid commit message format",
			Errors:  []string{"Commit message must start with: feat|fix|docs|style|refactor|test|chore|quantum"},
		}, nil
	}
	
	return &HookResponse{
		Success: true,
		Message: "Commit message format valid",
	}, nil
}

// ExecutePrePush runs pre-push checks
func (s *Server) ExecutePrePush(ctx context.Context, req *PrePushRequest) (*HookResponse, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.config == nil || !s.config.GitHooks.PrePush.Enabled {
		return &HookResponse{
			Success: true,
			Message: "Pre-push hook disabled",
		}, nil
	}
	
	var errors []string
	
	for _, check := range s.config.GitHooks.PrePush.Checks {
		select {
		case <-ctx.Done():
			return nil, status.Error(codes.Canceled, "context canceled")
		default:
		}
		
		cmd := exec.CommandContext(ctx, "sh", "-c", check.Command)
		cmd.Dir = req.RepoPath
		
		output, err := cmd.CombinedOutput()
		if err != nil && check.Required {
			errors = append(errors, fmt.Sprintf("%s: %s", check.Name, string(output)))
		}
	}
	
	return &HookResponse{
		Success: len(errors) == 0,
		Message: "Pre-push checks completed",
		Errors:  errors,
	}, nil
}

// ExecutePostCommit runs post-commit actions
func (s *Server) ExecutePostCommit(ctx context.Context, req *PostCommitRequest) (*HookResponse, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.config == nil || !s.config.GitHooks.PostCommit.Enabled {
		return &HookResponse{
			Success: true,
			Message: "Post-commit hook disabled",
		}, nil
	}
	
	for _, action := range s.config.GitHooks.PostCommit.Actions {
		select {
		case <-ctx.Done():
			return nil, status.Error(codes.Canceled, "context canceled")
		default:
		}
		
		cmd := exec.CommandContext(ctx, "sh", "-c", action.Command)
		cmd.Dir = req.RepoPath
		
		if err := cmd.Run(); err != nil {
			log.Printf("Post-commit action %s failed: %v", action.Name, err)
		}
	}
	
	return &HookResponse{
		Success: true,
		Message: "Post-commit actions completed",
	}, nil
}

// ExecutePostMerge runs post-merge actions
func (s *Server) ExecutePostMerge(ctx context.Context, req *PostMergeRequest) (*HookResponse, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.config == nil || !s.config.GitHooks.PostMerge.Enabled {
		return &HookResponse{
			Success: true,
			Message: "Post-merge hook disabled",
		}, nil
	}
	
	for _, action := range s.config.GitHooks.PostMerge.Actions {
		select {
		case <-ctx.Done():
			return nil, status.Error(codes.Canceled, "context canceled")
		default:
		}
		
		cmd := exec.CommandContext(ctx, "sh", "-c", action.Command)
		cmd.Dir = req.RepoPath
		
		if err := cmd.Run(); err != nil {
			log.Printf("Post-merge action %s failed: %v", action.Name, err)
		}
	}
	
	return &HookResponse{
		Success: true,
		Message: "Post-merge actions completed",
	}, nil
}

// GetHookConfig returns the current hook configuration
func (s *Server) GetHookConfig(ctx context.Context, req *HookConfigRequest) (*HookConfigResponse, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	if s.config == nil {
		return nil, status.Error(codes.NotFound, "configuration not loaded")
	}
	
	var config HookConfig
	switch req.HookType {
	case "pre_commit":
		config = s.config.GitHooks.PreCommit
	case "pre_push":
		config = s.config.GitHooks.PrePush
	case "post_commit":
		config = s.config.GitHooks.PostCommit
	case "post_merge":
		config = s.config.GitHooks.PostMerge
	default:
		return nil, status.Errorf(codes.InvalidArgument, "unknown hook type: %s", req.HookType)
	}
	
	return &HookConfigResponse{
		Enabled: config.Enabled,
		Checks:  convertChecksToProto(config.Checks),
		Actions: convertActionsToProto(config.Actions),
	}, nil
}

// UpdateHookConfig updates the hook configuration
func (s *Server) UpdateHookConfig(ctx context.Context, req *UpdateHookConfigRequest) (*HookResponse, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	if s.config == nil {
		return nil, status.Error(codes.NotFound, "configuration not loaded")
	}
	
	switch req.HookType {
	case "pre_commit":
		s.config.GitHooks.PreCommit.Enabled = req.Enabled
	case "pre_push":
		s.config.GitHooks.PrePush.Enabled = req.Enabled
	case "post_commit":
		s.config.GitHooks.PostCommit.Enabled = req.Enabled
	case "post_merge":
		s.config.GitHooks.PostMerge.Enabled = req.Enabled
	default:
		return nil, status.Errorf(codes.InvalidArgument, "unknown hook type: %s", req.HookType)
	}
	
	// Save configuration back to file
	data, err := json.MarshalIndent(s.config, "", "  ")
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to marshal config: %v", err)
	}
	
	if err := os.WriteFile(s.configPath, data, 0644); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to write config: %v", err)
	}
	
	return &HookResponse{
		Success: true,
		Message: "Hook configuration updated",
	}, nil
}

// Helper functions
func convertChecksToProto(checks []HookCheck) []*HookCheck {
	protoChecks := make([]*HookCheck, len(checks))
	for i, check := range checks {
		protoChecks[i] = &HookCheck{
			Name:        check.Name,
			Command:     check.Command,
			Description: check.Description,
			Required:    check.Required,
		}
	}
	return protoChecks
}

func convertActionsToProto(actions []HookAction) []*HookAction {
	protoActions := make([]*HookAction, len(actions))
	for i, action := range actions {
		protoActions[i] = &HookAction{
			Name:        action.Name,
			Command:     action.Command,
			Description: action.Description,
			Required:    action.Required,
		}
	}
	return protoActions
}

// RegisterServer registers the hook service with a gRPC server
func RegisterServer(grpcServer *grpc.Server, configPath string) {
	hookServer := NewServer(configPath)
	RegisterHookServiceServer(grpcServer, hookServer)
}
