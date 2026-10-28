# Contributing to Brouk

Thanks for helping. Brouk is a small project run by one maintainer, who decides what goes in. Please agree a change in an issue first, so your work isn't wasted.

## Reporting bugs and ideas

Open an [issue](https://github.com/DreaddCZ/brouk/issues/new/choose) with the bug report or feature idea form. The repository is public: do not include company or client names, internal addresses, account names, passwords, tokens, or screenshots of internal systems.

Security problems go through [`SECURITY.md`](SECURITY.md), never a public issue.

## Pull requests

1. **Open or find an issue first** and wait for the maintainer to agree with the change. Pull requests without an agreed issue may be closed. Typos and broken links are the exception.
2. Fork the repository and create a branch: `feat/NNN-short-name` for a spec, `fix/short-description` or `chore/…`.
3. Follow the rules in [`CLAUDE.md`](CLAUDE.md). It is written for Claude Code, but it is also the project's coding guide: the C++, Objective-C++ and TypeScript rules, the testing rules and the git workflow apply to everyone.
4. Open the pull request against `main` and fill in its template. The maintainer reviews every pull request. For first-time contributors, the CI run waits for the maintainer's approval.

By opening a pull request, you agree that your contribution is licensed under the [Apache License 2.0](LICENSE), as section 5 of the licence states. No separate agreement is needed.

## What a pull request needs

- **A spec.** Features are specified in [`specs/`](specs/README.md) first. Only specs with the status `ready` are implemented. A change of behaviour that no spec covers starts with an issue.
- **Tests** for every change, and a test that fails without the fix for every bug fix. Changes in behaviour also get a short manual test checklist in the pull request.
- **Help** updated for every user-visible change (`web/help/content/`).
- **Commit messages** in [Conventional Commits](https://www.conventionalcommits.org/) form: `type(scope): summary`, for example `fix(010): open links from finder in the right profile`.
- **No new third-party libraries** without the maintainer's approval (see the rules in `CLAUDE.md`).
- **No real company, client or organisation names**, and nothing that identifies them (domains, tenants, internal sites), in code, tests, docs or commits. Use placeholders: `contoso.onmicrosoft.com`, `example.com`, "Client A".

## Building and checking

[`docs/building.md`](docs/building.md) explains the setup. Before pushing, run:

```sh
scripts/build --test     # build and run the tests
scripts/format --check   # formatting
scripts/lint             # clang-tidy
scripts/sast             # security scanners
```

Enable the git hooks once per clone, so the scanners check every commit:

```sh
git config core.hooksPath .githooks
```

## Conduct

Be kind and assume good intent. The maintainer may remove comments and block accounts that are not.
