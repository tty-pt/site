// HIGH_LEVEL: #storage — short alphanumeric quest id, assigned at detection.
// SPEC: B1.0 (base62 unix seconds, minimum width, monotonic bump).
const QID_ALPHABET =
  "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
const QID_MIN_WIDTH = 6;

export type Qid = string & { readonly __brand: "qid" };

export function isQid(value: string): value is Qid {
  return /^[0-9A-Za-z]{6,}$/.test(value);
}

export function encodeQid(seconds: number): Qid {
  if (!Number.isInteger(seconds) || seconds < 0) {
    throw new Error(`invalid qid timestamp: ${seconds}`);
  }
  let rest = seconds;
  let out = "";
  do {
    out = QID_ALPHABET[rest % 62] + out;
    rest = Math.floor(rest / 62);
  } while (rest > 0);
  while (out.length < QID_MIN_WIDTH) out = `0${out}`;
  return out as Qid;
}

export function nextQid(nowSeconds: number, existing: readonly string[]): Qid {
  let seconds = Math.floor(nowSeconds);
  let candidate = encodeQid(seconds);
  while (existing.includes(candidate)) {
    seconds += 1;
    candidate = encodeQid(seconds);
  }
  return candidate;
}
