#!/usr/bin/env python3
"""Deterministic build-time GLSL comment & whitespace minifier for C shader tables.

Reads a C source file containing GLSL string literals (e.g. nd_seven_gl_shaders.c),
optionally expands pure string-literal GLSL macros from --macro-header files, and
writes an equivalent C source file with comments, blank lines, and redundant
whitespace stripped inside GLSL string literals while preserving all C structure,
macros, preprocessor directives (#version/#define/#ifdef/...), and chunk newline
boundaries required by multi-part glShaderSource() arrays.
"""

import argparse
import re
import sys
from pathlib import Path

IDENT_CHAR = re.compile(r"[A-Za-z0-9_]")
SAFE_PUNCT = set("{}();,?:[]")
OP_CHARS = set("+-*/<>&|=!%^~")


def unescape_c_str(raw: str) -> str:
    out = []
    i = 0
    n = len(raw)
    while i < n:
        ch = raw[i]
        if ch == "\\" and i + 1 < n:
            nxt = raw[i + 1]
            if nxt == "n":
                out.append("\n")
                i += 2
            elif nxt == "t":
                out.append("\t")
                i += 2
            elif nxt == "r":
                out.append("\r")
                i += 2
            elif nxt == '"':
                out.append('"')
                i += 2
            elif nxt == "\\":
                out.append("\\")
                i += 2
            else:
                out.append(ch)
                out.append(nxt)
                i += 2
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def escape_c_str(s: str) -> str:
    return (
        s.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )


def strip_glsl_comments(text: str) -> str:
    """Strip /* ... */ and // ... comments while preserving line breaks."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "*":
            end = text.find("*/", i + 2)
            if end == -1:
                break
            out.append(" ")
            i = end + 2
        elif ch == "/" and nxt == "/":
            end = text.find("\n", i + 2)
            if end == -1:
                break
            out.append("\n")
            i = end + 1
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def minify_glsl_line(line: str) -> str:
    """Collapse redundant whitespace in a single non-preprocessor GLSL line."""
    line = re.sub(r"[ \t]+", " ", line).strip()
    if not line:
        return ""
    res = []
    i = 0
    n = len(line)
    while i < n:
        ch = line[i]
        if ch == " ":
            prev = res[-1] if res else ""
            nxt = line[i + 1] if i + 1 < n else ""
            if not prev or not nxt:
                i += 1
                continue
            if prev in SAFE_PUNCT or nxt in SAFE_PUNCT:
                i += 1
                continue
            if (prev in OP_CHARS and nxt not in OP_CHARS) or (
                nxt in OP_CHARS and prev not in OP_CHARS
            ):
                i += 1
                continue
            res.append(" ")
        else:
            res.append(ch)
        i += 1
    return "".join(res)


def minify_glsl_chunk(
    text: str, keep_leading_space: bool, keep_trailing_space: bool
) -> str:
    if "\n" not in text and "/*" not in text and "//" not in text and "  " not in text:
        return text

    had_trailing_nl = text.endswith("\n")
    had_leading_sp = len(text) > 0 and text[0] in " \t"
    had_trailing_sp = len(text) > 0 and text[-1] in " \t"

    cleaned = strip_glsl_comments(text)
    raw_lines = cleaned.split("\n")

    out_parts = []
    prev_was_pp = False
    for raw_ln in raw_lines:
        stripped = raw_ln.strip()
        if not stripped:
            continue
        if stripped.startswith("#"):
            pp_ln = re.sub(r"[ \t]+", " ", stripped)
            if out_parts and not out_parts[-1].endswith("\n"):
                out_parts.append("\n")
            out_parts.append(pp_ln + "\n")
            prev_was_pp = True
        else:
            mln = minify_glsl_line(stripped)
            if not mln:
                continue
            if out_parts and not prev_was_pp:
                prev_ch = out_parts[-1][-1]
                first_ch = mln[0]
                if prev_ch not in "{};,(\n" and first_ch not in "});,]":
                    if (prev_ch in OP_CHARS and first_ch in OP_CHARS) or (
                        IDENT_CHAR.match(prev_ch) and IDENT_CHAR.match(first_ch)
                    ):
                        out_parts.append(" ")
                    elif prev_ch not in OP_CHARS and first_ch not in OP_CHARS:
                        out_parts.append(" ")
            out_parts.append(mln)
            prev_was_pp = False

    result = "".join(out_parts)
    if had_trailing_nl and result and not result.endswith("\n"):
        result += "\n"
    if keep_leading_space and had_leading_sp and result and result[0] not in " \t\n":
        result = " " + result
    if keep_trailing_space and had_trailing_sp and result and result[-1] not in " \t\n":
        result = result + " "
    return result


def emit_c_string_literals(s: str, max_chunk: int = 1800) -> str:
    if not s:
        return '""'
    esc = escape_c_str(s)
    if len(esc) <= max_chunk:
        return f'"{esc}"'
    chunks = []
    i = 0
    n = len(esc)
    while i < n:
        end = min(i + max_chunk, n)
        bs = 0
        j = end - 1
        while j >= i and esc[j] == "\\":
            bs += 1
            j -= 1
        if bs % 2 == 1:
            end -= 1
        chunks.append(f'"{esc[i:end]}"')
        i = end
    return "\n\t".join(chunks)


def load_string_macros(header_paths: list[Path]) -> dict[str, str]:
    """Parse object-like #define NAME "..." (and simple aliases) from headers."""
    raw_defs: dict[str, str] = {}
    aliases: dict[str, str] = {}
    for hp in header_paths:
        text = hp.read_text(encoding="utf-8")
        # Join backslash-continued lines
        lines = text.splitlines()
        i = 0
        while i < len(lines):
            ln = lines[i]
            if re.match(r"^\s*#\s*define\s+([A-Za-z_]\w*)\b(?!\()", ln):
                full = ln
                while full.endswith("\\") and i + 1 < len(lines):
                    full = full[:-1] + "\n" + lines[i + 1]
                    i += 1
                m = re.match(r"^\s*#\s*define\s+([A-Za-z_]\w*)\s+(.*)$", full, re.S)
                if m:
                    name, body = m.group(1), m.group(2).strip()
                    if re.fullmatch(r"[A-Za-z_]\w*", body):
                        aliases[name] = body
                    else:
                        # Check if body consists solely of string literals and whitespace/comments
                        body_no_c = re.sub(r"/\*.*?\*/", " ", body, flags=re.S)
                        body_no_c = re.sub(r"//[^\n]*", "", body_no_c)
                        rem = re.sub(r'"(?:\\.|[^"\\])*"', "", body_no_c).strip()
                        if not rem:
                            lits = re.findall(r'"((?:\\.|[^"\\])*)"', body_no_c)
                            if lits:
                                val = "".join(unescape_c_str(x) for x in lits)
                                if "\n" in val:
                                    raw_defs[name] = val
            i += 1

    # Resolve single-hop aliases
    for name, target in aliases.items():
        if target in raw_defs:
            raw_defs[name] = raw_defs[target]
    return raw_defs


def lex_c_and_minify(src: str, string_macros: dict[str, str]) -> str:
    tokens = []
    i = 0
    n = len(src)
    while i < n:
        ch = src[i]
        if ch == "#" and (i == 0 or src[i - 1] == "\n"):
            j = i
            while j < n:
                if src[j] == "\\" and j + 1 < n and src[j + 1] == "\n":
                    j += 2
                elif src[j] == "\n":
                    j += 1
                    break
                else:
                    j += 1
            tokens.append(("CODE", src[i:j]))
            i = j
        elif ch == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i + 2)
            if j == -1:
                j = n
            tokens.append(("WS", src[i:j]))
            i = j
        elif ch == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            if j == -1:
                j = n
            else:
                j += 2
            tokens.append(("WS", src[i:j]))
            i = j
        elif ch == '"':
            j = i + 1
            while j < n:
                if src[j] == "\\" and j + 1 < n:
                    j += 2
                elif src[j] == '"':
                    j += 1
                    break
                else:
                    j += 1
            tokens.append(("STR", src[i:j]))
            i = j
        elif ch == "'":
            j = i + 1
            while j < n:
                if src[j] == "\\" and j + 1 < n:
                    j += 2
                elif src[j] == "'":
                    j += 1
                    break
                else:
                    j += 1
            tokens.append(("CODE", src[i:j]))
            i = j
        elif ch in " \t\r\n":
            j = i + 1
            while j < n and src[j] in " \t\r\n":
                j += 1
            tokens.append(("WS", src[i:j]))
            i = j
        elif IDENT_CHAR.match(ch):
            j = i + 1
            while j < n and IDENT_CHAR.match(src[j]):
                j += 1
            ident = src[i:j]
            if ident in string_macros:
                tokens.append(("MACRO_STR", string_macros[ident]))
            else:
                tokens.append(("CODE", ident))
            i = j
        else:
            tokens.append(("CODE", ch))
            i += 1

    out = []
    idx = 0
    nt = len(tokens)
    while idx < nt:
        kind, val = tokens[idx]
        if kind not in ("STR", "MACRO_STR"):
            out.append(val)
            idx += 1
            continue

        run_strs = [unescape_c_str(val[1:-1]) if kind == "STR" else val]
        end = idx + 1
        last_str = idx
        while end < nt:
            tk = tokens[end][0]
            if tk == "WS":
                end += 1
            elif tk == "STR":
                run_strs.append(unescape_c_str(tokens[end][1][1:-1]))
                last_str = end
                end += 1
            elif tk == "MACRO_STR":
                run_strs.append(tokens[end][1])
                last_str = end
                end += 1
            else:
                break

        p = idx - 1
        while p >= 0 and tokens[p][0] == "WS":
            p -= 1
        prev_is_macro = p >= 0 and tokens[p][0] == "CODE" and (
            tokens[p][1].endswith(")") or bool(IDENT_CHAR.match(tokens[p][1][-1]))
        )
        q = last_str + 1
        while q < nt and tokens[q][0] == "WS":
            q += 1
        next_is_macro = q < nt and tokens[q][0] == "CODE" and bool(
            IDENT_CHAR.match(tokens[q][1][0])
        )

        combined = "".join(run_strs)
        minified = minify_glsl_chunk(
            combined,
            keep_leading_space=prev_is_macro,
            keep_trailing_space=next_is_macro,
        )
        out.append(emit_c_string_literals(minified))
        idx = last_str + 1

    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description="Build-time GLSL C string minifier")
    ap.add_argument("--macro-header", action="append", default=[], help="Header to parse pure GLSL string macros from")
    ap.add_argument("input_c", type=Path)
    ap.add_argument("output_c", type=Path)
    args = ap.parse_args()

    macros = load_string_macros([Path(p) for p in args.macro_header])
    src = args.input_c.read_text(encoding="utf-8")
    minified = lex_c_and_minify(src, macros)
    args.output_c.parent.mkdir(parents=True, exist_ok=True)
    args.output_c.write_text(minified, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
