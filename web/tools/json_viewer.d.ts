/**
 * The JSON viewer (web/jsonview/viewer.ts), loaded by the tools page as a
 * plain script (shared/json_viewer.js). Shows `data`, parsed from
 * `rawText`, in `host` as a tree with search and Tree / Raw / Pretty views.
 */
declare function jsonViewRender(host: HTMLElement, data: unknown, rawText: string): void;
