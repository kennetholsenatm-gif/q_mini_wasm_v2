#!/usr/bin/env python3
"""
MCP server that interfaces with the Foreman (and Smart Proxy) API for infrastructure
lifecycle: list/read/update hosts, quarantine, or create hosts (e.g. LXC).
Credentials from env: FOREMAN_API_URL, FOREMAN_USER, FOREMAN_PASSWORD (or FOREMAN_API_TOKEN).
Run as stdio MCP server: python foreman_provisioning_mcp.py
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


def _foreman_headers() -> dict | str:
    """Return auth headers for Foreman API or error string."""
    token = os.environ.get("FOREMAN_API_TOKEN")
    if token:
        return {"Content-Type": "application/json", "Accept": "application/json", "Authorization": f"Bearer {token}"}
    user = os.environ.get("FOREMAN_USER", "")
    password = os.environ.get("FOREMAN_PASSWORD", "")
    if not user or not password:
        return "Error: Set FOREMAN_API_TOKEN or (FOREMAN_USER and FOREMAN_PASSWORD)."
    creds = base64.b64encode(f"{user}:{password}".encode()).decode()
    return {"Content-Type": "application/json", "Accept": "application/json", "Authorization": f"Basic {creds}"}


def _foreman_request(method: str, path: str, body: dict | None = None, params: dict | None = None) -> str:
    """Send request to Foreman API. Returns JSON string or error message."""
    base_url = os.environ.get("FOREMAN_API_URL", "https://foreman.example.com").rstrip("/")
    headers = _foreman_headers()
    if isinstance(headers, str):
        return headers
    url = f"{base_url}{path}"
    if params:
        url += "?" + "&".join(f"{k}={v}" for k, v in params.items())
    try:
        import urllib.request
        data = json.dumps(body).encode() if body else None
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        with urllib.request.urlopen(req, timeout=30) as r:
            out = r.read().decode()
            return json.dumps(json.loads(out), indent=2) if out else "{}"
    except Exception as e:
        return f"Error calling Foreman API: {e}"


def foreman_list_hosts(search: str = "", per_page: int = 20) -> str:
    """List hosts (optional search filter)."""
    params = {"per_page": per_page}
    if search:
        params["search"] = search
    return _foreman_request("GET", "/api/v2/hosts", params=params)


def foreman_get_host(host_id_or_name: str) -> str:
    """Get a single host by ID or name."""
    if not host_id_or_name or not host_id_or_name.strip():
        return "Error: host_id_or_name is required."
    return _foreman_request("GET", f"/api/v2/hosts/{host_id_or_name.strip()}")


def foreman_update_host(host_id_or_name: str, host_params: dict) -> str:
    """Update host attributes (e.g. comment for quarantine, host_parameters)."""
    if not host_id_or_name or not host_id_or_name.strip():
        return "Error: host_id_or_name is required."
    return _foreman_request("PUT", f"/api/v2/hosts/{host_id_or_name.strip()}", body=host_params)


def foreman_set_host_parameter(host_id_or_name: str, name: str, value: str) -> str:
    """Set a host parameter (e.g. quarantine=true) via host update. Some Foreman versions require full host_parameters_attributes; if so, use foreman_update_host."""
    if not host_id_or_name or not name:
        return "Error: host_id_or_name and name are required."
    return _foreman_request("PUT", f"/api/v2/hosts/{host_id_or_name.strip()}", body={
        "host_parameters_attributes": [{"name": name.strip(), "value": value}],
    })


def foreman_create_host(name: str, organization_id: int | None = None, location_id: int | None = None, hostgroup_id: int | None = None, **kwargs: object) -> str:
    """Create a host (e.g. new LXC). Minimal: name; optional organization_id, location_id, hostgroup_id, and other host attributes."""
    body = {"name": name.strip(), **kwargs}
    if organization_id is not None:
        body["organization_id"] = organization_id
    if location_id is not None:
        body["location_id"] = location_id
    if hostgroup_id is not None:
        body["hostgroup_id"] = hostgroup_id
    return _foreman_request("POST", "/api/v2/hosts", body=body)


if HAS_MCP:
    server = Server("foreman-provisioning-mcp")

    @server.list_tools()
    async def list_tools() -> list[Tool]:
        return [
            Tool(
                name="foreman_list_hosts",
                description="List Foreman hosts with optional search filter.",
                inputSchema={
                    "type": "object",
                    "properties": {"search": {"type": "string"}, "per_page": {"type": "integer", "default": 20}},
                },
            ),
            Tool(
                name="foreman_get_host",
                description="Get a single host by ID or name.",
                inputSchema={"type": "object", "properties": {"host_id_or_name": {"type": "string"}}, "required": ["host_id_or_name"]},
            ),
            Tool(
                name="foreman_update_host",
                description="Update host attributes (e.g. comment for quarantine, build flag).",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "host_id_or_name": {"type": "string"},
                        "host_params": {"type": "object", "description": "Host attributes to update"},
                    },
                    "required": ["host_id_or_name", "host_params"],
                },
            ),
            Tool(
                name="foreman_set_host_parameter",
                description="Set a host parameter (e.g. quarantine=true).",
                inputSchema={
                    "type": "object",
                    "properties": {"host_id_or_name": {"type": "string"}, "name": {"type": "string"}, "value": {"type": "string"}},
                    "required": ["host_id_or_name", "name", "value"],
                },
            ),
            Tool(
                name="foreman_create_host",
                description="Create a new host (e.g. LXC). Provide name; optional organization_id, location_id, hostgroup_id.",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "name": {"type": "string"},
                        "organization_id": {"type": "integer"},
                        "location_id": {"type": "integer"},
                        "hostgroup_id": {"type": "integer"},
                    },
                    "required": ["name"],
                },
            ),
        ]

    @server.call_tool()
    async def call_tool(name: str, arguments: dict) -> list[TextContent]:
        if name == "foreman_list_hosts":
            result = foreman_list_hosts(
                search=arguments.get("search", ""),
                per_page=arguments.get("per_page", 20),
            )
        elif name == "foreman_get_host":
            result = foreman_get_host(arguments.get("host_id_or_name", ""))
        elif name == "foreman_update_host":
            result = foreman_update_host(
                arguments.get("host_id_or_name", ""),
                arguments.get("host_params", {}),
            )
        elif name == "foreman_set_host_parameter":
            result = foreman_set_host_parameter(
                arguments.get("host_id_or_name", ""),
                arguments.get("name", ""),
                arguments.get("value", ""),
            )
        elif name == "foreman_create_host":
            result = foreman_create_host(
                name=arguments.get("name", ""),
                organization_id=arguments.get("organization_id"),
                location_id=arguments.get("location_id"),
                hostgroup_id=arguments.get("hostgroup_id"),
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
        print(foreman_list_hosts(), file=sys.stderr)
        sys.exit(0)
