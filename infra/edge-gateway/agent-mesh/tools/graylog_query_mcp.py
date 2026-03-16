#!/usr/bin/env python3
"""
MCP server that interfaces with the Graylog REST API for log search by timeframe, IP, or event ID.
Credentials from env: GRAYLOG_API_URL, GRAYLOG_USER, GRAYLOG_PASSWORD.
Run as stdio MCP server: python graylog_query_mcp.py
"""

from __future__ import annotations

import base64
import json
import os
import sys

try:
    from mcp.server.stdio import stdio_server
    from mcp.server import Server
    from mcp.types import Tool, TextContent

    HAS_MCP = True
except ImportError:
    HAS_MCP = False


def _graylog_request(
    method: str, path: str, body: dict | None = None, params: dict | None = None
) -> str:
    """Send request to Graylog API. Returns JSON string or error message."""
    base_url = os.environ.get("GRAYLOG_API_URL", "http://localhost:9000").rstrip("/")
    user = os.environ.get("GRAYLOG_USER", "")
    password = os.environ.get("GRAYLOG_PASSWORD", "")
    if not user or not password:
        return "Error: GRAYLOG_USER and GRAYLOG_PASSWORD must be set."
    creds = base64.b64encode(f"{user}:{password}".encode()).decode()
    headers = {
        "Accept": "application/json",
        "Content-Type": "application/json",
        "Authorization": f"Basic {creds}",
        "X-Requested-By": "edge-agent-mcp",
    }
    url = f"{base_url}{path}"
    if params:
        url += "?" + "&".join(f"{k}={v}" for k, v in params.items())
    try:
        import urllib.request

        data = json.dumps(body).encode() if body else None
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.dumps(json.loads(r.read().decode()), indent=2)
    except Exception as e:
        return f"Error querying Graylog API: {e}"


def graylog_search(
    query: str = "*",
    from_seconds: int = 300,
    size: int = 50,
    streams: list[str] | None = None,
    fields: list[str] | None = None,
) -> str:
    """Search Graylog logs. query: search string (e.g. source:1.2.3.4 or event_id:123); from_seconds: last N seconds; optional streams (IDs), fields."""
    timerange = {"type": "relative", "from": from_seconds}
    body = {"query": query or "*", "timerange": timerange, "size": size}
    if streams:
        body["streams"] = streams
    if fields:
        body["fields"] = fields
    return _graylog_request("POST", "/api/search/messages", body=body)


def graylog_search_keyword(
    query: str = "*",
    keyword: str = "last 5 minutes",
    size: int = 50,
    streams: list[str] | None = None,
) -> str:
    """Search Graylog with keyword timerange (e.g. 'last 1 hour', 'yesterday')."""
    timerange = {"type": "keyword", "keyword": keyword}
    body = {"query": query or "*", "timerange": timerange, "size": size}
    if streams:
        body["streams"] = streams
    return _graylog_request("POST", "/api/search/messages", body=body)


def graylog_search_absolute(
    query: str = "*", from_iso: str = "", to_iso: str = "", size: int = 50
) -> str:
    """Search Graylog with absolute timerange (ISO 8601 from_iso and to_iso)."""
    if not from_iso or not to_iso:
        return "Error: from_iso and to_iso (ISO 8601) are required for absolute search."
    timerange = {"type": "absolute", "from": from_iso, "to": to_iso}
    body = {"query": query or "*", "timerange": timerange, "size": size}
    return _graylog_request("POST", "/api/search/messages", body=body)


if HAS_MCP:
    server = Server("graylog-query-mcp")

    @server.list_tools()
    async def list_tools() -> list[Tool]:
        return [
            Tool(
                name="graylog_search",
                description="Search Graylog logs by query (e.g. source:IP or event_id:ID), last N seconds, optional streams and fields.",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "query": {
                            "type": "string",
                            "default": "*",
                            "description": "Graylog query (e.g. source:192.168.1.1 or event_id:123)",
                        },
                        "from_seconds": {
                            "type": "integer",
                            "default": 300,
                            "description": "Last N seconds",
                        },
                        "size": {"type": "integer", "default": 50},
                        "streams": {"type": "array", "items": {"type": "string"}},
                        "fields": {"type": "array", "items": {"type": "string"}},
                    },
                },
            ),
            Tool(
                name="graylog_search_keyword",
                description="Search Graylog with keyword timerange (e.g. 'last 1 hour', 'yesterday').",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "query": {"type": "string", "default": "*"},
                        "keyword": {"type": "string", "default": "last 5 minutes"},
                        "size": {"type": "integer", "default": 50},
                        "streams": {"type": "array", "items": {"type": "string"}},
                    },
                },
            ),
            Tool(
                name="graylog_search_absolute",
                description="Search Graylog with absolute time range (ISO 8601 from_iso, to_iso).",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "query": {"type": "string", "default": "*"},
                        "from_iso": {"type": "string"},
                        "to_iso": {"type": "string"},
                        "size": {"type": "integer", "default": 50},
                    },
                    "required": ["from_iso", "to_iso"],
                },
            ),
        ]

    @server.call_tool()
    async def call_tool(name: str, arguments: dict) -> list[TextContent]:
        if name == "graylog_search":
            result = graylog_search(
                query=arguments.get("query", "*"),
                from_seconds=arguments.get("from_seconds", 300),
                size=arguments.get("size", 50),
                streams=arguments.get("streams"),
                fields=arguments.get("fields"),
            )
        elif name == "graylog_search_keyword":
            result = graylog_search_keyword(
                query=arguments.get("query", "*"),
                keyword=arguments.get("keyword", "last 5 minutes"),
                size=arguments.get("size", 50),
                streams=arguments.get("streams"),
            )
        elif name == "graylog_search_absolute":
            result = graylog_search_absolute(
                query=arguments.get("query", "*"),
                from_iso=arguments.get("from_iso", ""),
                to_iso=arguments.get("to_iso", ""),
                size=arguments.get("size", 50),
            )
        else:
            raise ValueError(f"Unknown tool: {name}")
        return [TextContent(type="text", text=result)]

    async def main():
        async with stdio_server() as (read_stream, write_stream):
            await server.run(read_stream, write_stream, server.create_initialization_options())

    if __name__ == "__main__":
        import asyncio

        asyncio.run(main())
else:
    if __name__ == "__main__":
        print(graylog_search(), file=sys.stderr)
        sys.exit(0)
