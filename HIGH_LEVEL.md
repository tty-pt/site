# High-level product specification
> THIS IS THE AUTHORITATIVE HIGH-LEVEL DOCUMENT OF HOW PI-QUEST SHOULD WORK.

## intro
pi-quest v2 is a pi coding agent extension that does the following:

- Adds drafting / implementing / validating modes to pi coding agent, and relates a task with something called a 'quest'.
- A quest might be comprised of sub-quests, or phases, which is a lower level task.
- For each quest or subquest the process is similar, and will be described below:

It's goal is to allow low-cost models to accomplish complex long-running tasks with as little user intervention as possible.

# modes

## drafting
The drafting phase is one where the main agent can only write one file, the draft file. Other than that it can research, read etc. Every time it changes the draft, an adversarial reviewer is fired up, demoting any previous and active adversarial reviewer. The reviewer will either FAIL on some grounds, or PASS the plan as-is. If it fails, it will substantiate its failure and make suggestions to the main agent to fix the plan, be it with more investigations, more proof, or whatever. If an adversarial reviewer PASSES the plan, the main agent will be promoted to the 'implementing' phase, provided its research is recorded and the plan is actionable.

At any time when drafting that the user themselves say that agent can proceed to implementation, any reviewer agent currently active can be dismissed, and the main agent will be promoted to the 'implementing' phase. PASS promotes automatically; the user may also approve at any moment, which promotes immediately — approval is never required.

## implementing
During implementation, no action is restricted to the agent. Setbacks (test failures, contradictions, new requirements) are recorded with their evidence and the agent carries on; nothing blocks. When it learns something that refines or contradicts the approved plan, it records the change with its reasons without rewriting the original plan. Amendments adjust the plan toward reality as new facts emerge; they never change the quest's scope — a change of scope is a new quest. Once it claims that implementation is complete, it will be promoted to the validation phase.

## validation
In this phase, a reviewer/validator agent will be fired up to check if the implementation really does comply to the plan, and whether the decisions the main agent took along the way were appropriate. If it does, the quest will be complete and archived (a slim archive: quest view, session reference, manifest). If it does not, then it must demote the main agent back to the 'implementing' phase with its findings.

# working together

## sub-quests
A quest decomposes into phases and sub-quests. Phases are sequential stages inside one quest's plan; sub-quests are full-lifecycle units with their own quest id, used only for complex sub-tasks. Each sub-quest implements and validates; it is reviewed only if it deviates from its brief. One quest is active at a time: a parent waits while a child runs and resumes on its return with the child's findings, which it must record and either act on or explicitly continue past. A parent cannot complete while any child is unfinished. Depth is capped at 3 by default. A failed child does not fail its parent; the parent records, adjusts, and the validator judges.

## human absence
The agent never waits indefinitely for the user. When it needs a human decision, it records the question with its recommended default, notifies, waits one minute, then proceeds with the default. The wait is configurable per question: no wait, any duration, or indefinite. A user who answers late still counts: the answer is applied as a refinement or confirmation whenever it arrives. Cancelled or unavailable prompts count as absence.

# surviving

## durability
Quest state is stamped into the session transcript on every change and re-read before every reply, so a quest survives compaction and restarts.

## dependencies
pi-quest v2 requires pi-vcc handling all compaction (its override enabled), so compacting is deterministic, instant, and free — and never touches the transcript the snapshots live in. History lookup within a session reuses pi-vcc's recall; recovery across sessions is pi-quest's own scan.

## storage
pi-quest keeps its runtime artifacts in two places. The quest's state lives in pi's own session transcript as snapshot stamps, which is also how a quest is recovered in a later session. On disk, under .pi/quest: the drafting workspace (future/), the generated quest view (current/), and slim archives of finished quests (archive/). Every quest — draft or active — is identified only by a short alphanumeric quest id, assigned at detection and used in all paths (future/<qid>.md, current/<qid>/). The generated files are views only — the transcript is the truth.

# surface

## tools (main agent)
### quest_update_state
Record findings, plans, amendments, and state. The agent's write path to the quest.

### quest_subquest
Spawn a linked sub-quest for a complex sub-task.

### quest_archive
Finish a quest as complete, failed, or abandoned.

### quest_recover
Rebuild quest state from the transcript, including earlier sessions. Runs automatically when state is absent; callable directly.

### quest_rebut
Answer a review with evidence; a successful rebuttal reopens the question.

### quest_ask_human
Ask the user with a recommended default and timeout (one minute default, configurable); never blocks.

## tools (other agents)
Reviewers and validators are strictly read-only: they may read files, search, and inspect diffs, but never edit, write, run mutating commands, or call quest tools. They report back a single verdict (PASS/FAIL) with findings. The main agent may rebut a verdict with evidence or escalate to the human.

## commands
Three manual handles; everything else the system does by itself.

### /quest
Resume a quest or drafting phase, or show the active quest.

### /quests
List all quests with their states and the active marker.

### /quest-del
Archive (kill) the current or named quest.

## skill
The quest-journal skill carries the workflow rules for the main agent — this document in actionable form. Reviewers do not receive it; they receive only their review brief.

# configurations
Four settings, in .pi/settings.json under "pi-quest", all optional with the stated defaults.

### ask timeout
How long quest_ask_human waits for the user: one minute by default; per question configurable to no wait, any duration, or indefinite.

### depth cap
How deep sub-quests may nest: 3 by default.

### draft thresholds
When a draft auto-reviews: 2 requirements, or 1 requirement plus 7 evidence items, with an actionable plan present.

### interface bindings
Which peer tools plug into the interfaces below (defaults shown). A declared binding wins when the named tool exists; otherwise built-in defaults apply; absent entirely, the specified degradation applies.
{"asking": {"tool": "ask_questions"}, "reviewRunner": {"tool": "subagent"}}

# interfaces
pi-quest talks to peer extensions through pi's tool registry and requires none of them.

### asking
Question-style extensions are detected by tool name when present and asked with a timeout signal. An ask that is absent, cancelled, or timed out is not an error and never blocks: the agent proceeds with its recorded default, and a late answer still applies whenever it arrives.

### sub-agents
Sub-agent extensions run reviewers and validators isolated and read-only, anchored at the project root, abortable mid-run. They are not mandatory: without one, reviews degrade to the user path — the plan is presented, a "go" promotes, completion is accepted on user confirmation — and the quest never stalls for a missing extension.

# review and validation communication

Communication between the main agent and reviewers/validators is asynchronous, explicit, and revision-bound.

A reviewer or validator never communicates with the main agent through ordinary conversational context. It receives a structured brief and returns a structured result through the sub-agent interface.

## review request

Every review request identifies:

* `qid`;
* review type (`draft` or `validation`);
* the exact revision or implementation snapshot being reviewed;
* the quest's current approved plan, where applicable;
* the evidence and findings relevant to the review;
* the criteria the reviewer must judge.

A reviewer must evaluate the material identified by the request and must not assume that later quest state exists.

For drafting, the reviewed object is a specific **draft revision**.

For validation, the reviewed object is a specific **implementation snapshot** together with the approved plan and all applicable amendments.

## review result

The reviewer returns exactly one result:

```text
verdict: PASS | FAIL
target: <revision or implementation snapshot>
findings: <findings supporting the verdict>
```

`PASS` contains no blocking findings.

`FAIL` contains every material blocking finding discovered by the reviewer, together with enough evidence or explanation for the main agent to act on it.

The reviewer does not directly modify quest state.

The main agent or quest runtime records the returned result in the quest history.

## stale results

A review result is valid only for the target revision or implementation snapshot named in its request.

If the quest has advanced beyond that target before the result arrives:

* the result is recorded historically;
* it cannot promote, demote, complete, or otherwise change the newer state.

For draft reviews, changing the draft invalidates all pending reviews of earlier draft revisions.

For validation, changing the implementation after validation began invalidates the validation result for purposes of acceptance and requires validation of the new implementation state.

## rebuttal

The main agent may submit a rebuttal to a review finding.

A rebuttal identifies:

* the review result being challenged;
* the specific finding;
* the evidence or reasoning supporting the rebuttal.

A rebuttal does not alter the original review result.

The quest runtime records the rebuttal as a new event.

The rebuttal may cause the relevant review question to be reopened and sent for re-review.

## reviewer independence

The reviewer receives only the information necessary to perform its role.

It does not receive the quest-journal skill or instructions intended for the main agent.

The reviewer must not attempt to infer authority over quest state.

Its role is to judge and report, not to negotiate the quest's state directly.

## validator communication

Validation follows the same protocol.

The validator receives:

* the approved plan;
* the implementation snapshot under review;
* recorded amendments;
* relevant evidence;
* validation criteria.

It returns exactly one `PASS` or `FAIL` result.

A validation `FAIL` returns control to the main agent.

The main agent may then change the implementation, record amendments, or rebut findings.

A subsequent validation run is a new validation request against a new implementation snapshot.

## no direct mutation

Reviewers and validators never:

* write quest state;
* alter the plan;
* create amendments;
* change implementation files;
* archive quests;
* promote or demote the main agent;
* create or mutate any files.

Only the quest runtime and main agent can cause those state transitions.

The reviewer or validator supplies evidence for those transitions; it does not perform them.

# quest creation

A quest is created automatically when:

* the user makes a request; and
* no quest is currently active.

The newly created quest receives its `qid` at creation time.

The user's request becomes the initial quest objective and scope.

Quest creation does not require explicit user confirmation.

When a quest is already active, a new user request does not automatically create a second active quest. It is interpreted in the context of the active quest unless the quest system determines that the request constitutes a separate quest according to its scope rules.

Only one quest may be active at a time.

# independent review contexts

Every reviewer and validator runs in a **new, independent agent context**.

The reviewer or validator must not inherit the main agent's conversational context, chain of reasoning, or conclusions.

This isolation is intentional: the reviewer must independently evaluate the work rather than continue the reasoning process that produced it.

The reviewer receives only the material necessary to perform its assigned review, including the structured review brief and the relevant quest artifacts, evidence, and target revision or implementation snapshot.

The reviewer does not receive the main agent's reasoning narrative unless that narrative has been deliberately recorded as evidence in the quest material.

The same rule applies to validators.

A fresh context is created for each independent review or validation run.

A subsequent review is therefore a new judgment, not a continuation of the previous review context.

## review independence

The purpose of reviewer isolation is to reduce confirmation bias and preserve genuine adversarial evaluation.

Accordingly:

* a reviewer does not continue from a previous reviewer's context;
* a validator does not continue from the main agent's implementation context;
* a reviewer does not receive previous reviewer conclusions unless those conclusions are explicitly part of the material it has been instructed to evaluate;
* a validator may inspect prior review results where relevant, but those results are evidence, not instructions;
* a new review or validation run begins from a fresh context even when it concerns the same quest and target.

The quest runtime may preserve the historical results of earlier reviews, but preservation of history must not become inheritance of reasoning context.


