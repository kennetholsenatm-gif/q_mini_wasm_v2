// RunPod remote training: sync repo over SSH + run python -m qminiwasm.engine on the pod.
// Requires OpenSSH client (ssh, scp) and tar on PATH. Configure via RUNPOD_SSH_* env vars.
package main

import (
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strings"
	"time"
)

func runpodSSHUser() string {
	if s := strings.TrimSpace(os.Getenv("RUNPOD_SSH_USER")); s != "" {
		return s
	}
	return "root"
}

func runpodRemoteDir() string {
	s := strings.TrimSpace(os.Getenv("RUNPOD_REMOTE_DIR"))
	if s == "" {
		return "/workspace/qminiwasm-core"
	}
	return strings.TrimRight(strings.ReplaceAll(s, "\\", "/"), "/")
}

func runpodSSHKeyPath() string {
	return strings.TrimSpace(os.Getenv("RUNPOD_SSH_KEY"))
}

func runpodSSHBaseArgs(keyPath string) []string {
	args := []string{
		"-o", "BatchMode=yes",
		"-o", "StrictHostKeyChecking=accept-new",
		"-o", "ConnectTimeout=30",
	}
	if keyPath != "" {
		args = append(args, "-i", keyPath)
	}
	return args
}

func shellQuoteSingle(s string) string {
	return `'` + strings.ReplaceAll(s, `'`, `'\''`) + `'`
}

func runpodToolOK(name string) bool {
	_, err := exec.LookPath(name)
	return err == nil
}

func runpodPublicIPFromState(ctx context.Context) (string, error) {
	out, err := runpodOutputsJSON(ctx)
	if err != nil {
		return "", err
	}
	raw, ok := out["public_ip"]
	if !ok || raw == nil {
		return "", fmt.Errorf("terraform output public_ip missing")
	}
	s, ok := raw.(string)
	if !ok {
		return "", fmt.Errorf("unexpected public_ip type %T", raw)
	}
	s = strings.TrimSpace(s)
	if s == "" {
		return "", fmt.Errorf("public_ip empty (pod may still be provisioning)")
	}
	return s, nil
}

func waitRunpodPublicIP(ctx context.Context) (string, error) {
	for {
		ip, err := runpodPublicIPFromState(ctx)
		if err == nil && ip != "" {
			return ip, nil
		}
		select {
		case <-ctx.Done():
			if err != nil {
				return "", fmt.Errorf("waiting for RunPod public_ip: %w (%v)", ctx.Err(), err)
			}
			return "", fmt.Errorf("waiting for RunPod public_ip: %w", ctx.Err())
		case <-time.After(4 * time.Second):
		}
	}
}

// runpodSyncRepo tars repoRoot (excluding bulky dirs) and extracts on the remote via ssh.
func runpodSyncRepo(ctx context.Context, ip, user, keyPath, remoteDir string) error {
	sshBin, err := exec.LookPath("ssh")
	if err != nil {
		return fmt.Errorf("ssh not on PATH (install OpenSSH client): %w", err)
	}
	tarBin, err := exec.LookPath("tar")
	if err != nil {
		return fmt.Errorf("tar not on PATH: %w", err)
	}
	remoteDir = strings.TrimRight(strings.ReplaceAll(remoteDir, "\\", "/"), "/")
	remoteScript := fmt.Sprintf(
		"mkdir -p %s && tar xzf - -C %s",
		shellQuoteSingle(remoteDir),
		shellQuoteSingle(remoteDir),
	)
	sshArgs := append(runpodSSHBaseArgs(keyPath), fmt.Sprintf("%s@%s", user, ip), remoteScript)
	sshCmd := exec.CommandContext(ctx, sshBin, sshArgs...)

	tarArgs := []string{
		"-czf", "-",
		"--exclude=.git", "--exclude=.venv", "--exclude=__pycache__", "--exclude=artifacts",
		"--exclude=.mypy_cache", "--exclude=node_modules", "--exclude=.terraform",
		"-C", repoRoot, ".",
	}
	tarCmd := exec.CommandContext(ctx, tarBin, tarArgs...)

	pr, pw := io.Pipe()
	tarCmd.Stdout = pw
	var tarErr strings.Builder
	tarCmd.Stderr = &tarErr

	sshCmd.Stdin = pr
	var sshOut strings.Builder
	sshCmd.Stdout = &sshOut
	sshCmd.Stderr = &sshOut

	if err := sshCmd.Start(); err != nil {
		_ = pw.Close()
		return fmt.Errorf("ssh start: %w", err)
	}
	if err := tarCmd.Start(); err != nil {
		_ = sshCmd.Process.Kill()
		_ = pw.Close()
		return fmt.Errorf("tar start: %w", err)
	}
	tarWaitErr := tarCmd.Wait()
	_ = pw.Close()
	if tarWaitErr != nil {
		_ = sshCmd.Process.Kill()
		return fmt.Errorf("tar pack: %w (%s)", tarWaitErr, tarErr.String())
	}
	if err := sshCmd.Wait(); err != nil {
		return fmt.Errorf("ssh unpack: %w\n%s", err, sshOut.String())
	}
	return nil
}

func runpodSCPLocalToRemote(ctx context.Context, ip, user, keyPath, localAbs, remoteUnixPath string) error {
	scpBin, err := exec.LookPath("scp")
	if err != nil {
		return fmt.Errorf("scp not on PATH: %w", err)
	}
	args := append(runpodSSHBaseArgs(keyPath), localAbs, fmt.Sprintf("%s@%s:%s", user, ip, remoteUnixPath))
	cmd := exec.CommandContext(ctx, scpBin, args...)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("scp: %w\n%s", err, string(out))
	}
	return nil
}

func runpodRemoteEngineScript(remoteDir, configRel string, extraEnv []string) string {
	var b strings.Builder
	b.WriteString("set -euo pipefail\n")
	b.WriteString(fmt.Sprintf("cd %s\n", shellQuoteSingle(remoteDir)))
	b.WriteString("if command -v python3 >/dev/null 2>&1; then PY=python3; else PY=python; fi\n")
	b.WriteString("if [ ! -d .venv ]; then $PY -m venv .venv; fi\n")
	b.WriteString(". .venv/bin/activate\n")
	b.WriteString("pip install -q -U pip\n")
	b.WriteString("pip install -q -e \".[training]\"\n")
	b.WriteString("export ACCELERATOR=cuda\n")
	for _, line := range extraEnv {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		i := strings.IndexByte(line, '=')
		if i <= 0 {
			continue
		}
		k, v := line[:i], line[i+1:]
		b.WriteString(fmt.Sprintf("export %s=%s\n", k, shellQuoteSingle(v)))
	}
	b.WriteString(fmt.Sprintf("python -m qminiwasm.engine --config %s\n", shellQuoteSingle(configRel)))
	return b.String()
}

// runpodRemoteTrainCmd returns an exec.Cmd that streams a remote bash script over ssh (stdin).
// Caller must attach StdoutPipe/StderrPipe and call Start.
func runpodRemoteTrainCmd(ip, user, keyPath, remoteDir, configRel string, extraEnv []string) (*exec.Cmd, error) {
	sshBin, err := exec.LookPath("ssh")
	if err != nil {
		return nil, fmt.Errorf("ssh not on PATH: %w", err)
	}
	sshArgs := append(runpodSSHBaseArgs(keyPath), fmt.Sprintf("%s@%s", user, ip), "bash", "-s")
	cmd := exec.Command(sshBin, sshArgs...)
	cmd.Dir = repoRoot
	cmd.Env = os.Environ()
	script := runpodRemoteEngineScript(remoteDir, configRel, extraEnv)
	cmd.Stdin = strings.NewReader(script)
	return cmd, nil
}
