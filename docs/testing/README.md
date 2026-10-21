# Manual test pages

Pages in `pages/` exercise browser features; each page lists the expected
results. Open them in Brouk with `file://…`, or — for features that need a
secure origin (permissions, notifications, clipboard) — serve them locally:

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory docs/testing/pages
# then open http://localhost:8765/permissions.html
```
