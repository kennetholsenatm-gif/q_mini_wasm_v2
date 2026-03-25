package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"time"
)

func (m *manager) finishServerlessRun(id string, exitCode int, isErr bool) {
	m.mu.Lock()
	if rec := m.byID[id]; rec != nil {
		rec.Running = false
		rec.ExitCode = exitCode
		rec.Error = isErr
		rec.ServerlessJobID = ""
		rec.ServerlessEndpointID = ""
	}
	m.mu.Unlock()
	wsHub.broadcast(id, map[string]any{
		"type":      "lifecycle",
		"run_id":    id,
		"state":     "finished",
		"ts":        time.Now().UTC().Format(time.RFC3339),
		"exit_code": exitCode,
	})
}

func (m *manager) pollServerlessJob(runID, endpointID, jobID, configCleanup string) {
	defer func() {
		if configCleanup != "" {
			_ = os.Remove(configCleanup)
		}
	}()
	deadline := time.Now().Add(168 * time.Hour)
	nextStatusLog := time.Time{}
	for {
		if time.Now().After(deadline) {
			m.appendLog(runID, []byte("\n=== runpod serverless: stopped polling after 7 days ===\n"), "stderr")
			m.finishServerlessRun(runID, 1, true)
			return
		}
		sctx, cancel := context.WithTimeout(context.Background(), 50*time.Second)
		out, code, raw, err := RunpodServerlessStatus(sctx, endpointID, jobID)
		cancel()
		if err != nil {
			m.appendLog(runID, []byte("\n=== runpod serverless status error: "+err.Error()+" ===\n"), "stderr")
			time.Sleep(5 * time.Second)
			continue
		}
		if code == 404 {
			m.appendLog(runID, []byte("\n=== runpod serverless: job not found (expired TTL or invalid id) ===\n"), "stderr")
			m.finishServerlessRun(runID, 1, true)
			return
		}
		if code >= 400 {
			msg := fmt.Sprintf("\n=== runpod serverless status HTTP %d: %s ===\n", code, string(raw))
			m.appendLog(runID, []byte(msg), "stderr")
			m.finishServerlessRun(runID, 1, true)
			return
		}
		st, _ := out["status"].(string)
		st = strings.ToUpper(strings.TrimSpace(st))
		if time.Now().After(nextStatusLog) {
			line := fmt.Sprintf("=== runpod serverless job %s status: %s ===\n", jobID, st)
			m.appendLog(runID, []byte(line), "stdout")
			nextStatusLog = time.Now().Add(15 * time.Second)
		}
		switch st {
		case "COMPLETED":
			ob, _ := json.MarshalIndent(out, "", "  ")
			m.appendLog(runID, []byte("\n=== runpod serverless job result ===\n"), "stdout")
			m.appendLog(runID, ob, "stdout")
			m.appendLog(runID, []byte("\n"), "stdout")
			m.finishServerlessRun(runID, 0, false)
			return
		case "FAILED", "ERROR", "CANCELLED", "TIMED_OUT":
			ob, _ := json.MarshalIndent(out, "", "  ")
			m.appendLog(runID, []byte("\n=== runpod serverless terminal: "+st+" ===\n"), "stderr")
			m.appendLog(runID, ob, "stderr")
			m.finishServerlessRun(runID, 1, true)
			return
		}
		time.Sleep(3 * time.Second)
	}
}
