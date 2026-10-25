# 021 – File viewers for text formats

- **Status:** idea
- **Priority:** P3
- **Depends on:** 006 (JSON viewer), 012

## Goal

Open the text files that come up in daily work (XML, YAML, Markdown, CSV, logs, config and source files) readably in the tab, instead of as one plain block of text or as a download. This is the same idea as the JSON viewer (006), extended to other formats, so checking an API response, a pipeline config or a README never needs another app.

## Description

High level only; details are specified when this spec is picked up.

### What Brouk already has (not part of this spec)

- **PDF:** Chromium's PDF viewer, through CEF (012).
- **JSON:** Brouk's own JSON viewer (006).
- **Images, SVG, plain text, audio and video:** Chromium itself (012; codec limits noted there).
- **XML:** Chromium's built-in XML tree view. Check on the current CEF build that it works; this spec only adds what is missing.

### Source viewer

One viewer, in the style of the JSON viewer, for text formats Chromium shows as plain text:

- syntax highlighting, line numbers, wrap on/off, search with next/previous, copy;
- formats: XML (also as an alternative to Chromium's tree), YAML, TOML, INI/`.env`, CSS, JavaScript/TypeScript, shell scripts, SQL, logs, diffs/patches, and plain text;
- detected by content type and file extension, also for `file://` URLs;
- large files stay responsive (as for JSON: a generated 10 MB file must not freeze the tab);
- a "Raw" switch shows Chromium's own view.

### Rendered views

- **Markdown:** rendered as a page (headings, tables, code blocks, links), with a switch to the source view.
- **CSV / TSV:** shown as a table with a header row, sorting and search, with a switch to the source view.
- **XML:** a collapsible tree with search and "copy path" (XPath), like the JSON viewer, if Chromium's tree view turns out to be too limited.

### Common rules

- Same safety as the JSON viewer: the viewer runs isolated from the page's scripts, and file content is only ever set as text, never as HTML. Rendered Markdown is sanitised: no scripts, no raw HTML, links open as normal links.
- Each viewer can be turned off in settings (Settings > Pages), like `content.json_viewer`.
- Works the same on macOS and Windows; no platform-specific code expected.

## Acceptance criteria

To be written when the spec is detailed.

## Out of scope

- Office documents (Word, Excel, PowerPoint): these download, as in Chrome.
- Editing files.
- Further binary formats (archives, databases, images beyond what Chromium shows).

## Configuration

One on/off setting per viewer (`content.source_viewer`, `content.markdown_viewer`, `content.csv_viewer`), on by default; the existing `content.json_viewer` stays as it is.

## Open questions

1. Which formats matter most? (Order of work.)
2. Highlighting and Markdown rendering need code that ships in the app: help (016) uses markdown-it only at build time. Use pinned libraries (needs approval, `CLAUDE.md`) or write small own highlighters for the most used formats?
3. Should Markdown and CSV open rendered or as source by default?

## Notes

- 2026-10-25 (from discussion with the user): CEF already brings the PDF viewer, images, media and an XML tree view; JSON is covered by 006. This spec records the remaining text formats as a later feature, so it is not forgotten.
- The JSON viewer's approach (TypeScript compiled at build time, embedded in the renderer, run in a wrapper so pages see none of its names, lazy building of large documents) is the starting point; the viewers should share one module for search, copy and the Raw switch.
