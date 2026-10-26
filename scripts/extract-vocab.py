#!/usr/bin/env python3
"""
scripts/extract-vocab.py — Standalone vocabulary extraction tooling for liblemma & pi-mm.

Extracts clean, deduplicated wordlists from:
  1. GGUF model files (reading `tokenizer.ggml.tokens` metadata directly)
  2. HuggingFace `tokenizer.json` files (reading vocab dictionary / merges)
  3. SentencePiece `.model` / text vocabularies
  4. Plain text wordlist / dictionary files (one word per line, or text corpora)

Outputs clean generic wordlists as:
  - Plaintext wordlist file (one word per line)
  - C header (vocab_base.h) for liblemma (tok.c)
  - TypeScript module (vocab_base.ts) for pi-mm (tok.ts)

Zero runtime neural dependencies — produces static, deterministic word lists.
"""

import argparse
import json
import os
import re
import struct
import sys
from typing import List, Optional, Set

DEFAULT_MAX_WORDS = 15872  # 512 + 15872 = 16384 (LEMMA_TOK_BASE_VOCAB)

CORE_DOMAIN_WORDS = [
    "the", "The", "of", "Of", "and", "And", "to", "To", "in", "In", "is", "Is", "you", "You", "that", "That", "it", "It", "he", "He",
    "was", "Was", "for", "For", "on", "On", "are", "Are", "as", "As", "with", "With", "his", "His", "they", "They", "at", "At", "be", "Be",
    "this", "This", "have", "Have", "from", "From", "or", "Or", "one", "One", "had", "Had", "by", "By", "word", "Word", "but", "But", "not", "Not",
    "what", "What", "all", "All", "were", "Were", "we", "We", "when", "When", "your", "Your", "can", "Can", "said", "there", "use",
    "an", "each", "which", "she", "do", "how", "their", "if", "will", "up",
    "other", "about", "out", "many", "then", "them", "these", "so", "some", "her",
    "would", "make", "like", "him", "into", "time", "has", "look", "two", "more",
    "write", "go", "see", "number", "no", "way", "could", "people", "my", "than",
    "first", "water", "been", "call", "who", "oil", "its", "now", "find", "long",
    "down", "day", "did", "get", "come", "made", "may", "part", "song", "poem",
    "gig", "music", "musica", "música", "coracao", "coração", "pao", "pão", "cada",
    "dia", "guitar", "band", "title", "author", "date", "text", "body", "type",
    "calculate", "port", "portable", "viewport", "export", "import", "module",
    "int", "char", "void", "return", "struct", "static", "const", "unsigned",
    "size_t", "uint32_t", "uint16_t", "uint8_t", "while", "break", "switch",
    "case", "default", "sizeof", "NULL", "typedef", "include", "define", "endif",
    "ifndef", "ifdef", "extern", "axil", "corm", "hyle", "lemma", "liblemma", "bud", "xylem", "joint", "sepal",
    "hello", "world", "item", "rows", "field", "query", "filter", "source", "status",
    "Blue", "blue", "Dawn", "dawn", "summer", "black", "manor", "blackstar", "season", "star",
    "episode", "Episode", "verification", "Verification", "token", "Token", "tokens", "Tokens",
    "storage", "Storage", "memory", "Memory", "record", "Record", "records", "Records",
    "recall", "Recall", "decision", "Decision", "problem", "Problem", "approach", "Approach",
    "approaches", "Approaches", "solution", "Solution", "result", "Result", "results", "Results",
    "finding", "Finding", "findings", "Findings", "evidence", "Evidence", "test", "Test", "tests", "Tests",
    "HTOK", "JSON", "pure", "binary", "lexical", "decoupled", "Decoupled", "generic", "Generic",
    "vocabulary", "Vocabulary", "serialization", "Serialization", "tested", "observed", "supported",
    "hypothesized", "contradicted", "superseded", "invariant", "investigation", "pattern", "note"
]

def clean_word(token: str) -> Optional[str]:
    """Cleans a raw model token into a valid word for lemma indexing."""
    if not token:
        return None
    # Filter out byte artifacts from GPT-2 / Qwen byte-level BPE (like Ċ, ĉ, etc.)
    if any(ord(c) in (0x0A, 0x0D, 0x09, 0x10A, 0x108, 0x109, 0x10C, 0x10D) for c in token):
        return None
    # Prefer word beginnings (Ġ /  )
    cand = token
    if cand.startswith('Ġ') or cand.startswith(' ') or cand.startswith('▁'):
        cand = cand[1:]
    cand = cand.strip(' \t\r\n#_')
    # Filter out special control tokens like <|...|>, [CLS], <s>, etc.
    if cand.startswith('<|') or cand.startswith('[') or cand.startswith('<'):
        return None
    # Must be valid word chars: alphanumeric, underscore, or UTF-8 multibyte (accents)
    if len(cand) >= 2 and len(cand) <= 48 and re.match(r'^[a-zA-Z0-9_\u0080-\uFFFF]+$', cand):
        return cand
    return None

def extract_from_gguf(path: str) -> List[str]:
    """Extracts raw token strings from a GGUF binary model file."""
    with open(path, 'rb') as f:
        magic = f.read(4)
        if magic != b'GGUF':
            raise ValueError(f"Not a valid GGUF file: {path} (magic: {magic!r})")
        version, n_tensors, n_kv = struct.unpack('<IQQ', f.read(20))
        for _ in range(n_kv):
            klen = struct.unpack('<Q', f.read(8))[0]
            key = f.read(klen).decode('utf-8', errors='replace')
            val_type = struct.unpack('<I', f.read(4))[0]
            if val_type == 9:  # array
                arr_type, arr_len = struct.unpack('<IQ', f.read(12))
                if key == 'tokenizer.ggml.tokens' and arr_type == 8:  # array of strings
                    tokens = []
                    for _ in range(arr_len):
                        slen = struct.unpack('<Q', f.read(8))[0]
                        tokens.append(f.read(slen).decode('utf-8', errors='replace'))
                    return tokens
                else:
                    # Skip array content
                    if arr_type in (0, 1, 7):  # u8, i8, bool
                        f.seek(arr_len, 1)
                    elif arr_type in (2, 3):  # u16, i16
                        f.seek(arr_len * 2, 1)
                    elif arr_type in (4, 5, 6):  # u32, i32, f32
                        f.seek(arr_len * 4, 1)
                    elif arr_type in (10, 11, 12):  # u64, i64, f64
                        f.seek(arr_len * 8, 1)
                    elif arr_type == 8:  # string
                        for _ in range(arr_len):
                            slen = struct.unpack('<Q', f.read(8))[0]
                            f.seek(slen, 1)
            elif val_type in (0, 1, 7):
                f.seek(1, 1)
            elif val_type in (2, 3):
                f.seek(2, 1)
            elif val_type in (4, 5, 6):
                f.seek(4, 1)
            elif val_type in (10, 11, 12):
                f.seek(8, 1)
            elif val_type == 8:
                slen = struct.unpack('<Q', f.read(8))[0]
                f.seek(slen, 1)
    return []

def extract_from_json(path: str) -> List[str]:
    """Extracts tokens from HuggingFace tokenizer.json or generic vocab.json."""
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        data = json.load(f)
    if isinstance(data, dict):
        if "model" in data and isinstance(data["model"], dict) and "vocab" in data["model"]:
            vocab = data["model"]["vocab"]
            if isinstance(vocab, dict):
                # Sort by token index if available
                sorted_tokens = sorted(vocab.items(), key=lambda x: x[1] if isinstance(x[1], int) else 0)
                return [k for k, _ in sorted_tokens]
            elif isinstance(vocab, list):
                return [str(x[0]) if isinstance(x, (list, tuple)) else str(x) for x in vocab]
        if "vocab" in data and isinstance(data["vocab"], dict):
            sorted_tokens = sorted(data["vocab"].items(), key=lambda x: x[1] if isinstance(x[1], int) else 0)
            return [k for k, _ in sorted_tokens]
        # Plain dict of token -> id
        return list(data.keys())
    elif isinstance(data, list):
        return [str(x) for x in data]
    return []

def extract_from_txt(path: str) -> List[str]:
    """Extracts words from a plaintext wordlist or corpus."""
    tokens = []
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            for part in line.strip().split():
                if part:
                    tokens.append(part)
    return tokens

def extract_words(input_path: str, max_words: int = DEFAULT_MAX_WORDS) -> List[str]:
    """Extracts, cleans, and deduplicates words from an arbitrary source file."""
    ext = os.path.splitext(input_path)[1].lower()
    if ext == '.gguf':
        raw_tokens = extract_from_gguf(input_path)
    elif ext == '.json':
        raw_tokens = extract_from_json(input_path)
    else:
        # Check magic for GGUF before falling back to text
        try:
            with open(input_path, 'rb') as f:
                magic = f.read(4)
                if magic == b'GGUF':
                    raw_tokens = extract_from_gguf(input_path)
                else:
                    raw_tokens = extract_from_txt(input_path)
        except Exception:
            raw_tokens = extract_from_txt(input_path)

    seen: Set[str] = set()
    words: List[str] = []

    # Seed core domain & programming words first
    for w in CORE_DOMAIN_WORDS:
        if w not in seen:
            seen.add(w)
            words.append(w)

    for t in raw_tokens:
        w = clean_word(t)
        if w and w not in seen:
            seen.add(w)
            words.append(w)
            if len(words) >= max_words:
                break

    return words

def emit_c_header(words: List[str], output_path: str) -> None:
    """Emits C header vocab_base.h."""
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write("/* Auto-generated decoupled wordlist for liblemma (tok.c) */\n")
        f.write("#ifndef LEMMA_VOCAB_BASE_H\n")
        f.write("#define LEMMA_VOCAB_BASE_H\n\n")
        f.write("static const char *BASE_WORDS[] = {\n")
        for w in words:
            # Escape backslashes and double quotes
            escaped = w.replace('\\', '\\\\').replace('"', '\\"')
            f.write(f'\t"{escaped}",\n')
        f.write("\tNULL\n")
        f.write("};\n\n")
        f.write("#endif /* LEMMA_VOCAB_BASE_H */\n")

def emit_typescript(words: List[str], output_path: str) -> None:
    """Emits TypeScript module vocab_base.ts."""
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write("/* Auto-generated decoupled wordlist for pi-mm (tok.ts) */\n")
        f.write("export const BASE_WORDS: readonly string[] = [\n")
        for w in words:
            escaped = w.replace('\\', '\\\\').replace('"', '\\"')
            f.write(f'  "{escaped}",\n')
        f.write("];\n")

def emit_txt(words: List[str], output_path: str) -> None:
    """Emits newline-separated plain wordlist."""
    with open(output_path, 'w', encoding='utf-8') as f:
        for w in words:
            f.write(f"{w}\n")

def main():
    parser = argparse.ArgumentParser(
        description="Extract generic wordlists from models (GGUF, JSON) or wordlists for liblemma and pi-mm."
    )
    parser.add_argument("--input", "-i", required=True, help="Input file (.gguf, .json, .txt)")
    parser.add_argument("--max-words", "-m", type=int, default=DEFAULT_MAX_WORDS, help=f"Max words to extract (default {DEFAULT_MAX_WORDS})")
    parser.add_argument("--output-h", help="Path to write C header (vocab_base.h)")
    parser.add_argument("--output-ts", help="Path to write TypeScript file (vocab_base.ts)")
    parser.add_argument("--output-txt", help="Path to write plaintext wordlist (vocab.txt)")
    parser.add_argument("--dry-run", action="store_true", help="Print summary without writing files")

    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"Error: input file does not exist: {args.input}", file=sys.stderr)
        sys.exit(1)

    words = extract_words(args.input, args.max_words)
    print(f"Extracted {len(words)} distinct words from {args.input}")

    if args.dry_run:
        print("Sample words (first 30):", words[:30])
        return

    if args.output_txt:
        emit_txt(words, args.output_txt)
        print(f"Wrote plain wordlist to {args.output_txt}")

    if args.output_h:
        emit_c_header(words, args.output_h)
        print(f"Wrote C header to {args.output_h}")

    if args.output_ts:
        emit_typescript(words, args.output_ts)
        print(f"Wrote TypeScript module to {args.output_ts}")

if __name__ == "__main__":
    main()
