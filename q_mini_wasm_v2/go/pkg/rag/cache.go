package rag

import (
	"sync"
	"time"
)

// CacheConfig holds cache configuration
type CacheConfig struct {
	MaxSize      int           `json:"max_size"`       // Maximum number of items
	TTL          time.Duration `json:"ttl"`            // Time to live
	EvictionPolicy string     `json:"eviction_policy"` // "lru", "lfu", "fifo"
}

// CacheEntry represents a cached item
type CacheEntry struct {
	Key        string
	Value      interface{}
	CreatedAt  time.Time
	AccessedAt time.Time
	AccessCount int
}

// Cache provides a thread-safe caching layer
type Cache struct {
	config  CacheConfig
	items   map[string]*CacheEntry
	mu      sync.RWMutex
	size    int
	maxSize int
}

// NewCache creates a new cache instance
func NewCache(config CacheConfig) *Cache {
	if config.MaxSize == 0 {
		config.MaxSize = 1000
	}
	if config.TTL == 0 {
		config.TTL = 5 * time.Minute
	}
	if config.EvictionPolicy == "" {
		config.EvictionPolicy = "lru"
	}
	
	return &Cache{
		config:  config,
		items:   make(map[string]*CacheEntry),
		maxSize: config.MaxSize,
	}
}

// Get retrieves a value from the cache
func (c *Cache) Get(key string) (interface{}, bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	
	entry, exists := c.items[key]
	if !exists {
		return nil, false
	}
	
	// Check TTL
	if time.Since(entry.CreatedAt) > c.config.TTL {
		delete(c.items, key)
		c.size--
		return nil, false
	}
	
	// Update access metadata
	entry.AccessedAt = time.Now()
	entry.AccessCount++
	
	return entry.Value, true
}

// Set adds a value to the cache
func (c *Cache) Set(key string, value interface{}) {
	c.mu.Lock()
	defer c.mu.Unlock()
	
	// Check if key already exists
	if _, exists := c.items[key]; exists {
		c.items[key].Value = value
		c.items[key].AccessedAt = time.Now()
		c.items[key].AccessCount++
		return
	}
	
	// Evict if at capacity
	if c.size >= c.maxSize {
		c.evict()
	}
	
	// Add new entry
	c.items[key] = &CacheEntry{
		Key:         key,
		Value:       value,
		CreatedAt:   time.Now(),
		AccessedAt:  time.Now(),
		AccessCount: 1,
	}
	c.size++
}

// Delete removes a value from the cache
func (c *Cache) Delete(key string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	
	if _, exists := c.items[key]; exists {
		delete(c.items, key)
		c.size--
	}
}

// Clear removes all items from the cache
func (c *Cache) Clear() {
	c.mu.Lock()
	defer c.mu.Unlock()
	
	c.items = make(map[string]*CacheEntry)
	c.size = 0
}

// Size returns the current cache size
func (c *Cache) Size() int {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.size
}

// evict removes an item based on the eviction policy
func (c *Cache) evict() {
	if len(c.items) == 0 {
		return
	}
	
	var evictKey string
	
	switch c.config.EvictionPolicy {
	case "lru":
		// Least Recently Used
		var oldest time.Time
		for key, entry := range c.items {
			if oldest.IsZero() || entry.AccessedAt.Before(oldest) {
				oldest = entry.AccessedAt
				evictKey = key
			}
		}
	case "lfu":
		// Least Frequently Used
		minCount := int(^uint(0) >> 1) // Max int
		for key, entry := range c.items {
			if entry.AccessCount < minCount {
				minCount = entry.AccessCount
				evictKey = key
			}
		}
	case "fifo":
		// First In First Out
		var oldest time.Time
		for key, entry := range c.items {
			if oldest.IsZero() || entry.CreatedAt.Before(oldest) {
				oldest = entry.CreatedAt
				evictKey = key
			}
		}
	}
	
	if evictKey != "" {
		delete(c.items, evictKey)
		c.size--
	}
}

// EmbeddingCache provides caching for embeddings
type EmbeddingCache struct {
	cache *Cache
}

// NewEmbeddingCache creates a new embedding cache
func NewEmbeddingCache(config CacheConfig) *EmbeddingCache {
	return &EmbeddingCache{
		cache: NewCache(config),
	}
}

// Get retrieves an embedding from the cache
func (ec *EmbeddingCache) Get(text string) ([]float32, bool) {
	val, ok := ec.cache.Get(text)
	if !ok {
		return nil, false
	}
	embedding, ok := val.([]float32)
	return embedding, ok
}

// Set adds an embedding to the cache
func (ec *EmbeddingCache) Set(text string, embedding []float32) {
	ec.cache.Set(text, embedding)
}

// QueryCache provides caching for query results
type QueryCache struct {
	cache *Cache
}

// NewQueryCache creates a new query cache
func NewQueryCache(config CacheConfig) *QueryCache {
	return &QueryCache{
		cache: NewCache(config),
	}
}

// Get retrieves query results from the cache
func (qc *QueryCache) Get(query string, maxTokens int, contextType string) (*RetrieveResult, bool) {
	key := qc.buildKey(query, maxTokens, contextType)
	val, ok := qc.cache.Get(key)
	if !ok {
		return nil, false
	}
	result, ok := val.(*RetrieveResult)
	return result, ok
}

// Set adds query results to the cache
func (qc *QueryCache) Set(query string, maxTokens int, contextType string, result *RetrieveResult) {
	key := qc.buildKey(query, maxTokens, contextType)
	qc.cache.Set(key, result)
}

// buildKey creates a cache key from query parameters
func (qc *QueryCache) buildKey(query string, maxTokens int, contextType string) string {
	return query + ":" + string(rune(maxTokens)) + ":" + contextType
}
