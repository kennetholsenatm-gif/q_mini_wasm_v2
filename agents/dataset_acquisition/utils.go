package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"time"
)

var (
	currentSize int64
	sizeMutex   sync.Mutex
	httpClient  = &http.Client{Timeout: 30 * time.Second}
)

type DatasetEntry struct {
	Text   string `json:"text"`
	Source string `json:"source"`
	Domain string `json:"domain"`
}

// EnsureDir ensures a directory exists.
func EnsureDir(path string) error {
	return os.MkdirAll(path, os.ModePerm)
}

// CheckDiskQuota checks if we have exceeded our defined MaxDiskUsage.
func CheckDiskQuota() error {
	sizeMutex.Lock()
	defer sizeMutex.Unlock()
	if currentSize > MaxDiskUsage {
		return fmt.Errorf("disk quota exceeded: %d bytes", currentSize)
	}
	return nil
}

// AddSize adds to the tracked size.
func AddSize(size int64) {
	sizeMutex.Lock()
	defer sizeMutex.Unlock()
	currentSize += size
}

// CalculateDirSize calculates the current size of the target directory.
func CalculateDirSize(path string) error {
	var size int64
	err := filepath.Walk(path, func(_ string, info os.FileInfo, err error) error {
		if err != nil {
			return nil // ignore errors
		}
		if !info.IsDir() {
			size += info.Size()
		}
		return nil
	})
	if err != nil {
		return err
	}
	sizeMutex.Lock()
	currentSize = size
	sizeMutex.Unlock()
	return nil
}

// WriteJSONL appends entries to a specified JSONL file safely.
func WriteJSONL(filePath string, entries []DatasetEntry) error {
	if err := CheckDiskQuota(); err != nil {
		return err
	}

	file, err := os.OpenFile(filePath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return err
	}
	defer file.Close()

	writer := bufio.NewWriter(file)
	encoder := json.NewEncoder(writer)

	var written int64
	for _, entry := range entries {
		b, err := json.Marshal(entry)
		if err != nil {
			continue
		}
		written += int64(len(b)) + 1 // +1 for newline
		if err := encoder.Encode(entry); err != nil {
			return err
		}
	}
	
	if err := writer.Flush(); err != nil {
		return err
	}
	
	AddSize(written)
	return nil
}

// FetchURL securely fetches data from a URL with basic retries.
func FetchURL(url string) ([]byte, error) {
	var lastErr error
	for retries := 0; retries < 3; retries++ {
		req, err := http.NewRequest("GET", url, nil)
		if err != nil {
			return nil, err
		}
		req.Header.Set("User-Agent", "DataAcquisitionBot/1.0")

		resp, err := httpClient.Do(req)
		if err != nil {
			lastErr = err
			time.Sleep(time.Second * time.Duration(retries+1))
			continue
		}
		defer resp.Body.Close()

		if resp.StatusCode != http.StatusOK {
			lastErr = fmt.Errorf("unexpected status code: %d", resp.StatusCode)
			time.Sleep(time.Second * time.Duration(retries+1))
			continue
		}

		body, err := io.ReadAll(resp.Body)
		if err != nil {
			lastErr = err
			continue
		}
		return body, nil
	}
	return nil, fmt.Errorf("failed to fetch %s after 3 retries: %v", url, lastErr)
}
