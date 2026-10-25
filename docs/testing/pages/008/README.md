# Test pages for the password manager (008)

Serve this folder and open the pages in Brouk, as the checklist in
`docs/testing/008-password-manager.md` says:

    cd docs/testing/pages/008 && python3 -m http.server 8765

Then use http://localhost:8765/login.html and the other pages. Brouk treats
localhost like an https site, so automatic filling works there.
