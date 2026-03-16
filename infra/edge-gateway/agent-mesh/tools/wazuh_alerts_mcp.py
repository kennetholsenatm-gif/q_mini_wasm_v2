#!/usr/bin/env python3
"""
MCP server that exposes Wazuh alerts from the neighboring security-ids LXC/container.
Reads from Wazuh API (WAZUH_API_URL, WAZUH_USER, WAZUH_PASSWORD from env).
Run as stdio MCP server: python wazuh_alerts_mcp.py
"""

from __future__ import annotations

import os
import sys

# MCP server implementation: use mcp package if available for stdio transport
try:
    from mcp.server.stdio import stdio_server
    from mcp.server import Server
    from mcp.types import Tool, TextContent

    HAS_MCP = True
except ImportError:
    HAS_MCP = False


def get_wazuh_alerts(limit: int = 50) -> str:
    """Fetch recent alerts from Wazuh API (security-ids container)."""
    api_url = os.environ.get("WAZUH_API_URL", "http://security-ids:55000")
    user = os.environ.get("WAZUH_USER", "wazuh-wui")
    password = os.environ.get("WAZUH_PASSWORD", "")
    if not password:
        return "WAZUH_PASSWORD not set; cannot query Wazuh API."
    try:
        import urllib.request
        import base64
        import json

        req = urllib.request.Request(
            f"{api_url.rstrip('/')}/alerts?limit={limit}",
            headers={
                "Authorization": "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode()
            },
        )
        with urllib.request.urlopen(req, timeout=10) as r:
            data = json.loads(r.read().decode())
        return json.dumps(data, indent=2)
    except Exception as e:
        return f"Error fetching Wazuh alerts: {e}"


if HAS_MCP:
    server = Server("wazuh-alerts-mcp")

    @server.list_tools()
    async def list_tools() -> list[Tool]:
        return [
            Tool(
                name="read_wazuh_alerts",
                description="Read recent Wazuh security alerts from the neighboring security-ids container. Returns alert list as JSON.",
                inputSchema={
                    "type": "object",
                    "properties": {"limit": {"type": "integer", "default": 50}},
                },
            )
        ]

    @server.call_tool()
    async def call_tool(name: str, arguments: dict) -> list[TextContent]:
        if name == "read_wazuh_alerts":
            limit = arguments.get("limit", 50)
            result = get_wazuh_alerts(limit=limit)
            return [TextContent(type="text", text=result)]
        raise ValueError(f"Unknown tool: {name}")

    async def main():
        async with stdio_server() as (read_stream, write_stream):
            await server.run(read_stream, write_stream, server.create_initialization_options())

    if __name__ == "__main__":
        import asyncio

        asyncio.run(main())
else:
    # Fallback: print alerts to stdout for testing without MCP package
    if __name__ == "__main__":
        print(get_wazuh_alerts(), file=sys.stderr)
        sys.exit(0)
