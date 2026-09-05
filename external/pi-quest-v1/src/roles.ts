import { ActiveReview, ExtensionContext } from "./types.ts";
import { getActiveContext, getSessionId } from "./state.ts";

export type AgentRoleId = "main" | "reviewer" | "subagent";

export interface AgentSelf {
  sessionId: string;
  isChild: boolean;
  parentSessionId?: string;
}

export function resolveCallerSelf(ctx?: ExtensionContext): AgentSelf {
  const c = getActiveContext(ctx);
  const sessionId = getSessionId(c);
  const childId = (c as { childSessionId?: unknown })?.childSessionId;
  const parentId = (c as { parentSessionId?: unknown })?.parentSessionId;
  const isChild = typeof childId === "string" && childId.length > 0;
  return {
    sessionId,
    isChild,
    ...(isChild ? { parentSessionId: parentId as string | undefined } : {}),
  };
}

export type RoleResolution =
  | {
    role: "implementer-drafter";
    roleSessionId: string;
    hasActiveReview: boolean;
  }
  | {
    role: "reviewer";
    reviewId: string;
    parentSessionId: string;
    childSessionId: string;
  };

export function resolveActiveRole(
  questSlug: string,
  self: AgentSelf,
  activeReviews: Iterable<ActiveReview>,
): RoleResolution {
  for (const review of activeReviews) {
    const isForQuest =
      review.questSlug === questSlug ||
      review.questId === questSlug ||
      (review.snapshot && review.snapshot.questId === questSlug);
    if (!isForQuest) continue;
    const stillActive =
      review.status === "starting" || review.status === "running";
    if (!stillActive) continue;

    if (review.childSessionId && review.childSessionId === self.sessionId) {
      return {
        role: "reviewer",
        reviewId: review.reviewId,
        parentSessionId: review.parentSessionId,
        childSessionId: review.childSessionId,
      };
    }
    if (review.parentSessionId === self.sessionId) {
      return {
        role: "implementer-drafter",
        roleSessionId: self.sessionId,
        hasActiveReview: true,
      };
    }
  }
  return {
    role: "implementer-drafter",
    roleSessionId: self.sessionId,
    hasActiveReview: false,
  };
}