import { check } from "../check.ts";
import { encodeQid, isQid, nextQid } from "../../src/domain/qid.ts";

Deno.test("qid rejects slugs and short strings", () => {
  check(!isQid(""), "empty rejected");
  check(!isQid("abc"), "short rejected");
  check(!isQid("abc-123"), "dash rejected");
  check(!isQid("abc 123"), "space rejected");
  check(isQid("abc123"), "six alnum accepted");
  check(isQid("000000"), "zeros accepted");
});

Deno.test("qid encodes base62 big-endian with minimum width", () => {
  check(encodeQid(0) === "000000", "zero pads to width 6");
  check(encodeQid(61) === "00000z", "61 is last single digit");
  check(encodeQid(62) === "000010", "62 carries big-endian");
});

Deno.test("qid rejects bad timestamps", () => {
  let threw = false;
  try {
    encodeQid(-1);
  } catch {
    threw = true;
  }
  check(threw, "negative throws");
  threw = false;
  try {
    encodeQid(1.5);
  } catch {
    threw = true;
  }
  check(threw, "fractional throws");
});

Deno.test("qid bumps monotonically on collision", () => {
  const first = encodeQid(1757000000);
  const second = nextQid(1757000000, [first]);
  check(second !== first, "collision bumps");
  check(isQid(second), "bumped id valid");
  check(nextQid(1757000000, []) === first, "no collision keeps encoding");
  const third = nextQid(1757000000, [first, second]);
  check(third !== first && third !== second, "double collision bumps twice");
});
