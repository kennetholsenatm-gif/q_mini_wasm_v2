// Command qmw-taxonomy-linter enforces Edge AI taxonomy rules (Go port of scripts/taxonomy_linter.py).
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
)

type cfgDoc struct {
	Patterns               []patternRule `json:"patterns"`
	ScanExtensions         []string      `json:"scan_extensions"`
	PathPrefixes           []string      `json:"path_prefixes"`
	IgnorePathSubstrings   []string      `json:"ignore_path_substrings"`
	SkipAllPatternsForPaths []string     `json:"skip_all_patterns_for_paths"`
}

type patternRule struct {
	Regex            string   `json:"regex"`
	Hint             string   `json:"hint"`
	PathPrefixes     []string `json:"path_prefixes"`
	PathPrefix       string   `json:"path_prefix"`
	ApplyExtensions  []string `json:"apply_extensions"`
}

type compiledRule struct {
	re           *regexp.Regexp
	hint         string
	prefixes     []string
	applyExt     map[string]struct{}
}

func main() {
	root := flag.String("root", ".", "repository root")
	configPath := flag.String("config", "", "path to taxonomy_linter.json")
	diffBase := flag.String("diff-base", "", "git ref for diff ...HEAD")
	full := flag.Bool("full", false, "scan all matching files")
	diffWorking := flag.Bool("diff-working", false, "diff working tree vs HEAD")
	flag.Parse()

	repoRoot, err := filepath.Abs(*root)
	if err != nil {
		fmt.Fprintf(os.Stderr, "qmw-taxonomy-linter: %v\n", err)
		os.Exit(1)
	}
	cfgFile := strings.TrimSpace(*configPath)
	if cfgFile == "" {
		cfgFile = filepath.Join(repoRoot, "configs", "ci", "taxonomy_linter.json")
	}
	raw, err := os.ReadFile(cfgFile)
	if err != nil {
		fmt.Fprintf(os.Stderr, "qmw-taxonomy-linter: config: %v\n", err)
		os.Exit(1)
	}
	var cfg cfgDoc
	if err := json.Unmarshal(raw, &cfg); err != nil {
		fmt.Fprintf(os.Stderr, "qmw-taxonomy-linter: json: %v\n", err)
		os.Exit(1)
	}
	rules := compileRules(&cfg)
	if len(rules) == 0 {
		fmt.Fprintf(os.Stderr, "qmw-taxonomy-linter: no patterns\n")
		os.Exit(1)
	}

	var entries [][3]string // path, lineno, line
	switch {
	case *diffWorking:
		entries = gitDiffAdded(repoRoot, "HEAD", &cfg, false)
	case *diffBase != "":
		entries = gitDiffAdded(repoRoot, *diffBase, &cfg, true)
	case *full:
		entries = collectFullScan(repoRoot, &cfg)
	default:
		flag.Usage()
		fmt.Fprintf(os.Stderr, "\nProvide --diff-base REF, --diff-working, or --full.\n")
		os.Exit(2)
	}

	v := scan(entries, rules, &cfg)
	if len(v) > 0 {
		fmt.Fprintf(os.Stderr, "Edge AI taxonomy violations:\n")
		for _, s := range v {
			fmt.Fprintf(os.Stderr, "%s\n", s)
		}
		os.Exit(1)
	}
}

func compileRules(cfg *cfgDoc) []compiledRule {
	var out []compiledRule
	for _, it := range cfg.Patterns {
		re, err := regexp.Compile(it.Regex)
		if err != nil {
			fmt.Fprintf(os.Stderr, "qmw-taxonomy-linter: bad regex %q: %v\n", it.Regex, err)
			continue
		}
		var prefs []string
		for _, p := range it.PathPrefixes {
			s := strings.TrimSpace(strings.ReplaceAll(p, "\\", "/"))
			if s != "" && !strings.HasSuffix(s, "/") {
				s += "/"
			}
			if s != "" {
				prefs = append(prefs, s)
			}
		}
		if it.PathPrefix != "" {
			s := strings.TrimSpace(strings.ReplaceAll(it.PathPrefix, "\\", "/"))
			if !strings.HasSuffix(s, "/") {
				s += "/"
			}
			prefs = append(prefs, s)
		}
		var extMap map[string]struct{}
		if len(it.ApplyExtensions) > 0 {
			extMap = make(map[string]struct{})
			for _, e := range it.ApplyExtensions {
				e = strings.ToLower(strings.TrimSpace(e))
				if !strings.HasPrefix(e, ".") {
					e = "." + e
				}
				extMap[e] = struct{}{}
			}
		}
		out = append(out, compiledRule{re: re, hint: it.Hint, prefixes: prefs, applyExt: extMap})
	}
	return out
}

func shouldScanPath(rel string, cfg *cfgDoc) bool {
	relNorm := strings.ReplaceAll(rel, "\\", "/")
	for _, ign := range cfg.IgnorePathSubstrings {
		if strings.Contains(relNorm, ign) {
			return false
		}
	}
	if len(cfg.PathPrefixes) > 0 {
		ok := false
		for _, p := range cfg.PathPrefixes {
			pn := strings.TrimSuffix(strings.TrimSpace(strings.ReplaceAll(p, "\\", "/")), "/")
			if pn == "" {
				continue
			}
			if relNorm == pn || strings.HasPrefix(relNorm, pn+"/") {
				ok = true
				break
			}
		}
		if !ok {
			return false
		}
	}
	ext := strings.ToLower(filepath.Ext(relNorm))
	set := make(map[string]struct{})
	for _, e := range cfg.ScanExtensions {
		set[strings.ToLower(e)] = struct{}{}
	}
	_, ok := set[ext]
	return ok
}

func stripMarkdownFences(text string) string {
	var out []string
	inFence := false
	for _, line := range strings.Split(text, "\n") {
		st := strings.TrimSpace(line)
		if strings.HasPrefix(st, "```") {
			inFence = !inFence
			continue
		}
		if !inFence {
			out = append(out, line)
		}
	}
	return strings.Join(out, "\n")
}

func gitDiffAdded(repoRoot, base string, cfg *cfgDoc, tripleDot bool) [][3]string {
	var arg string
	if tripleDot {
		arg = base + "...HEAD"
	} else {
		arg = base
	}
	cmd := exec.Command("git", "-C", repoRoot, "diff", "-U0", arg, "--")
	out, _ := cmd.Output()
	return parseGitDiff(string(out), cfg)
}

func parseGitDiff(diffText string, cfg *cfgDoc) [][3]string {
	var res [][3]string
	var curFile string
	newStart := 0
	hunkLine := 0
	for _, line := range strings.Split(diffText, "\n") {
		if strings.HasPrefix(line, "+++ b/") {
			curFile = strings.TrimSpace(strings.TrimPrefix(line, "+++ b/"))
			newStart = 0
			hunkLine = 0
			continue
		}
		if curFile == "" || !shouldScanPath(curFile, cfg) {
			continue
		}
		if strings.HasPrefix(line, "@@") {
			// hunk: @@ -old +new,count @@ — take first +number as new file line
			if idx := strings.Index(line, "+"); idx >= 0 {
				rest := line[idx+1:]
				var n int
				_, _ = fmt.Sscanf(strings.TrimLeft(rest, " "), "%d", &n)
				newStart = n
				hunkLine = 0
			}
			continue
		}
		if strings.HasPrefix(line, "+") && !strings.HasPrefix(line, "+++") {
			content := strings.TrimPrefix(line, "+")
			lineno := newStart + hunkLine
			hunkLine++
			if strings.HasSuffix(strings.ToLower(curFile), ".md") && strings.HasPrefix(strings.TrimSpace(content), "```") {
				continue
			}
			res = append(res, [3]string{curFile, fmt.Sprintf("%d", lineno), content})
		}
	}
	return res
}

func collectFullScan(repoRoot string, cfg *cfgDoc) [][3]string {
	var res [][3]string
	for _, prefix := range cfg.PathPrefixes {
		p := strings.TrimSpace(strings.ReplaceAll(prefix, "\\", "/"))
		p = strings.TrimSuffix(p, "/")
		if p == "" {
			continue
		}
		root := filepath.Join(repoRoot, filepath.FromSlash(p))
		_ = filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return nil
			}
			rel, e := filepath.Rel(repoRoot, path)
			if e != nil {
				return nil
			}
			relPos := filepath.ToSlash(rel)
			for _, ign := range cfg.IgnorePathSubstrings {
				if strings.Contains(relPos, ign) {
					if info.IsDir() {
						return filepath.SkipDir
					}
					return nil
				}
			}
			if info.IsDir() {
				return nil
			}
			if !shouldScanPath(relPos, cfg) {
				return nil
			}
			raw, err := os.ReadFile(path)
			if err != nil {
				return nil
			}
			s := string(raw)
			if strings.HasSuffix(strings.ToLower(path), ".md") {
				s = stripMarkdownFences(s)
			}
			for i, ln := range strings.Split(s, "\n") {
				res = append(res, [3]string{relPos, fmt.Sprintf("%d", i+1), ln})
			}
			return nil
		})
	}
	return res
}

func scan(entries [][3]string, rules []compiledRule, cfg *cfgDoc) []string {
	skip := make(map[string]struct{})
	for _, p := range cfg.SkipAllPatternsForPaths {
		skip[strings.ReplaceAll(p, "\\", "/")] = struct{}{}
	}
	var viol []string
	for _, e := range entries {
		path, lineno, text := e[0], e[1], e[2]
		pathNorm := strings.ReplaceAll(path, "\\", "/")
		if _, ok := skip[pathNorm]; ok {
			continue
		}
		suffix := strings.ToLower(filepath.Ext(pathNorm))
		for _, r := range rules {
			if len(r.prefixes) > 0 {
				ok := false
				for _, pfx := range r.prefixes {
					if strings.HasPrefix(pathNorm, pfx) {
						ok = true
						break
					}
				}
				if !ok {
					continue
				}
			}
			if r.applyExt != nil {
				if _, ok := r.applyExt[suffix]; !ok {
					continue
				}
			}
			if r.re.MatchString(text) {
				viol = append(viol, fmt.Sprintf("%s:%s: %q  [%s]  %s", path, lineno, strings.TrimSpace(text), r.re.String(), r.hint))
			}
		}
	}
	return viol
}
