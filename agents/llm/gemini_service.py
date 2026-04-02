"""
Gemini LLM Service with Rate Limiting

This module provides integration with Google's Gemini API via llama-index.
It implements rate limiting and caching to optimize usage within free tier limits.
"""

import asyncio
import json
import os
import time
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any, Dict, List, Optional, Union
from dataclasses import dataclass, field

import diskcache
import structlog
from pydantic import BaseModel, Field

logger = structlog.get_logger()


@dataclass
class RateLimitState:
    """Track rate limit state."""
    request_timestamps: List[float] = field(default_factory=list)
    token_usage_minute: int = 0
    token_usage_day: int = 0
    daily_reset_time: Optional[datetime] = None
    
    def reset_daily(self) -> None:
        """Reset daily counters."""
        self.token_usage_day = 0
        self.daily_reset_time = datetime.now() + timedelta(days=1)
    
    def cleanup_old_timestamps(self) -> None:
        """Remove timestamps older than 1 minute."""
        one_minute_ago = time.time() - 60
        self.request_timestamps = [
            ts for ts in self.request_timestamps
            if ts > one_minute_ago
        ]


class GeminiConfig(BaseModel):
    """Configuration for Gemini service."""
    api_key_env: str = "GEMINI_API_KEY"
    model: str = "gemini-3-flash-preview"
    temperature: float = 0.7
    max_tokens: int = 8192
    rate_limit_rpm: int = 15
    rate_limit_tpm: int = 1_000_000
    rate_limit_rpd: int = 1500
    batch_size: int = 10
    cache_ttl: int = 3600
    cache_dir: str = ".cache/gemini"


class GeminiService:
    """
    Gemini LLM service with rate limiting and caching.
    
    This service provides a wrapper around llama-index's GoogleGenAI LLM
    with built-in rate limiting for free tier usage.
    """
    
    def __init__(self, config: Optional[GeminiConfig] = None):
        self.config = config or GeminiConfig()
        self._rate_limit_state = RateLimitState()
        self._llm = None
        self._cache = None
        self._initialized = False
        
        # Initialize cache
        cache_path = Path(self.config.cache_dir)
        cache_path.mkdir(parents=True, exist_ok=True)
        self._cache = diskcache.Cache(str(cache_path))
        
        logger.info("Gemini service created", model=self.config.model)
    
    async def initialize(self) -> None:
        """Initialize the Gemini LLM."""
        if self._initialized:
            return
        
        try:
            # Import llama-index components
            from llama_index.llms.google_genai import GoogleGenAI
            from google.genai import types
            
            # Get API key
            api_key = os.getenv(self.config.api_key_env)
            if not api_key:
                raise ValueError(
                    f"API key not found. Set {self.config.api_key_env} environment variable."
                )
            
            # Initialize LLM
            self._llm = GoogleGenAI(
                model=self.config.model,
                api_key=api_key,
                generation_config=types.GenerateContentConfig(
                    temperature=self.config.temperature,
                    max_output_tokens=self.config.max_tokens,
                )
            )
            
            self._initialized = True
            logger.info("Gemini service initialized", model=self.config.model)
            
        except ImportError as e:
            logger.error("Failed to import llama-index", error=str(e))
            raise
        except Exception as e:
            logger.error("Failed to initialize Gemini service", error=str(e))
            raise
    
    def _check_rate_limit(self, estimated_tokens: int = 1000) -> bool:
        """
        Check if request is within rate limits.
        
        Args:
            estimated_tokens: Estimated token usage
            
        Returns:
            True if request is allowed
        """
        now = time.time()
        
        # Clean old timestamps
        self._rate_limit_state.cleanup_old_timestamps()
        
        # Check RPM
        if len(self._rate_limit_state.request_timestamps) >= self.config.rate_limit_rpm:
            logger.warning("RPM limit reached",
                         current=len(self._rate_limit_state.request_timestamps),
                         limit=self.config.rate_limit_rpm)
            return False
        
        # Check TPM
        if self._rate_limit_state.token_usage_minute + estimated_tokens > self.config.rate_limit_tpm:
            logger.warning("TPM limit approaching",
                         current=self._rate_limit_state.token_usage_minute,
                         limit=self.config.rate_limit_tpm)
            return False
        
        # Check RPD
        if self._rate_limit_state.daily_reset_time and datetime.now() > self._rate_limit_state.daily_reset_time:
            self._rate_limit_state.reset_daily()
        
        if self._rate_limit_state.token_usage_day + estimated_tokens > self.config.rate_limit_rpd * 1000:  # Approximate
            logger.warning("RPD limit approaching",
                         current=self._rate_limit_state.token_usage_day)
            return False
        
        return True
    
    def _record_request(self, tokens_used: int) -> None:
        """Record a request for rate limiting."""
        self._rate_limit_state.request_timestamps.append(time.time())
        self._rate_limit_state.token_usage_minute += tokens_used
        self._rate_limit_state.token_usage_day += tokens_used
    
    def _get_cache_key(self, prompt: str, **kwargs) -> str:
        """Generate cache key from prompt and parameters."""
        key_data = {
            "prompt": prompt,
            "model": self.config.model,
            "temperature": self.config.temperature,
            **kwargs
        }
        return json.dumps(key_data, sort_keys=True)
    
    async def complete(
        self,
        prompt: str,
        system_prompt: Optional[str] = None,
        **kwargs
    ) -> str:
        """
        Generate completion for a prompt.
        
        Args:
            prompt: User prompt
            system_prompt: Optional system prompt
            **kwargs: Additional parameters
            
        Returns:
            Generated text
        """
        if not self._initialized:
            await self.initialize()
        
        # Check cache
        cache_key = self._get_cache_key(prompt, system_prompt=system_prompt, **kwargs)
        cached_result = self._cache.get(cache_key)
        if cached_result is not None:
            logger.info("Cache hit", prompt_length=len(prompt))
            return cached_result
        
        # Check rate limit
        if not self._check_rate_limit(estimated_tokens=len(prompt) // 4):
            raise Exception("Rate limit exceeded. Please wait before making more requests.")
        
        try:
            start_time = time.time()
            
            # Prepare messages
            messages = []
            if system_prompt:
                messages.append({"role": "system", "content": system_prompt})
            messages.append({"role": "user", "content": prompt})
            
            # Generate response
            response = await self._llm.acomplete(prompt)
            response_text = str(response)
            
            # Record metrics
            elapsed = time.time() - start_time
            tokens_used = len(response_text) // 4  # Approximate
            self._record_request(tokens_used)
            
            # Cache result
            self._cache.set(cache_key, response_text, expire=self.config.cache_ttl)
            
            logger.info("Completion generated",
                       prompt_length=len(prompt),
                       response_length=len(response_text),
                       elapsed_seconds=elapsed,
                       tokens_used=tokens_used)
            
            return response_text
            
        except Exception as e:
            logger.error("Completion failed", error=str(e))
            raise
    
    async def chat(
        self,
        messages: List[Dict[str, str]],
        **kwargs
    ) -> str:
        """
        Generate chat completion.
        
        Args:
            messages: List of message dicts with 'role' and 'content'
            **kwargs: Additional parameters
            
        Returns:
            Generated response
        """
        if not self._initialized:
            await self.initialize()
        
        # Convert messages to prompt
        prompt_parts = []
        for msg in messages:
            role = msg.get("role", "user")
            content = msg.get("content", "")
            if role == "system":
                prompt_parts.append(f"System: {content}")
            elif role == "user":
                prompt_parts.append(f"User: {content}")
            elif role == "assistant":
                prompt_parts.append(f"Assistant: {content}")
        
        prompt = "\n".join(prompt_parts)
        
        # Check cache
        cache_key = self._get_cache_key(prompt, **kwargs)
        cached_result = self._cache.get(cache_key)
        if cached_result is not None:
            logger.info("Cache hit for chat")
            return cached_result
        
        # Check rate limit
        if not self._check_rate_limit(estimated_tokens=len(prompt) // 4):
            raise Exception("Rate limit exceeded")
        
        try:
            start_time = time.time()
            
            # Generate response
            response = await self._llm.acomplete(prompt)
            response_text = str(response)
            
            # Record metrics
            elapsed = time.time() - start_time
            tokens_used = len(response_text) // 4
            self._record_request(tokens_used)
            
            # Cache result
            self._cache.set(cache_key, response_text, expire=self.config.cache_ttl)
            
            logger.info("Chat completion generated",
                       messages_count=len(messages),
                       response_length=len(response_text),
                       elapsed_seconds=elapsed)
            
            return response_text
            
        except Exception as e:
            logger.error("Chat completion failed", error=str(e))
            raise
    
    async def analyze_with_context(
        self,
        analysis_prompt: str,
        context: Dict[str, Any],
        system_prompt: Optional[str] = None
    ) -> Dict[str, Any]:
        """
        Analyze data with structured output.
        
        Args:
            analysis_prompt: Prompt describing the analysis
            context: Context data to analyze
            system_prompt: Optional system prompt
            
        Returns:
            Structured analysis result
        """
        # Prepare full prompt with context
        full_prompt = f"""
{analysis_prompt}

Context Data:
{json.dumps(context, indent=2)}

Please provide your analysis in JSON format with the following structure:
{{
    "summary": "Brief summary of findings",
    "patterns": ["List of identified patterns"],
    "recommendations": ["List of recommendations"],
    "confidence": 0.0-1.0
}}
"""
        
        response = await self.complete(full_prompt, system_prompt=system_prompt)
        
        # Try to parse JSON from response
        try:
            # Find JSON in response
            json_start = response.find("{")
            json_end = response.rfind("}") + 1
            if json_start != -1 and json_end > json_start:
                json_str = response[json_start:json_end]
                return json.loads(json_str)
        except json.JSONDecodeError:
            pass
        
        # Return as text if JSON parsing fails
        return {"raw_response": response}
    
    def get_rate_limit_status(self) -> Dict[str, Any]:
        """Get current rate limit status."""
        self._rate_limit_state.cleanup_old_timestamps()
        
        return {
            "rpm_used": len(self._rate_limit_state.request_timestamps),
            "rpm_limit": self.config.rate_limit_rpm,
            "tpm_used": self._rate_limit_state.token_usage_minute,
            "tpm_limit": self.config.rate_limit_tpm,
            "rpd_used": self._rate_limit_state.token_usage_day,
            "rpd_limit": self.config.rate_limit_rpd,
            "cache_size": len(self._cache),
        }
    
    def clear_cache(self) -> None:
        """Clear the response cache."""
        self._cache.clear()
        logger.info("Cache cleared")
    
    async def shutdown(self) -> None:
        """Shutdown the service."""
        if self._cache:
            self._cache.close()
        self._initialized = False
        logger.info("Gemini service shut down")
