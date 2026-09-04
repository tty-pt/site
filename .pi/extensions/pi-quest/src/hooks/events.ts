import type { ExtensionAPI } from "../index.ts";

export type EventHandler = (...args: never[]) => unknown;

export function onSessionStart(_pi: ExtensionAPI, _handler: EventHandler): void {
  // S1: subscribe session start.
}

export function onTurnBoundary(_pi: ExtensionAPI, _handler: EventHandler): void {
  // S1: subscribe turn start/end.
}

export function onDraftEdit(_pi: ExtensionAPI, _handler: EventHandler): void {
  // S2: subscribe draft-file writes.
}

export function onSetback(_pi: ExtensionAPI, _handler: EventHandler): void {
  // S3: subscribe tool results signalling setbacks.
}

export function onCompletionClaim(_pi: ExtensionAPI, _handler: EventHandler): void {
  // S3: subscribe completion claims.
}

export function onChildReturn(_pi: ExtensionAPI, _handler: EventHandler): void {
  // S3: subscribe sub-quest completions.
}

export function onUserMessage(_pi: ExtensionAPI, _handler: EventHandler): void {
  // S3: subscribe user messages for late answers.
}
