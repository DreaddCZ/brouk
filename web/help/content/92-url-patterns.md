## URL patterns {#url-patterns}

The same patterns are used by [link routing](#link-routing-patterns), [request rules](#devtools-request-rules), [never-record sites](#history-never-record):

| Pattern | Matches |
|---|---|
| `example.com` | that host only |
| `*.example.com` | every subdomain of `example.com` (and `example.com` itself) |
| `example.com/path/` or `example.com/path/*` | pages whose path starts with `/path/` |
| `example.com:8443` | that host on that port |

- A scheme (`https://`) may be written; it is ignored. Hosts ignore case; paths don't.
- When several patterns match, the **most specific** wins: a longer path, then an exact host over a wildcard, then a longer host.
- Lists in settings separate patterns with commas: `login.microsoftonline.com, *.okta.com`.
- Only link routing accepts a part after `#` (e.g. `portal.azure.com/#@contoso.onmicrosoft.com/*`); requests never carry one.
