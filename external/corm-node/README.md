# @ttypt/corm

Universal Node.js native bindings and query engine for **`libcorm`** and the **Corm Recall Kernel (`cormr`)** multi-axis query engine (`joint`, `sepal`, `lemma`).

---

## Features

- **Persistent C99 Storage**: Direct, high-performance binary record storage via `corm_open`, `corm_put`, `corm_get`, `corm_del`, with automatic key indexing and file backing.
- **Set Algebra**: Fast candidate set intersections, unions, and subtractions (`cormr_set_t`).
- **Bounded Min-Heap Ranker**: Streaming $O(\log k)$ ranking engine (`cormr_rank_t`).
- **Composable Multi-Axis Queries**: Combine orthogonal domain axes (such as `joint` for temporal intervals, `sepal` for dense vector search, and `lemma` for lexical search) using AND/OR/NOT joins.
- **Resource Cleanup**: Node-API finalizers automatically clean up native memory handles when JavaScript objects are garbage collected or explicitly closed.

---

## Installation & Build

```bash
cd corm-node
npm run build:native  # Compiles native C N-API addon
npm run build:ts      # Compiles TypeScript typings & ESM wrapper
npm test              # Runs verification suite
```

---

## Usage Example

```typescript
import { CormDb, JointTemporalAxis, SepalVectorAxis } from "@ttypt/corm";

// 1. Initialize persistent storage
const db = new CormDb("./var/duna.corm", "reservations");

// 2. Attach domain axes
const temporal = new JointTemporalAxis();
temporal.registerInterval(101, 1 /* resourceId */, 1793400000, 1793428800);

const vector = new SepalVectorAxis();
vector.registerVector(101, [0.12, 0.85, 0.44]);

db.registerAxis(temporal);
db.registerAxis(vector);

// 3. Multi-Axis Composition & Ranking Query
const results = await db
  .query()
  .filter("temporal", { start: 1793405000, end: 1793415000 }, "AND")
  .rank("vector", { queryVector: [0.1, 0.8, 0.4] })
  .topK(5)
  .execute();

console.log("Top matched candidates:", results);

db.close();
```
