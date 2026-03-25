package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strings"

	"github.com/pelletier/go-toml/v2"
)

type runpodServerlessTomlSection struct {
	WorkerImage string `toml:"worker_image"`
}

type trainingTomlRunpodStub struct {
	RunpodServerless *runpodServerlessTomlSection `toml:"runpod_serverless"`
}

func workerImageFromTrainingConfigAbs(abs string) (image string, source string, err error) {
	raw, err := os.ReadFile(abs)
	if err != nil {
		return "", "", err
	}
	var doc trainingTomlRunpodStub
	if err := toml.Unmarshal(raw, &doc); err != nil {
		return "", "", fmt.Errorf("parse training toml: %w", err)
	}
	if doc.RunpodServerless != nil {
		if w := strings.TrimSpace(doc.RunpodServerless.WorkerImage); w != "" {
			return w, "toml", nil
		}
	}
	return runpodServerlessBuiltinWorkerImage, "builtin", nil
}

func handleRunpodServerlessWorkerImage(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	configRel := strings.TrimSpace(r.URL.Query().Get("config"))
	if configRel == "" {
		jsonErr(w, http.StatusBadRequest, "missing config query parameter")
		return
	}
	abs, err := resolveTrainingConfig(repoRoot, configRel)
	if err != nil {
		jsonErr(w, http.StatusBadRequest, err.Error())
		return
	}
	img, src, err := workerImageFromTrainingConfigAbs(abs)
	if err != nil {
		jsonErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok":     true,
		"image":  img,
		"source": src,
	})
}
