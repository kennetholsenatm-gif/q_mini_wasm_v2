// Command qmw-serve exposes GET /health and POST /infer (stub) for trained checkpoints (Go runtime).
package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"strings"

	toml "github.com/pelletier/go-toml/v2"
)

func main() {
	listen := flag.String("listen", "127.0.0.1:8001", "HTTP listen address")
	config := flag.String("config", "", "absolute path to serve.toml ([serve] checkpoint)")
	flag.Parse()
	cfg := strings.TrimSpace(*config)
	if cfg == "" {
		cfg = strings.TrimSpace(os.Getenv("QMINIWASM_SERVE_CONFIG"))
	}
	if cfg == "" {
		log.Fatal("-config or QMINIWASM_SERVE_CONFIG is required")
	}
	raw, err := os.ReadFile(cfg)
	if err != nil {
		log.Fatal(err)
	}
	var doc struct {
		Serve struct {
			Checkpoint string `toml:"checkpoint"`
		} `toml:"serve"`
	}
	if err := toml.Unmarshal(raw, &doc); err != nil {
		log.Fatal(err)
	}
	ck := strings.TrimSpace(doc.Serve.Checkpoint)
	mux := http.NewServeMux()
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprintf(w, `{"status":"ok","runtime":"go","checkpoint":%q}`+"\n", ck)
	})
	mux.HandleFunc("/infer", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusNotImplemented)
		fmt.Fprintf(w, `{"detail":"LibTorch HTTP infer not implemented; use gRPC extension."}`+"\n")
	})
	log.Printf("qmw-serve listening on %s (checkpoint %q)", *listen, ck)
	log.Fatal(http.ListenAndServe(*listen, mux))
}
