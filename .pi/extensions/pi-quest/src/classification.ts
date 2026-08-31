import { writeFile, mkdir } from "node:fs/promises";
import { checkAndTriggerDirectionReview } from "./critical_agent.ts";
import { PROMPT_MAX_COUNT } from "./constants.ts";
import { canImplement, syncImplementationPermission } from "./gates.ts";
import { persist } from "./persistence.ts";
import { triggerReassessment } from "./research.ts";
import { getState, state } from "./state.ts";
import { checkAndEmitGateContinuationSteer } from "./tool_gating.ts";
import { ExtensionAPI, ExtensionContext, UserMessageClassification } from "./types.ts";
import { updateUIStatus } from "./ui.ts";

export function hasRequirementKeyword(text: string): boolean {
	const lower = text.toLowerCase();
	return /\b(also add|change requirement|instead of|new requirement|must also|must not|actually need|do not|please change|refactor|fix|bug|broken|error|fail)\b/i.test(lower);
}

export function classifyUserMessage(text: string): UserMessageClassification {
	const trimmed = text.trim();
	if (!trimmed) return UserMessageClassification.CONVERSATIONAL_ACK;

	const lower = trimmed.toLowerCase();
	const clean = lower.replace(/[.,!?;:]+/g, " ").trim();
	const words = clean.split(/\s+/).filter(Boolean);

	const hasReq = hasRequirementKeyword(lower);

	// 1. Explicit user confirmations (standalone or combination of confirmation phrases)
	const confirmPhrases = [
		"yes", "yep", "yeah", "sure", "go ahead", "proceed", "approved", "approve",
		"do it", "confirm", "confirmed", "lgtm", "looks good", "sounds good",
		"start", "implement", "implement it", "let's do it", "lets do it", "let's go", "lets go",
		"continue", "fine by me", "go for it", "go", "please proceed", "please implement",
		"go ahead and implement", "looks good to me", "sounds good to me", "all good"
	];

	const matchesConfirmationPattern = confirmPhrases.some((phrase) => {
		if (clean === phrase) return true;
		if (clean.startsWith(phrase + " ") || clean.endsWith(" " + phrase) || clean.includes(" " + phrase + " ")) {
			if (!hasReq && clean.length < 120) return true;
		}
		return false;
	});

	if (matchesConfirmationPattern) {
		return UserMessageClassification.CONFIRMATION;
	}

	// 2. Pure conversational acknowledgments / greetings / closures
	const ackWords = new Set(["hi", "hello", "hey", "greetings", "thanks", "thank", "you", "thx", "ok", "okay", "k", "got", "it", "cool", "nice", "great", "good", "fine", "done", "quit", "exit", "no", "nope", "bye"]);
	const allAckWords = words.length > 0 && words.every((w) => ackWords.has(w));
	if (allAckWords && !hasReq) {
		return UserMessageClassification.CONVERSATIONAL_ACK;
	}

	// 3. Informational questions / inquiries about status, files, syntax
	const isQuestion = /^(what|where|who|how|why|is there|are there|can you explain|explain|tell me|show me|which|status|how does|what is|what are)\b/i.test(lower) || lower.endsWith("?");
	if (isQuestion && trimmed.length < 250) {
		if (!hasReq) {
			return UserMessageClassification.QUESTION_OR_DISCUSSION;
		}
	}

	// 4. Material requirements / refinements
	return UserMessageClassification.REFINEMENT_OR_REQUIREMENT;
}

export async function acceptRootConfirmation(pi: ExtensionAPI, ctx?: ExtensionContext): Promise<void> {
	const s = getState(ctx);
	if (!s.awaitingUserConfirmation) return;
	const wasImplementable = canImplement(s, ctx);
	s.awaitingUserConfirmation = false;
	if (s.active) {
		if (!Array.isArray(s.confirmedQuests)) s.confirmedQuests = [];
		if (!s.confirmedQuests.includes(s.active)) {
			s.confirmedQuests.push(s.active);
		}
	}
	syncImplementationPermission(s);
	persist(pi, ctx);
	updateUIStatus(ctx);

	const isImplementable = canImplement(s, ctx);
	if (!wasImplementable && isImplementable) {
		checkAndEmitGateContinuationSteer(pi, ctx, s.active || undefined, { wasAwaitingConfirmation: true });
	}

	if (s.active && ctx) {
		await checkAndTriggerDirectionReview(pi, ctx, "initial_plan_established");
	}
}

export function isConfirmationQuestion(
	questionText = "",
	headerText = "",
	inputQuestions?: any[],
	optionsList?: any[],
): boolean {
	const qLower = questionText.toLowerCase();
	const hLower = headerText.toLowerCase();
	const combined = `${hLower} ${qLower}`;

	const confirmKeywords = [
		"proceed", "confirm", "approve", "approval", "implement", "implementation",
		"start", "begin", "go ahead", "apply changes", "ready to proceed",
		"shall i", "should i", "permission", "authorization", "execute plan",
		"next action", "next step", "wrap up", "wrap-up"
	];

	if (confirmKeywords.some((kw) => combined.includes(kw))) {
		return true;
	}

	const checkOptions = (opts?: any[]) => {
		if (!Array.isArray(opts)) return false;
		for (const opt of opts) {
			const optText = (typeof opt === "string" ? opt : opt?.label || opt?.description || "").toLowerCase();
			if (
				optText.includes("proceed") ||
				optText.includes("implement") ||
				optText.includes("go ahead") ||
				optText.includes("approve") ||
				optText.includes("confirm") ||
				optText.includes("start")
			) {
				return true;
			}
		}
		return false;
	};

	if (checkOptions(optionsList)) return true;

	if (Array.isArray(inputQuestions)) {
		for (const q of inputQuestions) {
			if (checkOptions(q?.options)) return true;
		}
	}

	return false;
}

export function handleAskQuestionsResult(pi: ExtensionAPI, event: any, ctx: ExtensionContext) {
	if (event.isError || event.error) return;
	const details = event.details;
	if (details && (details.status === "cancelled" || details.status === "unavailable" || details.error)) {
		return;
	}

	const answers: Array<{ question: string; header: string; answer: string; options?: any[] }> = [];

	if (Array.isArray(details?.answers)) {
		for (const a of details.answers) {
			if (typeof a?.answer === "string") {
				const qIndex = typeof a.questionIndex === "number" ? a.questionIndex : -1;
				const matchedQ = qIndex >= 0 && Array.isArray(details.questions) ? details.questions[qIndex] : null;
				answers.push({
					question: typeof a.question === "string" ? a.question : (matchedQ?.question || ""),
					header: typeof a.header === "string" ? a.header : (matchedQ?.header || ""),
					answer: a.answer.trim(),
					options: matchedQ?.options || (Array.isArray(event.input?.questions) && qIndex >= 0 ? event.input.questions[qIndex]?.options : undefined),
				});
			}
		}
	} else if (Array.isArray(event.content)) {
		for (const item of event.content) {
			if (item?.type === "text" && typeof item.text === "string") {
				const lines = item.text.split(/\r?\n/);
				let currentQ = "";
				for (const line of lines) {
					const qMatch = line.match(/Question:\s*(.+)$/i);
					if (qMatch) currentQ = qMatch[1].trim();
					const aMatch = line.match(/Answer:\s*(.+)$/i);
					if (aMatch) {
						answers.push({
							question: currentQ,
							header: "",
							answer: aMatch[1].trim(),
						});
						currentQ = "";
					}
				}
			}
		}
	}

	if (answers.length === 0) return;

	for (const item of answers) {
		const answerText = item.answer;
		if (!answerText) continue;

		const qText = item.question || "";
		const hText = item.header || "";
		const isConfirmQ = isConfirmationQuestion(qText, hText, event.input?.questions, item.options);

		const classification = classifyUserMessage(answerText);

		if (isConfirmQ && classification === UserMessageClassification.CONFIRMATION) {
			acceptRootConfirmation(pi, ctx);
		} else if (
			classification === UserMessageClassification.REFINEMENT_OR_REQUIREMENT &&
			hasRequirementKeyword(answerText)
		) {
			if (state.active) {
				if (!Array.isArray(state.refinements)) state.refinements = [];
				state.refinements.push(answerText);
				if (!Array.isArray(state.prompts)) state.prompts = [];
				state.prompts.push(answerText);
				if (state.prompts.length > PROMPT_MAX_COUNT) {
					state.prompts = [state.prompts[0], ...state.prompts.slice(-(PROMPT_MAX_COUNT - 1))];
				}
				if (state.refinements.length > PROMPT_MAX_COUNT) {
					state.refinements = state.refinements.slice(-PROMPT_MAX_COUNT);
				}
				triggerReassessment(state, `User refinement received via ask_questions: "${answerText.slice(0, 100)}..."`, answerText);
				persist(pi, ctx);
				updateUIStatus(ctx);
			}
		}
	}
}
