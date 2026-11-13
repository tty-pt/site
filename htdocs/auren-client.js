/**
 * @file auren-client.js
 * @brief Browser coordinator for local in-browser LLM inference with Qwen2.5 & SmolLM2.
 */

import { AurenBrowserEngine } from '/vendor/auren/index.js';

(function() {
  'use strict';

  let engine = null;
  let isGenerating = false;
  const conversationHistory = [
    { role: 'system', content: 'You are a concise, helpful assistant. Answer questions directly and accurately.' }
  ];

  function dispatchToWasm(actionId, dataStr) {
    try {
      const client = window.__stl_client || window.steleClient;
      if (client && typeof client.dispatchAction === 'function') {
        client.dispatchAction(0, actionId, dataStr);
        return true;
      }
    } catch {
      /* Fallback to direct DOM manipulation */
    }
    return false;
  }

  function initAuren() {
    const container = document.querySelector('.auren-container');
    if (!container) return;

    const badge = container.querySelector('.auren-badge');
    const form = document.querySelector('#auren-form');
    const input = document.querySelector('#auren-input');
    const quickButtons = document.querySelectorAll('.btn-chip');
    const viewport = document.querySelector('.auren-chat-viewport');
    const modelSelect = document.querySelector('#auren-model-select');

    /* 1. ATTACH FORM AND CHIP LISTENERS IMMEDIATELY */
    if (form) {
      form.addEventListener('submit', async (e) => {
        e.preventDefault();
        if (!input || !input.value.trim() || isGenerating) return;

        const promptText = input.value.trim();
        input.value = '';
        await runPrompt(promptText);
      });
    }

    quickButtons.forEach((btn) => {
      btn.addEventListener('click', async () => {
        const prompt = btn.getAttribute('data-prompt');
        if (prompt && !isGenerating) {
          await runPrompt(prompt);
        }
      });
    });

    if (modelSelect) {
      modelSelect.addEventListener('change', () => {
        const selectedModel = modelSelect.value;
        if (badge) {
          badge.className = 'auren-badge badge-gpu-wait';
          badge.textContent = `Loading ${selectedModel}...`;
        }
        if (engine && engine.worker) {
          engine.worker.postMessage({ type: 'init', modelKey: selectedModel });
        }
      });
    }

    /* 2. INITIALIZE WORKER ENGINE ASYNCHRONOUSLY */
    (async () => {
      try {
        engine = new AurenBrowserEngine({
          modelKey: 'smollm2',
          modelName: 'smollm2-135m-instruct-q4_k_m.gguf',
          modelUrl: '/models/smollm2-135m-instruct-q4_k_m.gguf',
          workerPath: '/vendor/auren/auren-worker.js'
        });

        await engine.init((progress) => {
          dispatchToWasm(4, String(progress.percent || 0));

          const fill = document.querySelector('.auren-progress-fill');
          const label = document.querySelector('.auren-progress-label');
          if (fill && progress.percent !== undefined) {
            fill.style.width = `${progress.percent}%`;
          }
          if (label) {
            if (progress.status === 'cached' || progress.percent >= 100) {
              label.textContent = 'Model loaded and ready (WebAssembly llama.cpp)';
            } else if (progress.status === 'downloading') {
              label.textContent = `Downloading model weights: ${progress.percent}%`;
            } else if (progress.status === 'initializing_wasm') {
              label.textContent = 'Initializing WebAssembly llama.cpp runtime...';
            }
          }
        });

        dispatchToWasm(5, '1');
        if (badge) {
          badge.className = 'auren-badge badge-gpu-on';
          badge.textContent = 'WebAssembly Active (Local Hardware)';
        }
        const label = document.querySelector('.auren-progress-label');
        if (label) {
          label.textContent = 'Model loaded and ready (WebAssembly llama.cpp)';
        }
      } catch (err) {
        console.warn('[auren-client] Engine initialization note:', err.message);
        if (badge) {
          badge.className = 'auren-badge badge-gpu-wait';
          badge.textContent = 'Model Init Failed';
        }
        const label = document.querySelector('.auren-progress-label');
        if (label) {
          label.textContent = `Error loading model: ${err.message}`;
        }
      }
    })();

    async function runPrompt(text) {
      isGenerating = true;

      /* Record user message in conversational memory */
      conversationHistory.push({ role: 'user', content: text });

      const wasmHandled = dispatchToWasm(1, text);

      let assistantMsg = null;
      let bodyEl = null;
      let cursor = null;

      if (!wasmHandled) {
        appendMessage('user', 'User', text);

        assistantMsg = appendMessage('assistant', 'Assistant (Local GPU)', '');
        bodyEl = assistantMsg.querySelector('.auren-msg-body');
        cursor = document.createElement('span');
        cursor.className = 'auren-cursor';
        bodyEl.appendChild(cursor);
      }

      let accumulated = '';

      try {
        if (!engine || !engine.isReady) {
          /* Await engine initialization if in progress */
          for (let i = 0; i < 50; i++) {
            if (engine && engine.isReady) break;
            await new Promise((r) => setTimeout(r, 100));
          }
        }

        if (engine && engine.isReady) {
          for await (const token of engine.chatStream({
            messages: conversationHistory,
            prompt: text,
            maxTokens: 128
          })) {
            accumulated += token;
            if (wasmHandled) {
              dispatchToWasm(2, token);
            } else if (bodyEl && cursor) {
              bodyEl.insertBefore(document.createTextNode(token), cursor);
              if (viewport) viewport.scrollTop = viewport.scrollHeight;
            }
          }
        } else {
          const statusText = `Engine initializing. Please try again in a moment.`;
          accumulated = statusText;
          if (wasmHandled) {
            dispatchToWasm(2, statusText);
          } else if (bodyEl && cursor) {
            bodyEl.insertBefore(document.createTextNode(statusText), cursor);
          }
        }

        /* Record assistant response in conversational memory */
        if (accumulated.trim()) {
          conversationHistory.push({ role: 'assistant', content: accumulated.trim() });
        }
        dispatchToWasm(3, '');
      } catch (err) {
        if (bodyEl && cursor) {
          bodyEl.insertBefore(document.createTextNode(` [Error: ${err.message}]`), cursor);
        }
      } finally {
        if (cursor && cursor.parentNode) cursor.remove();
        isGenerating = false;
        if (viewport) viewport.scrollTop = viewport.scrollHeight;
      }
    }

    function appendMessage(role, label, text) {
      const emptyState = document.querySelector('.auren-empty-state');
      if (emptyState) emptyState.remove();

      const msgEl = document.createElement('div');
      msgEl.className = `auren-msg msg-${role}`;

      const headerEl = document.createElement('div');
      headerEl.className = 'auren-msg-header';
      headerEl.textContent = label;

      const bodyEl = document.createElement('div');
      bodyEl.className = 'auren-msg-body';
      bodyEl.textContent = text;

      msgEl.appendChild(headerEl);
      msgEl.appendChild(bodyEl);

      if (viewport) {
        viewport.appendChild(msgEl);
        viewport.scrollTop = viewport.scrollHeight;
      }
      return msgEl;
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initAuren);
  } else {
    initAuren();
  }
})();
