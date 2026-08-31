import { getSubquestCompactThreshold } from "../compaction.ts";
import { DEFAULT_CHECKPOINT_INTERVAL_TURNS, DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS } from "../constants.ts";
import { calculateCurrentTokens } from "../context.ts";
import { persist } from "../persistence.ts";
import { state } from "../state.ts";
import { ExtensionAPI, ExtensionContext } from "../types.ts";
import { formatTokens, parseTokenAmount } from "../utils.ts";

function notifyInfo(ctx: ExtensionContext, msg: string): void { if (ctx.hasUI) ctx.ui.notify(msg, "info"); }
function notifyWarning(ctx: ExtensionContext, msg: string): void { if (ctx.hasUI) ctx.ui.notify(msg, "warning"); }
function currentTokenStr(ctx: ExtensionContext): string {
	const tokens = calculateCurrentTokens(ctx);
	return tokens !== null ? formatTokens(tokens) : "unknown";
}
function isDefaultArg(trimmed: string): boolean { return trimmed.toLowerCase() === "default"; }
function isOffArg(trimmed: string): boolean { const t = trimmed.toLowerCase(); return t === "off" || t === "disable" || t === "disabled" || t === "0"; }
function handleEconomyStatus(ctx: ExtensionContext): void {
	const currentSubquest = getSubquestCompactThreshold();
	const tokenStr = currentTokenStr(ctx);
	const subStr = `${formatTokens(currentSubquest)} tokens (${currentSubquest.toLocaleString()})`;
	notifyInfo(ctx, `Quest checkpoint: periodic every ${DEFAULT_CHECKPOINT_INTERVAL_TURNS} substantive turns when dirty (token threshold removed). subquest launch limit = ${subStr}. Current usage = ${tokenStr} tokens. Usage: /quest-economy [subquestLaunch] or /quest-subquest-threshold <tokens>`);
}
function resetEconomyToDefault(ctx: ExtensionContext, pi: ExtensionAPI): void {
	state.subquestCompactTokens = null;
	state.economyTokens = null; state.economyPercent = null; state.warningMarginTokens = null;
	persist(pi, ctx);
	notifyInfo(ctx, `Quest checkpoint: reset to default (periodic ${DEFAULT_CHECKPOINT_INTERVAL_TURNS} turns, subquest = ${formatTokens(getSubquestCompactThreshold())}).`);
}
function disableEconomy(ctx: ExtensionContext, pi: ExtensionAPI): void {
	state.subquestCompactTokens = 0; persist(pi, ctx);
	notifyInfo(ctx, `Sub-quest launch compaction disabled. Periodic checkpoint still requires saves before compaction.`);
}
export async function handleQuestEconomyCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	const trimmed = args.trim();
	if (!trimmed) { handleEconomyStatus(ctx); return; }
	if (isDefaultArg(trimmed)) { resetEconomyToDefault(ctx, pi); return; }
	if (isOffArg(trimmed)) { disableEconomy(ctx, pi); return; }
	const parts = trimmed.split(/\s+/);
	// Legacy: first two args were token thresholds — now ignored, warn.
	if (parts.length >= 1) {
		notifyWarning(ctx, `Token thresholds removed; periodic checkpoint is every ${DEFAULT_CHECKPOINT_INTERVAL_TURNS} substantive turns. Only subquest launch threshold is configurable via third arg or /quest-subquest-threshold.`);
	}
	if (parts.length > 2) { const parsedSub = parseTokenAmount(parts[2], DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS); if (parsedSub !== null && parsedSub >= 0) { state.subquestCompactTokens = parsedSub; persist(pi, ctx); notifyInfo(ctx, `Sub-quest launch compaction threshold set to ${formatTokens(parsedSub)} tokens (${parsedSub.toLocaleString()}).`); return; } }
	if (parts.length === 1 || parts.length === 2) {
		const maybeSub = parseTokenAmount(parts[parts.length-1], DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);
		// If single numeric arg without token-threshold semantics, treat as subquest threshold for compat
		if (maybeSub !== null && parts.length === 1 && maybeSub < 100000) { /* ambiguous; still no-op */}
	}
	persist(pi, ctx);
	notifyInfo(ctx, `Quest checkpoint: periodic every ${DEFAULT_CHECKPOINT_INTERVAL_TURNS} turns. subquest launch limit = ${formatTokens(getSubquestCompactThreshold())}. Current usage: ${currentTokenStr(ctx)}.`);
}
function handleWarningStatus(ctx: ExtensionContext): void {
	notifyInfo(ctx, `Pre-compaction warning removed; periodic checkpoint every ${DEFAULT_CHECKPOINT_INTERVAL_TURNS} substantive turns. Configure subquest threshold via /quest-subquest-threshold.`);
}
export async function handleQuestWarningCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	const trimmed = args.trim();
	if (!trimmed) { handleWarningStatus(ctx); return; }
	notifyWarning(ctx, `Token-based warning removed; checkpoint is periodic. Use /quest-subquest-threshold for subquest launch limit.`);
}
function handleSubquestThresholdStatus(ctx: ExtensionContext): void {
	const c = getSubquestCompactThreshold();
	notifyInfo(ctx, `Sub-quest launch compaction threshold = ${formatTokens(c)} tokens (${c.toLocaleString()}). Usage: /quest-subquest-threshold <tokens|off|default> (e.g. /quest-subquest-threshold 40k)`);
}
export async function handleQuestSubquestThresholdCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	const trimmed = args.trim();
	if (!trimmed) { handleSubquestThresholdStatus(ctx); return; }
	if (isDefaultArg(trimmed)) { state.subquestCompactTokens = null; persist(pi, ctx); notifyInfo(ctx, `Sub-quest launch compaction threshold reset to default (${formatTokens(getSubquestCompactThreshold())}).`); return; }
	if (isOffArg(trimmed)) { state.subquestCompactTokens = 0; persist(pi, ctx); notifyInfo(ctx, `Sub-quest launch compaction disabled.`); return; }
	const parsed = parseTokenAmount(trimmed, DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);
	if (parsed === null) { notifyWarning(ctx, `Invalid token amount: "${trimmed}". Examples: 40k, 50000, off, default`); return; }
	state.subquestCompactTokens = parsed; persist(pi, ctx);
	notifyInfo(ctx, `Sub-quest launch compaction threshold set to ${formatTokens(parsed)} tokens (${parsed.toLocaleString()}).`);
}
