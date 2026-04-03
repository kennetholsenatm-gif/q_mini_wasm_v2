"""
RAG Client for Agent Intelligence

Provides integration with the qminiwasm-rag-service MCP server
for context-aware agent decision making.
"""

import asyncio
import json
import logging
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional
from pathlib import Path

import httpx
import structlog

logger = structlog.get_logger()


@dataclass
class RAGChunk:
    """A retrieved context chunk."""
    content: str
    source_file: str
    start_line: int
    end_line: int
    score: float
    chunk_type: str = "text"


@dataclass
class RAGResult:
    """Result from RAG retrieval."""
    chunks: List[RAGChunk] = field(default_factory=list)
    total_tokens: int = 0
    latency_ms: int = 0
    query_complexity: float = 0.0
    
    @property
    def context_text(self) -> str:
        """Get concatenated context text."""
        parts = []
        for chunk in self.chunks:
            parts.append(f"--- {chunk.source_file} (lines {chunk.start_line}-{chunk.end_line}) ---")
            parts.append(chunk.content)
        return "\n".join(parts)
    
    @property
    def source_files(self) -> List[str]:
        """Get unique source files."""
        return list(set(c.source_file for c in self.chunks))


class RAGClient:
    """Client for the qminiwasm-rag-service."""
    
    def __init__(
        self,
        http_endpoint: str = "http://localhost:8088",
        timeout: float = 30.0,
        cache_ttl: int = 300
    ):
        self.http_endpoint = http_endpoint.rstrip("/")
        self.timeout = timeout
        self.cache_ttl = cache_ttl
        self._client: Optional[httpx.AsyncClient] = None
        self._cache: Dict[str, tuple] = {}
        
    async def _get_client(self) -> httpx.AsyncClient:
        """Get or create HTTP client."""
        if self._client is None or self._client.is_closed:
            self._client = httpx.AsyncClient(
                base_url=self.http_endpoint,
                timeout=self.timeout
            )
        return self._client
    
    async def close(self) -> None:
        """Close the HTTP client."""
        if self._client and not self._client.is_closed:
            await self._client.aclose()
    
    def _get_cache_key(self, query: str, max_tokens: int, context_type: str) -> str:
        """Generate cache key."""
        return f"{query}:{max_tokens}:{context_type}"
    
    def _is_cache_valid(self, timestamp: float) -> bool:
        """Check if cache entry is valid."""
        import time
        return (time.time() - timestamp) < self.cache_ttl
    
    async def retrieve_context(
        self,
        query: str,
        max_tokens: int = 0,
        context_type: str = "all"
    ) -> RAGResult:
        """Retrieve relevant context for a query."""
        import time
        
        # Check cache
        cache_key = self._get_cache_key(query, max_tokens, context_type)
        if cache_key in self._cache:
            result, timestamp = self._cache[cache_key]
            if self._is_cache_valid(timestamp):
                logger.debug("RAG cache hit", query=query[:50])
                return result
        
        try:
            client = await self._get_client()
            response = await client.post(
                "/api/v1/rag/retrieve",
                json={
                    "query": query,
                    "max_tokens": max_tokens,
                    "context_type": context_type
                }
            )
            response.raise_for_status()
            data = response.json()
            
            # Parse response
            chunks = []
            for chunk_data in data.get("chunks", []):
                chunks.append(RAGChunk(
                    content=chunk_data.get("content", ""),
                    source_file=chunk_data.get("source_file", ""),
                    start_line=chunk_data.get("start_line", 0),
                    end_line=chunk_data.get("end_line", 0),
                    score=chunk_data.get("score", 0.0),
                    chunk_type=chunk_data.get("chunk_type", "text")
                ))
            
            result = RAGResult(
                chunks=chunks,
                total_tokens=data.get("total_tokens", 0),
                latency_ms=data.get("latency_ms", 0),
                query_complexity=data.get("query_complexity", 0.0)
            )
            
            # Cache result
            self._cache[cache_key] = (result, time.time())
            
            logger.info("RAG retrieval successful",
                       query=query[:50],
                       chunks=len(chunks),
                       tokens=result.total_tokens)
            
            return result
            
        except httpx.ConnectError:
            logger.warning("RAG service unavailable", endpoint=self.http_endpoint)
            return RAGResult()
        except Exception as e:
            logger.error("RAG retrieval failed", error=str(e))
            return RAGResult()
    
    async def get_metrics(self) -> Dict[str, Any]:
        """Get RAG service metrics."""
        try:
            client = await self._get_client()
            response = await client.get("/api/v1/rag/metrics")
            response.raise_for_status()
            return response.json()
        except Exception as e:
            logger.error("Failed to get RAG metrics", error=str(e))
            return {}
    
    async def index_document(self, file_path: str) -> bool:
        """Index a document into the RAG vector store."""
        try:
            client = await self._get_client()
            response = await client.post(
                "/api/v1/rag/index",
                json={"file_path": file_path}
            )
            response.raise_for_status()
            logger.info("Document indexed", file_path=file_path)
            return True
        except Exception as e:
            logger.error("Failed to index document", error=str(e))
            return False
    
    async def health_check(self) -> bool:
        """Check if RAG service is healthy."""
        try:
            client = await self._get_client()
            response = await client.get("/api/v1/health")
            return response.status_code == 200
        except Exception:
            return False