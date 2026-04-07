(function (globalScope) {
    'use strict';
    const CONTRACT_VERSION = "qmini.desktop.mcp_host.v1";
    if (globalScope.qMiniMcpHost && typeof globalScope.qMiniMcpHost.callTool === 'function') {
        return;
    }

    const webview = globalScope.chrome && globalScope.chrome.webview;
    if (!webview || typeof webview.postMessage !== 'function') {
        return;
    }

    let nextId = 1;
    const pending = new Map();

    if (typeof webview.addEventListener === 'function') {
        webview.addEventListener('message', (event) => {
            const payload = event && event.data ? event.data : {};
            if (!payload || payload.channel !== 'qmini-mcp-response') {
                return;
            }

            const wait = pending.get(payload.id);
            if (!wait) {
                return;
            }
            pending.delete(payload.id);

            if (payload.error) {
                wait.reject(new Error(payload.error.message || String(payload.error)));
            } else {
                wait.resolve(payload.result);
            }
        });
    }

    function callTool(name, args) {
        const id = nextId++;
        const payload = {
            channel: 'qmini-mcp-request',
            id,
            name,
            arguments: args || {},
        };

        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                pending.delete(id);
                reject(new Error('MCP request timeout: ' + name));
            }, 15000);

            pending.set(id, {
                resolve(value) {
                    clearTimeout(timer);
                    resolve(value);
                },
                reject(error) {
                    clearTimeout(timer);
                    reject(error);
                }
            });

            webview.postMessage(payload);
        });
    }

    globalScope.qMiniMcpHost = {
        contractVersion: CONTRACT_VERSION,
        callTool,
        describe() {
            return {
                contract_version: CONTRACT_VERSION,
                adapter: 'webview2-message-channel',
                channel: 'qmini-mcp-request',
            };
        },
        ping() {
            return callTool('ping', {});
        }
    };

    globalScope.dispatchEvent(new CustomEvent('qminiDesktopHostReady', {
        detail: {
            contract_version: CONTRACT_VERSION,
            adapter: 'webview2-message-channel',
        }
    }));
})(typeof window !== 'undefined' ? window : globalThis);
