import { logEvent } from "../logging.ts";
import { state } from "../state.ts";
import { QuestErrorCode } from "../types.ts";
import { reportAgentError } from "../messaging.ts";
import type { ExtensionAPI, ExtensionContext } from "../types.ts";

export async function executeAskHumanTool(
  params: any,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
) {
  const question = (params?.question || params?.prompt || params?.message || "")
    .trim();
  if (!question || question.length < 10) {
    reportAgentError(
      pi,
      ctx,
      "quest_ask_human requires a question (at least 10 chars) for the human to decide.",
      {
        code: QuestErrorCode.PLAN_REVIEW_REQUIRED,
        requiredNextAction:
          'Call quest_ask_human with { question: "..." } including full dialogue transcript and options: uphold reviewer / side with implementer / guide.',
      },
    );
    return {
      content: [{ type: "text", text: "Question too short." }],
      details: { error: "question_too_short" },
    };
  }
  const transcript = state.reviewDialogue && state.reviewDialogue.length > 0
    ? state.reviewDialogue.map((d: any) =>
      `Round ${d.round} [${d.verdictBefore}->${d.verdictAfter || "?"}]: ${
        d.implementerRebuttal.slice(0, 300)
      }`
    ).join("\n")
    : "No prior dialogue";
  const fullPrompt =
    `**Human escalation requested**\n\nQuestion: ${question}\n\nDialogue transcript:\n${transcript}\n\nOptions:\n- uphold reviewer (keep REVISE, revise plan)\n- side with implementer (approve, clear gate)\n- guide (provide direction)`;

  // Use Pi's ask_questions if available, otherwise fallback to confirmation gate
  try {
    if (typeof pi.executeTool === "function") {
      // Try to invoke ask_questions via tool execution if registered
      const tools = typeof pi.getAllTools === "function"
        ? pi.getAllTools()
        : [];
      const hasAsk = Array.isArray(tools) &&
        tools.some((t: any) => t?.name === "ask_questions");
      if (hasAsk) {
        await pi.executeTool(
          "ask_questions",
          {
            questions: [{
              header: "Quest escalation",
              question: fullPrompt,
              options: [
                { label: "Uphold reviewer", description: "Keep REVISE" },
                { label: "Side with implementer", description: "Approve" },
                { label: "Guide", description: "Provide direction" },
              ],
            }],
          },
          undefined,
          undefined,
          ctx,
        );
      }
    }
  } catch {}

  // Also set awaiting confirmation so gate reflects human decision pending
  state.awaitingUserConfirmation = true;
  try {
    const { persist } = await import("../persistence.ts");
    persist(pi, ctx);
  } catch {}

  logEvent(
    "HUMAN_ESCALATION_REQUESTED",
    `human escalation: ${question.slice(0, 200)}`,
    { quest: state.active || "", question: question.slice(0, 200) },
  );

  // Send as steer so human sees it
  try {
    const { sendInternalAgentMessage } = await import("../messaging.ts");
    sendInternalAgentMessage(
      pi,
      fullPrompt,
      "steer",
      "human_escalation",
      `human_${Date.now()}`,
    );
  } catch {}

  return {
    content: [{
      type: "text",
      text: `Escalated to human: "${
        question.slice(0, 200)
      }". Awaiting human decision (uphold reviewer / side with implementer / guide). Dialogue transcript preserved in ## Review Dialogue.`,
    }],
    details: { question, transcriptLength: transcript.length },
  };
}
