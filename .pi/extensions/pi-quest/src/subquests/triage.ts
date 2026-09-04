// HIGH_LEVEL: #working together — a new request during an active quest is
// interpreted in context (refinement, amendment trigger, sub-quest seed) unless
// it constitutes a separate quest. Borderline cases stay in context.
export type MessageKind = "confirmation" | "ack" | "question" | "refinement";

const CONFIRM_PHRASES = [
  "yes", "yep", "yeah", "sure", "go ahead", "proceed", "approved", "approve",
  "do it", "confirm", "confirmed", "lgtm", "looks good", "sounds good",
  "start", "implement", "implement it", "continue", "fine by me", "go for it",
  "go", "please proceed", "please implement", "all good",
];

const ACK_WORDS = new Set([
  "hi", "hello", "hey", "thanks", "thank", "you", "thx", "ok", "okay", "k",
  "got", "it", "cool", "nice", "great", "good", "fine", "done", "bye",
]);

const QUESTION_OPENERS =
  /^(what|where|who|how|why|is there|are there|can you explain|explain|tell me|show me|which|status|how does|what is|what are)\b/i;

export function classifyUserMessage(text: string): MessageKind {
  const trimmed = text.trim();
  const clean = trimmed.toLowerCase().replace(/[!?.]+$/g, "");
  if (CONFIRM_PHRASES.some((phrase) => clean === phrase || (clean.length < 120 && clean.includes(` ${phrase} `)))) {
    return "confirmation";
  }
  const words = clean.split(/\s+/).filter((w) => w.length > 0);
  if (words.length > 0 && words.every((w) => ACK_WORDS.has(w))) return "ack";
  if (QUESTION_OPENERS.test(trimmed) || trimmed.endsWith("?")) {
    if (trimmed.length < 250) return "question";
  }
  return "refinement";
}
