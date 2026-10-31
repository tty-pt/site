import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import {
  CormDb,
  CandidateSet,
  JointTemporalAxis,
  SepalVectorAxis,
  LemmaLexicalAxis,
} from "../dist/index.js";

const TEST_DB_PATH = "/tmp/test_corm_multi_axis.corm";

test("CormDb basic CRUD persistence and cleanup", () => {
  if (fs.existsSync(TEST_DB_PATH)) fs.unlinkSync(TEST_DB_PATH);

  const db = new CormDb(TEST_DB_PATH, "duna_test");

  // Put JSON record
  const pos = db.put("res_101", {
    userId: "usr_ana",
    seat: "Desk 04",
    credits: 10,
  });
  assert.ok(pos > 0, "corm_put should return a positive position handle");

  // Get JSON record
  const record = db.get("res_101");
  assert.deepEqual(record, {
    userId: "usr_ana",
    seat: "Desk 04",
    credits: 10,
  });

  // Delete record
  const deleted = db.del("res_101");
  assert.equal(deleted, true);

  const afterDel = db.get("res_101");
  assert.equal(afterDel, null);

  db.close();
});

test("CandidateSet C algebra: intersect, union, and subtract", () => {
  const setA = new CandidateSet();
  setA.push(100).push(200).push(300).seal();

  const setB = new CandidateSet();
  setB.push(200).push(300).push(400).seal();

  // Intersect: A ∩ B = {200, 300}
  const inter = setA.intersect(setB);
  assert.deepEqual(inter.toArray(), [200, 300]);

  // Union: A ∪ B = {100, 200, 300, 400}
  const un = setA.union(setB);
  assert.deepEqual(un.toArray(), [100, 200, 300, 400]);

  // Subtract: A − B = {100}
  const diff = setA.subtract(setB);
  assert.deepEqual(diff.toArray(), [100]);
});

test("Native Joint temporal interval engine verification", () => {
  const temporal = new JointTemporalAxis();
  temporal.registerInterval(10, 1, 1000, 2000);
  temporal.registerInterval(20, 1, 3000, 4000);

  // Overlapping query [1500, 2500] -> should find ref 10
  const hit = temporal.fill({ start: 1500, end: 2500 });
  assert.deepEqual(hit.toArray(), [10]);

  // Overlapping query [3500, 4500] -> should find ref 20
  const hit2 = temporal.fill({ start: 3500, end: 4500 });
  assert.deepEqual(hit2.toArray(), [20]);
});

test("Lemma lexical text search axis verification", () => {
  const lexical = new LemmaLexicalAxis();
  lexical.registerText(101, "Quiet corner desk with 4k external monitor");
  lexical.registerText(102, "Sunny window garden seat beside coffee bar");

  const monitorHit = lexical.fill({ term: "monitor" });
  assert.deepEqual(monitorHit.toArray(), [101]);

  const gardenHit = lexical.fill({ term: "garden" });
  assert.deepEqual(gardenHit.toArray(), [102]);

  const score101 = lexical.rank({ term: "4k monitor" }, 101);
  assert.equal(score101, 1.0);
});

test("Multi-Axis Query: Joint temporal + Lemma lexical + Sepal vector ranking (libuv async)", async () => {
  const db = new CormDb("/tmp/test_corm_axis_query.corm", "query_test");

  // 1. Native Joint Temporal Axis
  const temporal = new JointTemporalAxis();
  temporal.registerInterval(1, 1, 1000, 2000); // Overlaps [1200, 1800]
  temporal.registerInterval(2, 1, 3000, 4000); // Disjoint
  temporal.registerInterval(3, 2, 1500, 2500); // Overlaps [1200, 1800]

  // 2. Lemma Lexical Axis
  const lexical = new LemmaLexicalAxis();
  lexical.registerText(1, "Premium standing desk with 4k monitor");
  lexical.registerText(2, "Lounge sofa next to coffee bar");
  lexical.registerText(3, "Dedicated quiet garden desk with monitor");

  // 3. Sepal Vector Axis
  const vector = new SepalVectorAxis();
  vector.registerVector(1, [1.0, 0.0, 0.0]);
  vector.registerVector(2, [0.0, 1.0, 0.0]);
  vector.registerVector(3, [0.8, 0.2, 0.0]);

  db.registerAxis(temporal);
  db.registerAxis(lexical);
  db.registerAxis(vector);

  // Store metadata in corm
  db.put("1", { id: "res_1", title: "Morning Standing Desk" });
  db.put("2", { id: "res_2", title: "Evening Sofa" });
  db.put("3", { id: "res_3", title: "Afternoon Garden Desk" });

  // Composed multi-axis query:
  // - Filter temporal: overlaps [1200, 1800] (candidates 1 and 3)
  // - Filter lexical: matches 'monitor' (candidates 1 and 3)
  // - Rank vector: similarity to [1.0, 0.0, 0.0]
  const results = await db
    .query()
    .filter("temporal", { start: 1200, end: 1800 }, "AND")
    .filter("lexical", { term: "monitor" }, "AND")
    .rank("vector", { queryVector: [1.0, 0.0, 0.0] }, 1.0)
    .topK(10)
    .execute();

  assert.equal(results.length, 2, "Expected 2 matching candidates (ref 1 and ref 3)");
  assert.equal(results[0].ref, 1, "Ref 1 should rank first with perfect vector similarity");
  assert.ok(results[0].score > 0.99);
  assert.equal(results[1].ref, 3, "Ref 3 should rank second");
  assert.deepEqual(results[0].data, { id: "res_1", title: "Morning Standing Desk" });

  db.close();
});
