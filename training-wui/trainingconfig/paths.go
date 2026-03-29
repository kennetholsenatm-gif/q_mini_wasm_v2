package trainingconfig

import (
	"errors"
	"path/filepath"
	"strings"
)

// ResolveRepoRelativePath joins user path to root and ensures the result stays inside root.
func ResolveRepoRelativePath(root, user string) (abs string, err error) {
	user = strings.TrimSpace(user)
	if user == "" {
		return "", errors.New("path is required")
	}
	user = strings.TrimPrefix(user, "/")
	root = filepath.Clean(root)
	abs = filepath.Join(root, filepath.FromSlash(user))
	abs = filepath.Clean(abs)
	relRoot, e := filepath.Rel(root, abs)
	if e != nil || strings.HasPrefix(relRoot, "..") {
		return "", errors.New("path escapes repository root")
	}
	return abs, nil
}
