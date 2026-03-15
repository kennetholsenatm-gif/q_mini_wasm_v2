#!/usr/bin/env python3
"""
MCP server that queries the local Netdisco instance for LLDP/CDP topology and MAC-to-port.
Credentials from env: NETDISCO_API_URL, NETDISCO_USER, NETDISCO_PASSWORD (or NETDISCO_API_KEY).
Run as stdio MCP server: python netdisco_topology_mcp.py
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


def _netdisco_auth() -> tuple[str, dict] | str:
    """Return (base_url, headers) or error string. Uses NETDISCO_API_KEY or login with user/password."""
    api_url = os.environ.get("NETDISCO_API_URL", "http://localhost:5000").rstrip("/")
    key = os.environ.get("NETDISCO_API_KEY")
    if key:
        return (api_url, {"Accept": "application/json", "Authorization": key})
    user = os.environ.get("NETDISCO_USER", "")
    password = os.environ.get("NETDISCO_PASSWORD", "")
    if not user or not password:
        return "Error: Set NETDISCO_API_KEY or (NETDISCO_USER and NETDISCO_PASSWORD)."
    try:
        import urllib.request
        req = urllib.request.Request(
            f"{api_url}/login",
            data=b"",
            headers={"Accept": "application/json"},
            method="POST",
        )
        req.add_header("Authorization", "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode())
        with urllib.request.urlopen(req, timeout=10) as r:
            data = json.loads(r.read().decode())
        token = data.get("api_key")
        if not token:
            return "Error: Netdisco login did not return api_key."
        return (api_url, {"Accept": "application/json", "Authorization": token})
    except Exception as e:
        return f"Error logging in to Netdisco: {e}"


def _netdisco_get(path: str, params: dict | None = None) -> str:
    """GET from Netdisco API. Returns JSON string or error message."""
    auth = _netdisco_auth()
    if isinstance(auth, str):
        return auth
    base_url, headers = auth
    url = f"{base_url}{path}"
    if params:
        url += "?" + "&".join(f"{k}={v}" for k, v in params.items())
    try:
        import urllib.request
        req = urllib.request.Request(url, headers=headers, method="GET")
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.dumps(json.loads(r.read().decode()), indent=2)
    except Exception as e:
        return f"Error querying Netdisco API: {e}"


def get_netdisco_node_search(query: str) -> str:
    """Find node by IP or MAC; returns MACs and sightings (switch/port) for MAC-to-port mapping."""
    if not query or not query.strip():
        return "Error: query (IP or MAC address) is required."
    return _netdisco_get("/api/v1/search/node", {"q": query.strip()})


def get_netdisco_device_nodes(device_ip: str) -> str:
    """Get all nodes (MAC-to-port) on a device (switch) by its IP."""
    if not device_ip or not device_ip.strip():
        return "Error: device_ip is required."
    return _netdisco_get(f"/api/v1/object/device/{device_ip.strip()}/nodes")


def get_netdisco_device(device_ip: str) -> str:
    """Get device details by IP (includes port/neighbor context)."""
    if not device_ip or not device_ip.strip():
        return "Error: device_ip is required."
    return _netdisco_get(f"/api/v1/object/device/{device_ip.strip()}")


def get_netdisco_report(report_name: str = "device_portutilization") -> str:
    """Run a Netdisco report (e.g. device_portutilization, device_neighbors). Returns report JSON."""
    return _netdisco_get(f"/api/v1/report/{report_name}")


if HAS_MCP:
    server = Server("netdisco-topology-mcp")

    @server.list_tools()
    async def list_tools() -> list[Tool]:
        return [
            Tool(
                name="get_netdisco_node_search",
                description="Search Netdisco by IP or MAC to get MAC-to-port mappings (which switch/port the node is on).",
                inputSchema={"type": "object", "properties": {"query": {"type": "string", "description": "IP or MAC address"}}, "required": ["query"]},
            ),
            Tool(
                name="get_netdisco_device_nodes",
                description="Get all nodes (MAC addresses on ports) for a device (switch) by its IP.",
                inputSchema={"type": "object", "properties": {"device_ip": {"type": "string"}}, "required": ["device_ip"]},
            ),
            Tool(
                name="get_netdisco_device",
                description="Get device details from Netdisco by device IP.",
                inputSchema={"type": "object", "properties": {"device_ip": {"type": "string"}}, "required": ["device_ip"]},
            ),
            Tool(
                name="get_netdisco_report",
                description="Run a Netdisco report (e.g. device_portutilization, device_neighbors).",
                inputSchema={"type": "object", "properties": {"report_name": {"type": "string", "default": "device_portutilization"}}},
            ),
        ]

    @server.call_tool()
    async def call_tool(name: str, arguments: dict) -> list[TextContent]:
        if name == "get_netdisco_node_search":
            result = get_netdisco_node_search(arguments.get("query", ""))
        elif name == "get_netdisco_device_nodes":
            result = get_netdisco_device_nodes(arguments.get("device_ip", ""))
        elif name == "get_netdisco_device":
            result = get_netdisco_device(arguments.get("device_ip", ""))
        elif name == "get_netdisco_report":
            result = get_netdisco_report(arguments.get("report_name", "device_portutilization"))
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
        print(get_netdisco_report(), file=sys.stderr)
        sys.exit(0)
