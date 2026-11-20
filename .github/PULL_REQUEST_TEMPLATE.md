## Summary

<!-- What changes, and which problem it solves. One topic per pull request. -->

## Type

- [ ] Bug fix
- [ ] Feature
- [ ] Refactor (no behaviour change)
- [ ] Documentation
- [ ] Build / CI / packaging
- [ ] Performance

## Related issue

<!-- Link the issue or relevant section of docs/architecture.md, where applicable. -->

## Verification

<!-- List the commands and desktop configurations you verified. -->

- [ ] `cmake --build --preset dev` succeeds
- [ ] `ctest --preset dev --output-on-failure` passes
- [ ] `cmake --build --preset dev --target format-check` passes
- [ ] Tests added or updated for changed behaviour
- [ ] Manual checks (fill in what applies):

| Display server | Desktop / compositor | Scaling | Result |
| --- | --- | --- | --- |
|  |  |  |  |
|  |  |  |  |

## Checklist

- [ ] Changes are scoped to this topic; no unrelated reformatting or refactoring.
- [ ] No new dependency (or justification below).
- [ ] No secrets, tokens, personal paths, or captured user content in the diff or test fixtures.
- [ ] Docs updated (`docs/`, `README.md`, `CHANGELOG.md` → `Unreleased`).
- [ ] Commits are signed off (`git commit -s`, DCO).
- [ ] UI strings are translatable (`tr()` / `qsTr()`), no hardcoded text.
- [ ] No blocking work added to the UI thread.

## Dependency justification

<!-- Required only when adding a dependency: why, size, license, packaging impact. -->

## Notes for reviewers

<!-- Trade-offs, follow-up work, known limitations, anything you deliberately did not do. -->
