/**
 * @deprecated
 * Legacy WebSocket bridge has been intentionally disabled for production.
 *
 * Production desktop control path:
 *   wui pages -> mcp-host-bridge.js -> MCP host tools
 *
 * This tombstone module is retained only to make accidental legacy loads fail
 * fast with an explicit operator/developer error.
 */
(function attachDeprecatedWebSocketBridge(globalScope) {
    'use strict';

    const DEPRECATION_MESSAGE = [
        '[q_mini_wasm_v2] websocket-bridge.js is deprecated and disabled.',
        'Use wui/js/mcp-host-bridge.js and MCP host adapters instead.',
    ].join(' ');

    function failDeprecatedBridgeUse() {
        throw new Error(DEPRECATION_MESSAGE);
    }

    if (typeof console !== 'undefined' && typeof console.error === 'function') {
        console.error(DEPRECATION_MESSAGE);
    }

    class DeprecatedWebSocketBridge {
        constructor() {
            failDeprecatedBridgeUse();
        }
    }

    globalScope.WebSocketBridge = DeprecatedWebSocketBridge;
    globalScope.createWebSocketBridge = failDeprecatedBridgeUse;
})(typeof window !== 'undefined' ? window : globalThis);

