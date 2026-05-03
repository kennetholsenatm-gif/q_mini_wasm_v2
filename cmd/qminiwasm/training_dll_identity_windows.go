//go:build windows

package main

/*
extern void Training_GetVersion(char* version_out, size_t max_len);
*/
import "C"

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
	"unsafe"

	"golang.org/x/sys/windows"
)

func stripWindowsNamespacePrefix(p string) string {
	p = strings.TrimSpace(p)
	if strings.HasPrefix(p, `\\?\`) {
		return p[len(`\\?\`):]
	}
	if strings.HasPrefix(p, `\??\`) {
		return p[len(`\??\`):]
	}
	return p
}

func normalizePathForCompare(p string) string {
	p = stripWindowsNamespacePrefix(p)
	if p == "" {
		return ""
	}
	if s, err := filepath.EvalSymlinks(p); err == nil {
		p = s
	}
	p = stripWindowsNamespacePrefix(p)
	if a, err := filepath.Abs(p); err == nil {
		p = a
	}
	return filepath.Clean(p)
}

// sameUnderlyingTrainingDLL is true when both paths denote the same file (junction/subst/SameFile),
// even if string forms differ after normalization.
func sameUnderlyingTrainingDLL(loadedPath, canonicalPath string) bool {
	fiL, errL := os.Stat(loadedPath)
	fiC, errC := os.Stat(canonicalPath)
	if errL != nil || errC != nil {
		return false
	}
	return os.SameFile(fiL, fiC)
}

func logLoadedQTrainingDLLIdentity() {
	// Do NOT use GetModuleHandleEx(FROM_ADDRESS) on C.Training_GetVersion: with PE import thunks that
	// address often lies in qminiwasm.exe, so GetModuleFileName would report the exe — instant false fatal.
	mod, err := windows.LoadLibrary("q_training.dll")
	if err != nil || mod == 0 {
		fmt.Fprintf(os.Stderr, "[Training] FATAL: LoadLibrary(q_training.dll): %v\n", err)
		_ = os.Stderr.Sync()
		os.Exit(1)
	}
	defer func() { _ = windows.FreeLibrary(mod) }()

	buf := make([]uint16, 32768)
	n, err := windows.GetModuleFileName(mod, &buf[0], uint32(len(buf)))
	if err != nil || n == 0 {
		fmt.Fprintf(os.Stderr, "[Training] FATAL: GetModuleFileName(q_training.dll): %v n=%d\n", err, n)
		_ = os.Stderr.Sync()
		os.Exit(1)
	}
	loadedPath := windows.UTF16ToString(buf[:n])

	exePath, err := os.Executable()
	if err != nil {
		fmt.Fprintf(os.Stderr, "[Training] FATAL: os.Executable: %v\n", err)
		_ = os.Stderr.Sync()
		os.Exit(1)
	}
	exePath = stripWindowsNamespacePrefix(exePath)
	canonical := filepath.Join(filepath.Dir(exePath), "q_training.dll")
	loadedNorm := normalizePathForCompare(loadedPath)
	canonNorm := normalizePathForCompare(canonical)
	skipGuard := strings.TrimSpace(os.Getenv("QMINI_SKIP_TRAINING_DLL_PATH_GUARD")) == "1"
	pathsMatch := strings.EqualFold(loadedNorm, canonNorm) || sameUnderlyingTrainingDLL(loadedPath, canonical)
	if !pathsMatch {
		msg := fmt.Sprintf("[Training] FATAL: loaded q_training.dll is not the file beside this exe.\n"+
			"  loaded:   %s\n"+
			"  required: %s\n"+
			"  (normalized loaded=%q canonical=%q)\n"+
			"Build with scripts/build_qminiwasm.ps1 (publishes DLL next to qminiwasm.exe). "+
			"If publish failed, close any process holding q_training.dll and rebuild.\n",
			loadedPath, canonical, loadedNorm, canonNorm)
		if skipGuard {
			fmt.Fprintf(os.Stderr, "%s[Training] WARN: QMINI_SKIP_TRAINING_DLL_PATH_GUARD=1 — continuing anyway.\n", msg)
			_ = os.Stderr.Sync()
		} else {
			fmt.Fprint(os.Stderr, msg)
			_ = os.Stderr.Sync()
			os.Exit(1)
		}
	}

	mt := "(unknown)"
	sz := "(unknown)"
	if st, err := os.Stat(loadedPath); err == nil {
		mt = st.ModTime().UTC().Format(time.RFC3339)
		sz = fmt.Sprintf("%d", st.Size())
	} else {
		mt = fmt.Sprintf("stat: %v", err)
	}
	verBuf := make([]byte, 256)
	C.Training_GetVersion((*C.char)(unsafe.Pointer(&verBuf[0])), C.size_t(len(verBuf)))
	ver := C.GoString((*C.char)(unsafe.Pointer(&verBuf[0])))
	fmt.Fprintf(os.Stderr, "[Training] Native training DLL: %s | size=%s bytes | mtime_utc=%s | Training_GetVersion=%q\n",
		loadedPath, sz, mt, ver)
	_ = os.Stderr.Sync()
}
