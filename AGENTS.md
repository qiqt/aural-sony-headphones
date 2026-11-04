# Aural development

## Workflow

- Work directly on `main`. Do not create branches or worktrees unless asked.
- Check the repository path, branch and working-tree status before editing.
- Preserve existing changes and untracked files. Do not switch checkouts to
  avoid a problem in this checkout.
- Commit completed work separately from unrelated changes. Report the commit,
  checks run and any remaining limitations. Do not push unless asked.

## Commits

- Use `type(scope): what changed`, imperative and at most 72 characters.
- Keep an optional body under 20 lines, wrapped at 72 columns. Explain the
  change, reason, issue IDs, verification and any material limitation.
- No metaphors, rhetorical framing, repeated summaries, all-caps emphasis,
  generated-by footers or co-author trailers.

## Code and copy

- Follow the surrounding C++ and QML conventions; avoid unrelated formatting.
- Keep Bluetooth and protocol handling in the engine, application state in
  `HeadphonesController`, and presentation in QML.
- Preserve Aural's existing layout, theme and reusable controls.
- Name the action or state directly. Remove jargon, promotional language,
  repeated explanations and implementation details from routine screens.
- Keep distinctions that affect use: requested, acknowledged and confirmed
  settings; unknown and reported values; available and unsupported controls.
- Keep documentation focused on installation, development, protocol details
  and reproducible checks. Do not add session transcripts or completion reports.
- Preserve license notices. Keep credentials and raw device captures out of Git.

## Verification

See `BUILD.md` for commands and `packaging/README.md` for portable builds.

- Run `git diff --check` for every change.
- For engine changes, build `Client/tests` and run its CTest suites.
- For controller or QML changes, build `Client/qt`, run its CTest suites and
  run `qmllint -W 0` against the built module.
- For UI changes, inspect the affected screens, states and keyboard actions
  in the running app. Demo mode checks presentation, not Bluetooth behavior.
- For packaging changes, build `aural_package` and check its required files.
- Report source inspection, automated tests, demo checks and hardware tests
  separately. Record the model, firmware and build for hardware checks.
- Do not change headphone settings or reconnect devices solely for cleanup.
