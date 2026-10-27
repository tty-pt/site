#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../../mods/software/markdown.h"

static void test_headings(void)
{
	const char *md = "# Title\n## Section\n### Sub\n#### Subsub\n";
	char *html = markdown_to_html(md);
	assert(html != NULL);
	assert(strstr(html, "<h1") != NULL);
	assert(strstr(html, "Title</h1>") != NULL);
	assert(strstr(html, "<h2") != NULL);
	assert(strstr(html, "Section</h2>") != NULL);
	assert(strstr(html, "<h3") != NULL);
	assert(strstr(html, "Sub</h3>") != NULL);
	assert(strstr(html, "<h4") != NULL);
	assert(strstr(html, "Subsub</h4>") != NULL);
	free(html);
	printf("PASS: test_headings\n");
}

static void test_formatting(void)
{
	const char *md = "This is **bold** and *italic* and `code`.\n";
	char *html = markdown_to_html(md);
	assert(html != NULL);
	assert(strstr(html, "<strong>bold</strong>") != NULL);
	assert(strstr(html, "<em>italic</em>") != NULL);
	assert(strstr(html, "<code>code</code>") != NULL);
	free(html);
	printf("PASS: test_formatting\n");
}

static void test_links(void)
{
	const char *md = "Visit [GitHub](https://github.com/tty-pt/corm) for code.\n"
	                 "Or direct link: https://github.com/tty-pt/site\n";
	char *html = markdown_to_html(md);
	assert(html != NULL);
	assert(strstr(html, "<a href=\"https://github.com/tty-pt/corm\"") != NULL);
	assert(strstr(html, "target=\"_blank\"") != NULL);
	assert(strstr(html, "GitHub</a>") != NULL);
	assert(strstr(html, "<a href=\"https://github.com/tty-pt/site\"") != NULL);
	free(html);
	printf("PASS: test_links\n");
}

static void test_code_blocks(void)
{
	const char *md = "```c\n#include <stdio.h>\nint main() { return 0; }\n```\n";
	char *html = markdown_to_html(md);
	assert(html != NULL);
	assert(strstr(html, "<pre><code class=\"language-c\">") != NULL);
	assert(strstr(html, "&lt;stdio.h&gt;") != NULL);
	assert(strstr(html, "return 0;") != NULL);
	free(html);
	printf("PASS: test_code_blocks\n");
}

static void test_lists(void)
{
	const char *md = "- First item\n- Second item\n\n1. Number one\n2. Number two\n";
	char *html = markdown_to_html(md);
	assert(html != NULL);
	assert(strstr(html, "<ul>") != NULL);
	assert(strstr(html, "<li>First item</li>") != NULL);
	assert(strstr(html, "</ul>") != NULL);
	assert(strstr(html, "<ol>") != NULL);
	assert(strstr(html, "<li>Number one</li>") != NULL);
	assert(strstr(html, "</ol>") != NULL);
	free(html);
	printf("PASS: test_lists\n");
}

static void test_escaping(void)
{
	const char *md = "Dangerous <script>alert('xss')</script> & \"quotes\"\n";
	char *html = markdown_to_html(md);
	assert(html != NULL);
	assert(strstr(html, "<script>") == NULL);
	assert(strstr(html, "&lt;script&gt;") != NULL);
	assert(strstr(html, "&amp;") != NULL);
	assert(strstr(html, "&quot;") != NULL);
	free(html);
	printf("PASS: test_escaping\n");
}

int main(void)
{
	printf("Running Markdown parser unit tests...\n");
	test_headings();
	test_formatting();
	test_links();
	test_code_blocks();
	test_lists();
	test_escaping();
	printf("All Markdown unit tests passed successfully!\n");
	return 0;
}
