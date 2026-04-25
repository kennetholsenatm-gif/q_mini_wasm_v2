//go:build windows

package main

import (
	"fmt"
	"sync"
	"syscall"
	"unsafe"
)

var (
	trainingCheckpointDLL     *syscall.DLL
	procExportCheckpoint      *syscall.Proc
	procImportCheckpoint      *syscall.Proc
	trainingCheckpointLoadErr error
	checkpointOnce            sync.Once
)

func loadCheckpointProcs() error {
	checkpointOnce.Do(func() {
		dll, err := syscall.LoadDLL("q_training.dll")
		if err != nil {
			trainingCheckpointLoadErr = err
			return
		}
		trainingCheckpointDLL = dll
		p1, err := dll.FindProc("Training_ExportCheckpoint")
		if err != nil {
			trainingCheckpointLoadErr = fmt.Errorf("Training_ExportCheckpoint: %w", err)
			return
		}
		p2, err := dll.FindProc("Training_ImportCheckpoint")
		if err != nil {
			trainingCheckpointLoadErr = fmt.Errorf("Training_ImportCheckpoint: %w", err)
			return
		}
		procExportCheckpoint = p1
		procImportCheckpoint = p2
	})
	return trainingCheckpointLoadErr
}

func callTrainingExportCheckpoint(sessionID uint64, path string) (int32, error) {
	if err := loadCheckpointProcs(); err != nil {
		return -1, err
	}
	ptr, err := syscall.BytePtrFromString(path)
	if err != nil {
		return -1, err
	}
	r, _, _ := procExportCheckpoint.Call(uintptr(sessionID), uintptr(unsafe.Pointer(ptr)))
	return int32(r), nil
}

func callTrainingImportCheckpoint(sessionID uint64, path string) (int32, error) {
	if err := loadCheckpointProcs(); err != nil {
		return -1, err
	}
	ptr, err := syscall.BytePtrFromString(path)
	if err != nil {
		return -1, err
	}
	r, _, _ := procImportCheckpoint.Call(uintptr(sessionID), uintptr(unsafe.Pointer(ptr)))
	return int32(r), nil
}
