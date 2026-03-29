package trainingconfig

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/url"
	"os"
	"sort"
	"strings"
	"time"
)

// HFDatasetsServerBase returns the REST root for Hugging Face dataset viewer (override in tests via QMW_HF_DATASETS_SERVER_BASE).
func HFDatasetsServerBase() string {
	b := strings.TrimSpace(os.Getenv("QMW_HF_DATASETS_SERVER_BASE"))
	if b == "" {
		return "https://datasets-server.huggingface.co"
	}
	return strings.TrimRight(b, "/")
}

// ResolveHFDefaultConfigName calls GET /info?dataset=… and picks a config name when TOML omits dataset_config.
func ResolveHFDefaultConfigName(ctx context.Context, datasetID string) (string, error) {
	datasetID = strings.TrimSpace(datasetID)
	if datasetID == "" {
		return "", fmt.Errorf("empty dataset id")
	}
	u := HFDatasetsServerBase() + "/info?dataset=" + url.QueryEscape(datasetID)
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, u, nil)
	if err != nil {
		return "", err
	}
	req.Header.Set("Accept", "application/json")
	req.Header.Set("User-Agent", "qminiwasm-training-wui/1.0 (+native-cascade)")
	cli := &http.Client{Timeout: 30 * time.Second}
	resp, err := cli.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("HF info HTTP %d", resp.StatusCode)
	}
	var top map[string]json.RawMessage
	if err := json.NewDecoder(resp.Body).Decode(&top); err != nil {
		return "", err
	}
	if rawErr, ok := top["error"]; ok {
		var es string
		if json.Unmarshal(rawErr, &es) == nil && strings.TrimSpace(es) != "" {
			return "", fmt.Errorf("HF info: %s", es)
		}
		if len(rawErr) > 0 && string(rawErr) != "null" {
			return "", fmt.Errorf("HF info: %s", string(rawErr))
		}
	}
	rawDi, ok := top["dataset_info"]
	if !ok {
		return "", fmt.Errorf("HF info: missing dataset_info")
	}
	var configs map[string]json.RawMessage
	if err := json.Unmarshal(rawDi, &configs); err != nil {
		return "", fmt.Errorf("dataset_info: %w", err)
	}
	if len(configs) == 0 {
		return "", fmt.Errorf("HF info: no configs")
	}
	if _, ok := configs["all"]; ok {
		return "all", nil
	}
	if _, ok := configs["default"]; ok {
		return "default", nil
	}
	keys := make([]string, 0, len(configs))
	for k := range configs {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys[0], nil
}
