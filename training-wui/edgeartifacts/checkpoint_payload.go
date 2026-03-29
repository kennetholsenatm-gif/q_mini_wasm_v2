package edgeartifacts

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
)

// ExtractCheckpointPayload runs scripts/checkpoint_tpem_payload.py (requires Python + torch + qminiwasm).
func ExtractCheckpointPayload(repoRoot, pythonExe, checkpointAbs string) ([]byte, error) {
	script := filepath.Join(repoRoot, "scripts", "checkpoint_tpem_payload.py")
	if _, err := os.Stat(script); err != nil {
		return nil, fmt.Errorf("checkpoint helper script: %w", err)
	}
	tmp, err := os.CreateTemp("", "qmw-ckpt-payload-*.bin")
	if err != nil {
		return nil, err
	}
	outPath := tmp.Name()
	_ = tmp.Close()
	defer os.Remove(outPath)
	cmd := exec.Command(pythonExe, script, checkpointAbs, "--output", outPath)
	cmd.Dir = repoRoot
	out, err := cmd.CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("checkpoint_tpem_payload.py: %w: %s", err, string(out))
	}
	return os.ReadFile(outPath)
}
