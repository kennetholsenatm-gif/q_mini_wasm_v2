#!/usr/bin/env python3
"""
MCP server that queries the local Zabbix proxy REST API for hardware/network metrics.
Credentials from env: ZABBIX_API_URL, ZABBIX_API_TOKEN (or ZABBIX_USER, ZABBIX_PASSWORD).
Run as stdio MCP server: python zabbix_metrics_mcp.py
"""

from __future__ import annotations

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


def _zabbix_request(method: str, params: dict) -> str:
    """Send JSON-RPC request to Zabbix API. Returns JSON string or error message."""
    api_url = os.environ.get("ZABBIX_API_URL", "http://localhost/zabbix/api_jsonrpc.php").rstrip(
        "/"
    )
    token = os.environ.get("ZABBIX_API_TOKEN")
    if not token:
        return "Error: ZABBIX_API_TOKEN not set. Obtain a token from Zabbix (API tokens or user.login)."
    try:
        import urllib.request

        body = {"jsonrpc": "2.0", "method": method, "params": params, "auth": token, "id": 1}
        data = json.dumps(body).encode()
        req = urllib.request.Request(
            api_url,
            data=data,
            headers={"Content-Type": "application/json-rpc"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=15) as r:
            out = json.loads(r.read().decode())
        if "error" in out:
            return f"Zabbix API error: {out['error']}"
        return json.dumps(out.get("result", out), indent=2)
    except Exception as e:
        return f"Error querying Zabbix API: {e}"


def get_zabbix_hosts() -> str:
    """Return list of hosts from Zabbix (for context)."""
    return _zabbix_request(
        "host.get", {"output": ["hostid", "host", "name"], "selectGroups": ["name"]}
    )


def get_zabbix_items(
    host_ids: list[str] | None = None, key_pattern: str | None = None, limit: int = 100
) -> str:
    """Return items (metrics) from Zabbix. Optionally filter by host IDs or key pattern."""
    params = {"output": ["itemid", "name", "key_", "hostid", "lastvalue", "units"], "limit": limit}
    if host_ids:
        params["hostids"] = host_ids
    if key_pattern:
        params["search"] = {"key_": key_pattern}
    return _zabbix_request("item.get", params)


def get_zabbix_triggers(only_problem: bool = True, limit: int = 50) -> str:
    """Return triggers (alarms). If only_problem, only firing triggers."""
    params = {"output": "extend", "selectHosts": ["host"], "limit": limit}
    if only_problem:
        params["filter"] = {"value": 1}
    return _zabbix_request("trigger.get", params)


if HAS_MCP:
    server = Server("zabbix-metrics-mcp")

    @server.list_tools()
    async def list_tools() -> list[Tool]:
        return [
            Tool(
                name="get_zabbix_hosts",
                description="List hosts from the local Zabbix proxy (hardware/network inventory).",
                inputSchema={"type": "object", "properties": {}},
            ),
            Tool(
                name="get_zabbix_items",
                description="Get metric items from Zabbix (CPU, network, disk, etc.). Optional host_ids list or key_pattern filter.",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "host_ids": {
                            "type": "array",
                            "items": {"type": "string"},
                            "description": "Filter by host IDs",
                        },
                        "key_pattern": {
                            "type": "string",
                            "description": "Filter items by key pattern (e.g. net.if)",
                        },
                        "limit": {"type": "integer", "default": 100},
                    },
                },
            ),
            Tool(
                name="get_zabbix_triggers",
                description="Get Zabbix triggers (alarms). Returns only problem triggers by default.",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "only_problem": {"type": "boolean", "default": True},
                        "limit": {"type": "integer", "default": 50},
                    },
                },
            ),
        ]

    @server.call_tool()
    async def call_tool(name: str, arguments: dict) -> list[TextContent]:
        if name == "get_zabbix_hosts":
            result = get_zabbix_hosts()
        elif name == "get_zabbix_items":
            result = get_zabbix_items(
                host_ids=arguments.get("host_ids"),
                key_pattern=arguments.get("key_pattern"),
                limit=arguments.get("limit", 100),
            )
        elif name == "get_zabbix_triggers":
            result = get_zabbix_triggers(
                only_problem=arguments.get("only_problem", True),
                limit=arguments.get("limit", 50),
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
        print(get_zabbix_hosts(), file=sys.stderr)
        sys.exit(0)
