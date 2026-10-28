# Security

## Reporting a vulnerability

Please report security problems **privately**, not as a public issue: on GitHub, open [Security → Report a vulnerability](https://github.com/DreaddCZ/brouk/security/advisories/new). Only the maintainer sees the report.

Include the Brouk version (Brouk → About Brouk), your Mac (Apple Silicon or Intel), the macOS version, and the steps to reproduce.

Brouk is maintained by one person in their spare time. You can expect a reply within 7 days. Once a fix is released, the advisory is published with credit to you, unless you prefer not to be named.

## Supported versions

Only the latest release gets security fixes. Brouk updates itself, so most users are on it within a day.

## Scope

In scope: Brouk's own code in this repository, its update feed and its release builds.

Problems in Chromium or CEF themselves belong to [Chromium's](https://www.chromium.org/Home/chromium-security/reporting-security-bugs/) and [CEF's](https://github.com/chromiumembedded/cef/security) reporting processes. Brouk takes their fixes by following CEF releases closely (see [`docs/cef-updates.md`](docs/cef-updates.md)). If a Chromium fix has not reached Brouk yet, that is worth reporting here.
