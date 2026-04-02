//! Rate limiting and caching for q_mini_wasm_v2

use std::sync::Arc;
use std::time::{Duration, Instant};
use dashmap::DashMap;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RateLimitConfig {
    pub rpm: u32,
    pub tpm: u32,
    pub rpd: u32,
}

impl Default for RateLimitConfig {
    fn default() -> Self {
        Self { rpm: 15, tpm: 1_000_000, rpd: 1500 }
    }
}

pub struct RateLimiter {
    config: RateLimitConfig,
    request_timestamps: Arc<DashMap<String, Instant>>,
    token_usage: Arc<DashMap<String, u32>>,
}

impl RateLimiter {
    pub fn new(config: RateLimitConfig) -> Self {
        Self {
            config,
            request_timestamps: Arc::new(DashMap::new()),
            token_usage: Arc::new(DashMap::new()),
        }
    }

    pub fn check_rate_limit(&self, agent_id: &str, estimated_tokens: u32) -> bool {
        let now = Instant::now();
        let one_minute_ago = now - Duration::from_secs(60);
        self.request_timestamps.retain(|_, &mut ts| ts > one_minute_ago);
        let rpm_count = self.request_timestamps.iter().filter(|e| e.key().starts_with(agent_id)).count();
        if rpm_count >= self.config.rpm as usize { return false; }
        let tpm_used = self.token_usage.get(agent_id).map(|e| *e.value()).unwrap_or(0);
        if tpm_used + estimated_tokens > self.config.tpm { return false; }
        true
    }

    pub fn record_request(&self, agent_id: &str, tokens_used: u32) {
        let key = format!("{}:{}", agent_id, chrono::Utc::now().timestamp_nanos());
        self.request_timestamps.insert(key, Instant::now());
        let mut tpm = self.token_usage.entry(agent_id.to_string()).or_insert(0);
        *tpm += tokens_used;
    }
}

pub struct Cache<T: Clone> {
    entries: Arc<DashMap<String, (T, Instant)>>,
    ttl: Duration,
}

impl<T: Clone> Cache<T> {
    pub fn new(ttl: Duration) -> Self {
        Self { entries: Arc::new(DashMap::new()), ttl }
    }

    pub fn get(&self, key: &str) -> Option<T> {
        if let Some(entry) = self.entries.get(key) {
            if entry.value().1 > Instant::now() { return Some(entry.value().0.clone()); }
            drop(entry);
            self.entries.remove(key);
        }
        None
    }

    pub fn set(&self, key: String, value: T) {
        self.entries.insert(key, (value, Instant::now() + self.ttl));
    }

    pub fn clear(&self) { self.entries.clear(); }
    pub fn len(&self) -> usize { self.entries.len() }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_rate_limiter() {
        let config = RateLimitConfig::default();
        let limiter = RateLimiter::new(config);
        assert!(limiter.check_rate_limit("agent1", 1000));
        limiter.record_request("agent1", 1000);
    }

    #[test]
    fn test_cache() {
        let cache = Cache::new(Duration::from_secs(60));
        cache.set("key1".to_string(), "value1".to_string());
        assert_eq!(cache.get("key1"), Some("value1".to_string()));
    }
}
