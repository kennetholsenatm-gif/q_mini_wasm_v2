//go:build !windows

package main

import "fmt"

func callTrainingExportCheckpoint(sessionID uint64, path string) (int32, error) {
	_ = sessionID
	_ = path
	return -1, fmt.Errorf("training checkpoint export/import is only implemented for Windows q_training.dll")
}

func callTrainingImportCheckpoint(sessionID uint64, path string) (int32, error) {
	_ = sessionID
	_ = path
	return -1, fmt.Errorf("training checkpoint export/import is only implemented for Windows q_training.dll")
}
