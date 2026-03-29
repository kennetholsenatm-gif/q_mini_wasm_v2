package main

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strings"
	"time"
)

const hfHubMaxInlineUploadBytes = 8 << 20 // below typical LFS threshold; avoids LFS preupload in Go

// hfHubCommitURL returns POST /api/models/{repo_id}/commit/{revision} (revision URL-quoted per Hub).
func hfHubCommitURL(repoID, revision string) (string, error) {
	rid := strings.Trim(strings.TrimSpace(repoID), "/")
	if rid == "" {
		return "", fmt.Errorf("empty repo id")
	}
	rev := strings.TrimSpace(revision)
	if rev == "" {
		rev = "main"
	}
	// Match huggingface_hub: quote(revision, safe="")
	qrev := url.PathEscape(rev)
	repoSegs := strings.Split(rid, "/")
	var escaped []string
	for _, s := range repoSegs {
		s = strings.TrimSpace(s)
		if s == "" {
			continue
		}
		escaped = append(escaped, url.PathEscape(s))
	}
	if len(escaped) == 0 {
		return "", fmt.Errorf("invalid repo id")
	}
	return fmt.Sprintf("https://huggingface.co/api/models/%s/commit/%s", strings.Join(escaped, "/"), qrev), nil
}

// huggingFaceUploadModelFile uploads a single file via the Hub NDJSON commit API (regular/base64 file entry).
// Large files that would require Git LFS are rejected with a clear error (use huggingface-cli for those).
func huggingFaceUploadModelFile(ctx context.Context, repoID, token, absLocal, pathInRepo string) error {
	token = strings.TrimSpace(token)
	if token == "" {
		token = strings.TrimSpace(os.Getenv("HF_TOKEN"))
	}
	if token == "" {
		token = strings.TrimSpace(os.Getenv("HUGGING_FACE_HUB_TOKEN"))
	}
	if token == "" {
		return fmt.Errorf("missing Hugging Face token (JSON body token, HF_TOKEN, or HUGGING_FACE_HUB_TOKEN)")
	}
	st, err := os.Stat(absLocal)
	if err != nil {
		return err
	}
	if st.IsDir() {
		return fmt.Errorf("not a file: %s", absLocal)
	}
	if st.Size() > hfHubMaxInlineUploadBytes {
		return fmt.Errorf("file size %d exceeds inline limit (%d bytes); use huggingface-cli upload for LFS-sized artifacts",
			st.Size(), hfHubMaxInlineUploadBytes)
	}
	raw, err := os.ReadFile(absLocal)
	if err != nil {
		return err
	}
	pathInRepo = strings.Trim(strings.ReplaceAll(pathInRepo, "\\", "/"), "/")
	if pathInRepo == "" {
		return fmt.Errorf("empty path_in_repo")
	}

	commitURL, err := hfHubCommitURL(repoID, "main")
	if err != nil {
		return err
	}

	headerObj := map[string]any{
		"summary":     fmt.Sprintf("Upload %s", pathInRepo),
		"description": "",
	}
	headerLine, err := json.Marshal(map[string]any{"key": "header", "value": headerObj})
	if err != nil {
		return err
	}
	fileVal := map[string]any{
		"content":  base64.StdEncoding.EncodeToString(raw),
		"path":     pathInRepo,
		"encoding": "base64",
	}
	fileLine, err := json.Marshal(map[string]any{"key": "file", "value": fileVal})
	if err != nil {
		return err
	}
	body := bytes.Join([][]byte{headerLine, fileLine}, []byte("\n"))
	body = append(body, '\n')

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, commitURL, bytes.NewReader(body))
	if err != nil {
		return err
	}
	req.Header.Set("Authorization", "Bearer "+token)
	req.Header.Set("Content-Type", "application/x-ndjson")
	req.Header.Set("User-Agent", "qminiwasm-training-wui/1.0")

	cli := &http.Client{Timeout: 5 * time.Minute}
	resp, err := cli.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	respBody, _ := io.ReadAll(io.LimitReader(resp.Body, 16384))
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("HF commit HTTP %d: %s", resp.StatusCode, strings.TrimSpace(string(respBody)))
	}
	return nil
}
