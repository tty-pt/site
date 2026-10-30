#ifndef SOFTWARE_MARKDOWN_H
#define SOFTWARE_MARKDOWN_H

#include <stddef.h>

/**
 * @brief Parse a simple Markdown document into HTML.
 *
 * Supports:
 * - Headings: #, ##, ###, ####
 * - Unordered lists: - item, * item
 * - Ordered lists: 1. item
 * - Fenced code blocks: ```lang ... ```
 * - Inline code: `code`
 * - Links: [label](url) and raw https:// / http:// URLs
 * - Bold: **text** or __text__
 * - Italic: *text* or _text_
 * - Blockquotes: > quote
 * - Horizontal rules: --- or ***
 * - Paragraphs with automatic line grouping
 * - Full HTML entity escaping to prevent XSS
 *
 * @param md Markdown input string (UTF-8).
 * @return Dynamically allocated HTML string (caller must free with free()),
 *         or NULL on OOM.
 */
char *markdown_to_html(const char *md);

#endif /* SOFTWARE_MARKDOWN_H */
