// RunPod REST management API (Serverless templates + endpoints).
// Base: https://rest.runpod.io/v1 — endpoints and templates API reference on docs.runpod.io
package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"
)

const runpodManagementAPIBase = "https://rest.runpod.io/v1"

// GraphQL template create (Serverless): REST POST /templates rejects volumeInGb for serverless
// (including implicit pod defaults). RunPod documents saveTemplate on api.runpod.io with
// volumeInGb: 0, containerDiskInGb, dockerArgs, and env — see docs.runpod.io GraphQL manage templates.
const runpodGraphQLAPI = "https://api.runpod.io/graphql"

// runpodManagementAPIKeyMaterial returns the secret for Authorization, trimming space and a
// single leading "Bearer " if present (avoids "Bearer Bearer …" when .env already includes it).
func runpodManagementAPIKeyMaterial() string {
	k := strings.TrimSpace(runpodAccountAPIKeyForREST())
	if len(k) >= 7 && strings.EqualFold(k[:7], "bearer ") {
		k = strings.TrimSpace(k[7:])
	}
	return k
}

func runpodManagementShouldRetryRawAuth(method string, code int) bool {
	if code == http.StatusUnauthorized || code == http.StatusForbidden {
		return true
	}
	// Some accounts or gateways mis-classify key issues as 500; queue API uses raw key only.
	if method == http.MethodGet && code == http.StatusInternalServerError {
		return true
	}
	return false
}

// runpodManagementDoJSON calls the management API. Tries Bearer first (OpenAPI), then raw key
// like https://api.runpod.ai/v2 when the first response looks like an auth or ambiguous failure.
func runpodManagementDoJSON(ctx context.Context, method, path string, query url.Values, body any) (int, []byte, error) {
	key := runpodManagementAPIKeyMaterial()
	if key == "" {
		return 0, nil, errors.New("set RUNPOD_API_KEY or RUNPOD_TOKEN for rest.runpod.io (RUNPOD_TOKEN_END is endpoint-only)")
	}
	u, err := url.Parse(runpodManagementAPIBase + path)
	if err != nil {
		return 0, nil, err
	}
	if query != nil {
		u.RawQuery = query.Encode()
	}
	var bodyBytes []byte
	if body != nil {
		bodyBytes, err = json.Marshal(body)
		if err != nil {
			return 0, nil, err
		}
	}

	doOnce := func(authValue string) (int, []byte, error) {
		var rdr io.Reader
		if len(bodyBytes) > 0 {
			rdr = bytes.NewReader(bodyBytes)
		}
		req, err := http.NewRequestWithContext(ctx, method, u.String(), rdr)
		if err != nil {
			return 0, nil, err
		}
		if len(bodyBytes) > 0 {
			req.Header.Set("Content-Type", "application/json")
		}
		req.Header.Set("Accept", "application/json")
		req.Header.Set("Authorization", authValue)

		client := &http.Client{Timeout: 120 * time.Second}
		resp, err := client.Do(req)
		if err != nil {
			return 0, nil, err
		}
		defer resp.Body.Close()
		raw, _ := io.ReadAll(io.LimitReader(resp.Body, 16<<20))
		return resp.StatusCode, raw, nil
	}

	code, raw, err := doOnce("Bearer " + key)
	if err != nil {
		return code, raw, err
	}
	if runpodManagementShouldRetryRawAuth(method, code) {
		c2, r2, err2 := doOnce(key)
		if err2 == nil {
			return c2, r2, nil
		}
	}
	return code, raw, nil
}

// decodeEndpointsListJSON parses GET /endpoints success bodies: top-level array or common wrappers.
func decodeEndpointsListJSON(raw []byte) ([]map[string]any, error) {
	var list []map[string]any
	if err := json.Unmarshal(raw, &list); err == nil {
		return list, nil
	}
	var wrap map[string]json.RawMessage
	if err := json.Unmarshal(raw, &wrap); err != nil {
		return nil, fmt.Errorf("decode endpoints list: %w", err)
	}
	for _, key := range []string{"endpoints", "data", "items"} {
		if v, ok := wrap[key]; ok {
			if err := json.Unmarshal(v, &list); err == nil {
				return list, nil
			}
		}
	}
	return nil, errors.New("decode endpoints list: expected JSON array or object with endpoints/data/items")
}

func runpodManagementAPIErrorDetail(raw []byte) string {
	var m map[string]any
	if json.Unmarshal(raw, &m) != nil {
		return ""
	}
	for _, k := range []string{"message", "detail", "description"} {
		if v, ok := m[k]; ok && v != nil {
			s := strings.TrimSpace(fmt.Sprint(v))
			if s != "" {
				return s
			}
		}
	}
	if v, ok := m["error"]; ok && v != nil {
		if s, ok := v.(string); ok {
			if t := strings.TrimSpace(s); t != "" {
				return t
			}
		}
		if em, ok := v.(map[string]any); ok {
			for _, nk := range []string{"message", "msg", "detail"} {
				if v2, ok := em[nk]; ok && v2 != nil {
					if t := strings.TrimSpace(fmt.Sprint(v2)); t != "" {
						return t
					}
				}
			}
		}
	}
	return ""
}

func runpodManagementErrorMessage(code int, raw []byte) string {
	msg := fmt.Sprintf("RunPod management API HTTP %d", code)
	if d := runpodManagementAPIErrorDetail(raw); d != "" {
		return msg + ": " + d
	}
	s := strings.TrimSpace(string(raw))
	if len(s) == 0 {
		return msg
	}
	if len(s) <= 900 {
		return msg + ": " + s
	}
	return msg + ": " + s[:900] + "…"
}

// RunpodManagementListEndpoints GET /endpoints
func RunpodManagementListEndpoints(ctx context.Context, includeTemplate, includeWorkers bool) ([]map[string]any, int, []byte, error) {
	q := url.Values{}
	if includeTemplate {
		q.Set("includeTemplate", "true")
	}
	if includeWorkers {
		q.Set("includeWorkers", "true")
	}
	code, raw, err := runpodManagementDoJSON(ctx, http.MethodGet, "/endpoints", q, nil)
	if err != nil {
		return nil, code, raw, err
	}
	if code >= 400 {
		return nil, code, raw, nil
	}
	list, err := decodeEndpointsListJSON(raw)
	if err != nil {
		return nil, code, raw, err
	}
	return list, code, raw, nil
}

// RunpodManagementCreateEndpoint POST /endpoints
func RunpodManagementCreateEndpoint(ctx context.Context, body map[string]any) (map[string]any, int, []byte, error) {
	code, raw, err := runpodManagementDoJSON(ctx, http.MethodPost, "/endpoints", nil, body)
	if err != nil {
		return nil, code, raw, err
	}
	var out map[string]any
	if len(strings.TrimSpace(string(raw))) == 0 {
		out = map[string]any{}
	} else if err := json.Unmarshal(raw, &out); err != nil {
		if code < 400 {
			return nil, code, raw, fmt.Errorf("decode endpoint: %w", err)
		}
		out = map[string]any{"_non_json_body": string(raw)}
	}
	return out, code, raw, nil
}

// decodeTemplatesListJSON parses GET /templates success bodies: top-level array or common wrappers.
func decodeTemplatesListJSON(raw []byte) ([]map[string]any, error) {
	var list []map[string]any
	if err := json.Unmarshal(raw, &list); err == nil {
		return list, nil
	}
	var wrap map[string]json.RawMessage
	if err := json.Unmarshal(raw, &wrap); err != nil {
		return nil, fmt.Errorf("decode templates list: %w", err)
	}
	for _, key := range []string{"templates", "data", "items"} {
		if v, ok := wrap[key]; ok {
			if err := json.Unmarshal(v, &list); err == nil {
				return list, nil
			}
		}
	}
	return nil, errors.New("decode templates list: expected JSON array or object with templates/data/items")
}

// RunpodManagementListTemplates GET /templates — q may include includeEndpointBoundTemplates, includePublicTemplates, includeRunpodTemplates.
func RunpodManagementListTemplates(ctx context.Context, q url.Values) ([]map[string]any, int, []byte, error) {
	code, raw, err := runpodManagementDoJSON(ctx, http.MethodGet, "/templates", q, nil)
	if err != nil {
		return nil, code, raw, err
	}
	if code >= 400 {
		return nil, code, raw, nil
	}
	list, err := decodeTemplatesListJSON(raw)
	if err != nil {
		return nil, code, raw, err
	}
	return list, code, raw, nil
}

func runpodManagementTemplatesQueryFromRequest(r *http.Request) url.Values {
	q := url.Values{}
	for _, name := range []string{"includeEndpointBoundTemplates", "includePublicTemplates", "includeRunpodTemplates"} {
		v := strings.TrimSpace(r.URL.Query().Get(name))
		if v == "1" || strings.EqualFold(v, "true") {
			q.Set(name, "true")
		}
	}
	return q
}

func runpodServerlessEnvStringMap(body map[string]any) map[string]string {
	v, ok := body["env"]
	if !ok || v == nil {
		return nil
	}
	m, ok := v.(map[string]any)
	if !ok {
		return nil
	}
	out := make(map[string]string, len(m))
	for k, val := range m {
		if val == nil {
			continue
		}
		out[k] = fmt.Sprint(val)
	}
	if len(out) == 0 {
		return nil
	}
	return out
}

func runpodGraphQLServerlessEnvArray(body map[string]any) []map[string]any {
	m := runpodServerlessEnvStringMap(body)
	if len(m) == 0 {
		return []map[string]any{}
	}
	out := make([]map[string]any, 0, len(m))
	for k, v := range m {
		out = append(out, map[string]any{"key": k, "value": v})
	}
	return out
}

// runpodServerlessGraphQLSaveInput builds saveTemplate variables for Serverless (GraphQL API).
// Required per RunPod docs: containerDiskInGb, dockerArgs, env, imageName, name, volumeInGb (0).
func runpodServerlessGraphQLSaveInput(body map[string]any, name, imageName string) map[string]any {
	disk := 50
	if v, ok := body["containerDiskInGb"]; ok {
		switch t := v.(type) {
		case float64:
			disk = int(t)
		case int:
			disk = t
		case int64:
			disk = int(t)
		}
	}
	if disk < 1 {
		disk = 50
	}
	dockerArgs := "python /app/serverless/handler.py"
	if v, ok := body["dockerArgs"]; ok {
		if s := strings.TrimSpace(fmt.Sprint(v)); s != "" {
			dockerArgs = s
		}
	}
	in := map[string]any{
		"name":              name,
		"imageName":         imageName,
		"isServerless":      true,
		"volumeInGb":        0,
		"containerDiskInGb": disk,
		"dockerArgs":        dockerArgs,
		"env":               runpodGraphQLServerlessEnvArray(body),
	}
	if v, ok := body["readme"]; ok && v != nil {
		if s := strings.TrimSpace(fmt.Sprint(v)); s != "" {
			in["readme"] = s
		}
	}
	if v, ok := body["containerRegistryAuthId"]; ok && v != nil {
		if s := strings.TrimSpace(fmt.Sprint(v)); s != "" {
			in["containerRegistryAuthId"] = s
		}
	}
	return in
}

func runpodGraphQLPost(ctx context.Context, query string, variables map[string]any) (int, []byte, error) {
	key := runpodManagementAPIKeyMaterial()
	if key == "" {
		return 0, nil, errors.New("set RUNPOD_API_KEY or RUNPOD_TOKEN for api.runpod.io GraphQL")
	}
	u := runpodGraphQLAPI + "?api_key=" + url.QueryEscape(key)
	payload := map[string]any{"query": query}
	if len(variables) > 0 {
		payload["variables"] = variables
	}
	b, err := json.Marshal(payload)
	if err != nil {
		return 0, nil, err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, u, bytes.NewReader(b))
	if err != nil {
		return 0, nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "application/json")
	client := &http.Client{Timeout: 120 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return 0, nil, err
	}
	defer resp.Body.Close()
	raw, _ := io.ReadAll(io.LimitReader(resp.Body, 16<<20))
	return resp.StatusCode, raw, nil
}

func runpodGraphQLParseSaveTemplate(raw []byte) (map[string]any, error) {
	var env struct {
		Data *struct {
			SaveTemplate map[string]any `json:"saveTemplate"`
		} `json:"data"`
		Errors []struct {
			Message string `json:"message"`
		} `json:"errors"`
	}
	if err := json.Unmarshal(raw, &env); err != nil {
		return nil, fmt.Errorf("parse GraphQL JSON: %w", err)
	}
	if len(env.Errors) > 0 {
		return nil, errors.New(env.Errors[0].Message)
	}
	if env.Data == nil || env.Data.SaveTemplate == nil {
		return nil, errors.New("saveTemplate returned no template object")
	}
	return env.Data.SaveTemplate, nil
}

// RunpodGraphQLSaveServerlessTemplate creates a Serverless worker template via GraphQL saveTemplate.
// REST POST https://rest.runpod.io/v1/templates is not used for Serverless (volumeInGb handling).
func RunpodGraphQLSaveServerlessTemplate(ctx context.Context, body map[string]any, name, imageName string) (map[string]any, int, []byte, error) {
	input := runpodServerlessGraphQLSaveInput(body, name, imageName)
	// Input type name varies by schema version; try common variants.
	queries := []string{
		`mutation SaveQMWServerless($input: SaveTemplateInput!) { saveTemplate(input: $input) { id imageName name isServerless containerDiskInGb dockerArgs } }`,
		`mutation SaveQMWServerless($input: SavePodTemplateInput!) { saveTemplate(input: $input) { id imageName name isServerless containerDiskInGb dockerArgs } }`,
	}
	var lastRaw []byte
	var lastCode int
	var lastErr error
	for _, q := range queries {
		code, raw, err := runpodGraphQLPost(ctx, q, map[string]any{"input": input})
		lastRaw, lastCode = raw, code
		if err != nil {
			return nil, code, raw, err
		}
		tpl, gerr := runpodGraphQLParseSaveTemplate(raw)
		if gerr == nil {
			return tpl, code, raw, nil
		}
		lastErr = gerr
		if code >= 400 {
			break
		}
		// Unknown variable type: try next mutation string
		msg := strings.ToLower(gerr.Error())
		if !strings.Contains(msg, "unknown type") && !strings.Contains(msg, "variable") {
			break
		}
	}
	if lastErr == nil {
		lastErr = errors.New("saveTemplate failed")
	}
	return nil, lastCode, lastRaw, lastErr
}

func handleRunpodServerlessTemplates(w http.ResponseWriter, r *http.Request) {
	if runpodManagementAPIKeyMaterial() == "" {
		jsonErr(w, http.StatusBadRequest, "set RUNPOD_API_KEY or RUNPOD_TOKEN in .env for management API (endpoint key RUNPOD_TOKEN_END is not used here)")
		return
	}
	switch r.Method {
	case http.MethodGet:
		ctx, cancel := context.WithTimeout(r.Context(), 60*time.Second)
		defer cancel()
		q := runpodManagementTemplatesQueryFromRequest(r)
		list, code, raw, err := RunpodManagementListTemplates(ctx, q)
		if err != nil {
			jsonErr(w, http.StatusBadGateway, err.Error())
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.Header().Set("Cache-Control", "no-store")
		if code >= 400 {
			msg := runpodManagementErrorMessage(code, raw)
			w.WriteHeader(code)
			_ = json.NewEncoder(w).Encode(map[string]any{
				"ok":     false,
				"status": code,
				"raw":    string(raw),
				"error":  msg,
			})
			return
		}
		_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "templates": list})

	case http.MethodPost:
		var body map[string]any
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			jsonErr(w, http.StatusBadRequest, "invalid JSON")
			return
		}
		name := ""
		if v := body["name"]; v != nil {
			name = strings.TrimSpace(fmt.Sprint(v))
		}
		img := ""
		if v := body["imageName"]; v != nil {
			img = strings.TrimSpace(fmt.Sprint(v))
		}
		if name == "" && img == "" {
			jsonErr(w, http.StatusBadRequest, "set JSON fields \"name\" and \"imageName\" (RunPod TemplateCreateInput); see docs/RUNPOD_SERVERLESS.md")
			return
		}
		if name == "" {
			jsonErr(w, http.StatusBadRequest, "\"name\" is required and must be non-empty")
			return
		}
		if img == "" {
			jsonErr(w, http.StatusBadRequest, "\"imageName\" is required and must be non-empty (Docker image reference)")
			return
		}
		// Serverless template create uses GraphQL saveTemplate on api.runpod.io. REST
		// POST https://rest.runpod.io/v1/templates fails for serverless with volumeInGb errors.
		ctx, cancel := context.WithTimeout(r.Context(), 120*time.Second)
		defer cancel()
		out, code, raw, err := RunpodGraphQLSaveServerlessTemplate(ctx, body, name, img)
		w.Header().Set("Content-Type", "application/json")
		w.Header().Set("Cache-Control", "no-store")
		if err != nil {
			msg := "RunPod GraphQL saveTemplate: " + err.Error()
			if len(raw) > 0 {
				rs := string(raw)
				if len(rs) > 4096 {
					rs = rs[:4096] + "…"
				}
				msg += " — " + rs
			}
			st := http.StatusBadGateway
			if code == http.StatusUnauthorized || code == http.StatusForbidden {
				st = code
			}
			w.WriteHeader(st)
			_ = json.NewEncoder(w).Encode(map[string]any{
				"ok":     false,
				"status": code,
				"error":  msg,
			})
			return
		}
		if code >= 400 {
			msg := runpodManagementErrorMessage(code, raw)
			w.WriteHeader(http.StatusBadGateway)
			_ = json.NewEncoder(w).Encode(map[string]any{
				"ok":     false,
				"status": code,
				"runpod": out,
				"raw":    string(raw),
				"error":  msg,
			})
			return
		}
		_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "template": out})

	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func handleRunpodServerlessEndpoints(w http.ResponseWriter, r *http.Request) {
	if runpodManagementAPIKeyMaterial() == "" {
		jsonErr(w, http.StatusBadRequest, "set RUNPOD_API_KEY or RUNPOD_TOKEN in .env for management API (endpoint key RUNPOD_TOKEN_END is not used here)")
		return
	}
	switch r.Method {
	case http.MethodGet:
		ctx, cancel := context.WithTimeout(r.Context(), 60*time.Second)
		defer cancel()
		incT := r.URL.Query().Get("includeTemplate") == "1" || strings.EqualFold(r.URL.Query().Get("includeTemplate"), "true")
		incW := r.URL.Query().Get("includeWorkers") == "1" || strings.EqualFold(r.URL.Query().Get("includeWorkers"), "true")
		list, code, raw, err := RunpodManagementListEndpoints(ctx, incT, incW)
		if err != nil {
			jsonErr(w, http.StatusBadGateway, err.Error())
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.Header().Set("Cache-Control", "no-store")
		if code >= 400 {
			msg := runpodManagementErrorMessage(code, raw)
			w.WriteHeader(code)
			_ = json.NewEncoder(w).Encode(map[string]any{
				"ok":     false,
				"status": code,
				"raw":    string(raw),
				"error":  msg,
			})
			return
		}
		_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "endpoints": list})

	case http.MethodPost:
		var body map[string]any
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			jsonErr(w, http.StatusBadRequest, "invalid JSON")
			return
		}
		tid := ""
		if v := body["templateId"]; v != nil {
			tid = strings.TrimSpace(fmt.Sprint(v))
		}
		if tid == "" {
			jsonErr(w, http.StatusBadRequest, "templateId is required (RunPod EndpointCreateInput)")
			return
		}
		ctx, cancel := context.WithTimeout(r.Context(), 120*time.Second)
		defer cancel()
		out, code, raw, err := RunpodManagementCreateEndpoint(ctx, body)
		if err != nil {
			jsonErr(w, http.StatusBadGateway, err.Error())
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.Header().Set("Cache-Control", "no-store")
		if code >= 400 {
			msg := runpodManagementErrorMessage(code, raw)
			w.WriteHeader(code)
			_ = json.NewEncoder(w).Encode(map[string]any{
				"ok":     false,
				"status": code,
				"runpod": out,
				"raw":    string(raw),
				"error":  msg,
			})
			return
		}
		_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "endpoint": out})

	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}
