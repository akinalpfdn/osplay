# Contributing to Osplay

Thanks for your interest. Osplay is a small project with firm design rules; reading this page first
saves review round-trips.

## Before you start

- **Scope:** Osplay is an extended display, not remote desktop. Input forwarding will not be added.
  The roadmap lives in [`DEVPLAN.md`](DEVPLAN.md).
- **Bugs and features:** open an issue with the matching template before large changes, so the
  approach can be agreed first.
- **Security issues:** never in public issues — see [`SECURITY.md`](SECURITY.md).
- By participating you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).

## Development setup

Follow [`docs/BUILDING.md`](docs/BUILDING.md) exactly. It covers macOS (terminal) and Windows
(command line) and is kept accurate by CI.

## Workflow

1. Branch from `main`: `feature/<short-name>`, `fix/<short-name>`, `docs/<short-name>`.
2. Keep each branch to one concern. Refactors and features go in separate pull requests.
3. Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/):
   `feat(core): add nack bookkeeping`, `fix(macos): stop capture on permission revoke`.
   Lowercase, imperative, no trailing period, at most 72 characters in the subject.
4. Run the checks before pushing:
   - macOS: `scripts/lint.sh` (formatting, clang-tidy, localization completeness).
   - Windows: `dotnet format windows/app/Osplay.slnx --verify-no-changes`.
5. Open a pull request using the template. CI must be green before review.

## Code rules

These are enforced in review and, where possible, by tooling:

- **No hardcoded user-facing text.** Every string goes through `Localizable.xcstrings` (macOS) or
  `.resx` (Windows) and must exist in **English and Turkish**. `core` and the Windows engine return
  error codes, never text. `scripts/check_localization.py` fails on missing translations.
- **No hardcoded visual values.** Colors, fonts, spacing, radii and shadows come from the platform
  theme (`macos/Osplay/Theme`, `windows/app/Osplay.App/Theme`).
- **No hardcoded tunables.** Protocol constants live in `core`; timeouts, bitrates and limits live
  in one constants file per platform.
- **Errors are explicit.** No exceptions across the C API, no silent catches, no force unwraps in
  Swift outside tests.
- **Network input is hostile.** Validate every length and index before use.
- Comments explain *why*, not *what*.

## Tests

- Every `core` module has unit tests (GoogleTest; Swift tests for the bridge).
- Every bug fix comes with a test that fails before the fix.
- Hardware paths (capture, encode, present) are covered by the manual matrix in `docs/TESTING.md`
  once it exists.

## License

By contributing you agree that your contributions are licensed under the [MIT License](LICENSE).
