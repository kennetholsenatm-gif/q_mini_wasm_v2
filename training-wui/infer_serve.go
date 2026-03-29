package main

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"

	toml "github.com/pelletier/go-toml/v2"
)

type serveTomlDoc struct {
	Serve struct {
		Checkpoint string `toml:"checkpoint"`
	} `toml:"serve"`
}

func parseServeTomlCheckpoint(serveAbs string) (checkpointRel string, err error) {
	raw, err := os.ReadFile(serveAbs)
	if err != nil {
		return "", err
	}
	var doc serveTomlDoc
	if err := toml.Unmarshal(raw, &doc); err != nil {
		return "", err
	}
	return strings.TrimSpace(doc.Serve.Checkpoint), nil
}

// inferHTTPHandler returns GET /health and POST /infer (501 until C++ tensor bridge exists).
func inferHTTPHandler(stem, checkpointRel string) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"status":     "ok",
			"runtime":    "go",
			"model_stem": stem,
			"checkpoint": checkpointRel,
		})
	})
	mux.HandleFunc("/infer", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusNotImplemented)
		_ = json.NewEncoder(w).Encode(map[string]any{
			"detail": "LibTorch tensor inference over HTTP is not implemented yet; extend cpp/training with an Infer RPC and bridge here.",
		})
	})
	return mux
}

// embeddedInferServer is a local net/http.Server started by POST /api/serve/start.
type embeddedInferServer struct {
	mu   sync.Mutex
	srv  *http.Server
	ln   net.Listener
	stem string
	ckpt string
}

func (e *embeddedInferServer) start(port int, stem, serveTomlAbs string) error {
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.srv != nil {
		return fmt.Errorf("infer server already running")
	}
	ckRel, err := parseServeTomlCheckpoint(serveTomlAbs)
	if err != nil {
		return err
	}
	addr := fmt.Sprintf("127.0.0.1:%d", port)
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return err
	}
	srv := &http.Server{
		Handler:      inferHTTPHandler(stem, ckRel),
		ReadTimeout:  30 * time.Second,
		WriteTimeout: 120 * time.Second,
	}
	e.srv = srv
	e.ln = ln
	e.stem = stem
	e.ckpt = ckRel
	go func() { _ = srv.Serve(ln) }()
	return nil
}

func (e *embeddedInferServer) stop() error {
	e.mu.Lock()
	srv := e.srv
	e.srv = nil
	e.ln = nil
	e.mu.Unlock()
	if srv == nil {
		return nil
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	return srv.Shutdown(ctx)
}
