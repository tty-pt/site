I think so. And I think the mistake would be to build yet another memory system that happens to use your libraries.

What if the fundamental unit of memory weren't a document, a vector, or even a fact, but a piece of experience whose meaning changes with context, time, and accumulated knowledge?

Your existing ideas suggest a different direction.

## 1. Don't build a memory database. Build a memory space.

You have the beginnings of a multidimensional retrieval kernel in `libcorm`, with distinct axes for different kinds of search:

- Joint — points and intervals, including temporal search.
- Islet — spatial search.
- Lemma — lexical and full-text search, associated with `libstoma`.
- Sepal — semantic approximate-nearest-neighbour search, through `libsepal`.

That suggests a more interesting architecture than a conventional RAG pipeline. These aren't merely four alternative indexes. They are four ways of locating relevant knowledge.

Imagine Pi asks:

> Why did we abandon that approach to memory consolidation?

The system could search simultaneously for:

- semantically similar investigations;
- exact references to the abandoned approach;
- episodes that occurred before the decision;
- related knowledge in the same project or subsystem.

Then it combines those results into one evidence-backed answer.

The key is that relevance becomes a multidimensional operation, rather than a single similarity score.

## The memory space

One collection of experiences, multiple independent retrieval axes

Time

What happened before, after, or during an investigation?

Context

Which project, module, subsystem or environment?

Lexical

Which exact symbols, errors, concepts or phrases?

Semantic

Which experiences are conceptually similar?

Recall kernel

Intersect, rank and traverse evidence across axes

The spatial axis is especially interesting if you eventually want to model relationships between code locations, architectural regions and changes. But I wouldn't force every memory into every axis: each index should be populated only when its data is meaningful.

## 2. Memory mipmaps should be more than summaries

Here's where I'd go further than ordinary hierarchical memory.

A conventional system stores a transcript, summarizes it, then summarizes the summary. Each level is effectively a lossy textual compression.

Instead, imagine a hierarchy in which every abstraction remains connected to its underlying experiences.

### L3 · General knowledge

Lessons that recur across projects

### L2 · Project understanding

Architectural decisions, invariants and established conventions

### L1 · Investigations

Hypotheses, attempted fixes, outcomes and causal explanations

### L0 · Experience

Original observations, tool results, diffs and test evidence

Move downward to inspect evidence; move upward to discover broader patterns.

The novel part is not the hierarchy itself. Hierarchical summaries already exist.

It's that each level is a queryable view over a connected body of evidence, with its own indexes and relationships.

A high-level memory could be regenerated or revised when the evidence beneath it changes. Pi could inspect why a conclusion exists instead of accepting a summary as an unquestionable fact.

And retrieval could begin at any resolution. A broad question might need only L2; a subtle debugging problem could descend all the way to L0.

## 3. Let memory have a lifecycle

I would also avoid treating everything the LLM writes as permanent knowledge.

A memory should move through states such as:

- Observed: something appeared in a tool result or conversation.
- Hypothesized: the agent proposed an explanation.
- Tested: an experiment produced evidence.
- Supported: evidence currently favours the claim.
- Contradicted: new evidence conflicts with it.
- Superseded: a newer conclusion replaced it, while the historical record remains available.

This lets the system remember mistakes without believing them forever.

More importantly, it allows the memory engine to distinguish three different questions:

1. What did we believe at the time?
2. What does the available evidence support now?
3. What should the agent believe for this particular task?

Those are not necessarily the same answer.

The LLM should propose interpretations and abstractions. Your deterministic core should preserve identity, provenance, temporal history and relationships. That division is a strong foundation for experimentation.

## 4. Make retrieval learn from its own success

There's another opportunity I find promising: memory retrieval should improve based on what actually helps.

Suppose Pi retrieves an old investigation. It might use the finding successfully, discover that it is obsolete, or ignore it because it is irrelevant.

Record those outcomes.

Over time, the system could learn which retrieval paths are useful for particular query types. For example, a debugging task might benefit from exact error matching and temporal context, while an architectural question might benefit from semantic similarity and cross-project abstractions.

Initially, I would keep this simple: measure retrieval usefulness and adjust ranking weights. Don't introduce a second learning model before you have a reliable evaluation set.

The goal is not to make the memory system appear intelligent. It's to make recall measurably more useful.

## 5. What I would build first

I'd resist implementing every idea at once. The first experiment should establish whether this architecture offers an advantage over a normal memory plugin.

1. Capture complete episodes. Preserve the relevant user request, investigation steps, tool observations, code changes and outcomes.
2. Create evidence-linked abstractions. Extract candidate lessons, but keep each linked to the episodes that support it.
3. Use your multidimensional recall kernel. Combine temporal, contextual, lexical and semantic retrieval instead of relying on embeddings alone.
4. Add two-way mipmap traversal. Let Pi move from summaries to evidence and discover broader lessons from related episodes.
5. Benchmark against simpler alternatives. Compare lexical search, semantic search, flat episode summaries and your combined system on the same historical coding tasks.

I would use `axil-qllm` where its actual capabilities fit the extraction and consolidation work, rather than make the memory engine depend on a particular inference implementation.

One caveat: this is an architectural proposal based on your previously described library roles, not a verified implementation plan. I still haven't inspected the current source of `libcorm`, `libsepal`, `libstoma` or `axil-qllm,` so the exact division of responsibilities needs to be checked against their real APIs.

My strongest suggestion: explore the idea of a multiresolution, evidence-preserving memory space.

The combination of temporal retrieval, lexical and semantic indexes, contextual relationships, and navigable levels of abstraction is more distinctive than simply adding a graph or vector database to Pi.

And it leads to a useful design test:

Can Pi answer not only what it remembers, but why it remembers it, when it was true, and whether it still applies?

If your system can do that efficiently, it would be solving a more interesting problem than ordinary coding-agent memory.
