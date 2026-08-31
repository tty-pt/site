export function buildTemplatePlanSections(): string[] {
	return [
		`## Plan`,
		`> Multi-stage execution plan (treat as provisional until falsification pass completes).`,
		`1. `,
		``,
		`## Plan Confidence`,
		`> low · medium · high (justify confidence based on verified assumptions and evidence).`,
		``,
		`## Plan Revisions`,
		`> Record of plan changes: previous plan -> invalidating evidence -> new finding -> revised plan.`,
		`- Initial plan formulated.`,
		``,
		`## Latest Reassessment`,
		`> Records findings from the most recent fresh investigation when reassessment is triggered.`,
		`- `,
		``,
		`## Rejected Approaches`,
		`> Disproved hypotheses, failed attempts, and why they were abandoned.`,
		`- `,
		``,
	];
}
