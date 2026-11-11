/*
 * chat.js — Form-to-WebSocket bridge for chat module
 * Intercepts chat form submissions and sends them as WebSocket messages
 * instead of HTTP POST requests, enabling real-time token streaming.
 */

(function() {
  'use strict';

  // WebSocket connection pool by session ID
  const wsConnections = {};

  function getWebSocketForSession(sessionId) {
    if (wsConnections[sessionId] && wsConnections[sessionId].readyState === WebSocket.OPEN) {
      return wsConnections[sessionId];
    }

    const wsUrl = `/chat/${sessionId}`;
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(`${protocol}//${window.location.host}${wsUrl}`);
    wsConnections[sessionId] = ws;

    ws.onmessage = function(event) {
      if (typeof window.wasm_ws_on_message === 'function') {
        window.wasm_ws_on_message(event.data, event.data.length);
      }
    };

    ws.onclose = function() {
      delete wsConnections[sessionId];
    };

    return ws;
  }

  function initChatForms() {
    const form = document.querySelector('.chat-input-row');
    if (!form || form.dataset.chatBound) return;
    form.dataset.chatBound = '1';

    form.addEventListener('submit', function(e) {
      const sessionId = form.dataset.wsSession;
      if (!sessionId || typeof WebSocket === 'undefined') return;

      const input = form.querySelector('input[name="prompt"]');
      if (!input || !input.value.trim()) return;

      const ws = getWebSocketForSession(sessionId);
      if (ws.readyState === WebSocket.OPEN) {
        e.preventDefault();
        const payload = JSON.stringify({ prompt: input.value.trim() });
        ws.send(payload);
        input.value = '';
      }
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initChatForms);
  } else {
    initChatForms();
  }
})();
