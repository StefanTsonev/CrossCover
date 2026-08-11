---
title: CrossCover Upstream and Release Workflow
parent: Contributing
nav_order: 4
---

# CrossCover Upstream and Release Workflow

This document is the source of truth for maintaining CrossCover as a fork of
CrossInk. It is intentionally separate from the product README so the rules
can evolve without changing the user-facing documentation.

The complete list of CrossCover-specific features and files is maintained in
the [CrossCover Customization Manifest](./crosscover-customizations.md). Use it
when importing every new upstream release.

## Project relationship

CrossCover is a downstream firmware fork with two permanent product features:

* Hardcover integration, including library search, reading progress, and
  Hardcover-specific settings.
* Anna's Archive search and download through the CrossCover Cloudflare Worker
  relay.

CrossCover must preserve X3/X4 reader stability and the OpenX4/FreeInk SDK
choice documented for the current release. A future upstream SDK migration is
a deliberate integration project, not an incidental side effect of syncing a
release.

## Remotes and branches

`origin` is the CrossCover GitHub repository. `upstream` is the CrossInk
repository and is read-only from this project.

The long-lived branch is only:

* `main` — release-ready CrossCover firmware.

Temporary branches use these forms:

* `feat/<short-name>` — one user-facing feature.
* `fix/<short-name>` — one bug fix.
* `chore/<short-name>` — repository, build, or documentation maintenance.
* `integrate/crossink-vX.Y.Z` — temporary upstream integration branch.
* `release/vX.Y.Z-crosscover.N` — optional release-candidate branch.

Do not keep permanent `development`, `next`, or feature branches. Delete a
temporary branch after its pull request is merged.

## Upstream integration

Never merge a new CrossInk release directly into `main`. Create an integration
branch from `main`, merge the upstream tag once, resolve conflicts, build, and
open a pull request:

```sh
git fetch upstream --tags --prune
git switch main
git pull --ff-only
git switch -c integrate/crossink-v1.6.0
git merge --no-ff --no-commit upstream/v1.6.0
```

The upstream merge commit must be preserved. Do not squash it; Git needs the
merge ancestry to calculate the next upstream delta correctly. Keep any
CrossCover compatibility changes after the merge as separate commits.

Before resolving a conflict, inspect the high-risk files:

```sh
git diff main upstream/v1.6.0 -- .gitmodules platformio.ini
git diff --name-status main upstream/v1.6.0
```

The following areas require explicit review:

* `.gitmodules` and all PlatformIO SDK/library paths;
* `platformio.ini`, partitions, and firmware size limits;
* Home, Settings, Reader, ActivityManager, and Wi-Fi integration points;
* `lib/Hardcover/` and Hardcover activities;
* `src/network/ShadowLibraryClient.*` and `worker/`;
* generated web and i18n files;
* release workflows and version injection.

Enable conflict reuse in each clone:

```sh
git config rerere.enabled true
git config fetch.prune true
git config pull.ff only
```

## SDK policy

CrossInk v1.5 moved from the OpenX4 SDK to the FreeInk SDK. Do not partially
mix the two SDKs. Before accepting an upstream SDK migration, port and test
Hardcover and Anna's Archive against the new HAL/network contracts. If the
migration is not complete, keep the integration branch experimental and do not
merge it into `main`.

The goal is to reduce CrossCover-specific changes to small adapters and
integration hooks. API parsing, URL construction, ranking, and relay logic
should not depend on display or reader implementation details.

## Commit rules

Use one focused change per commit. Prefer Conventional Commit subjects:

```text
feat(hardcover): restore library search
feat(annas): add relay download handling
fix(reader): preserve word spacing in cache
build: update CrossCover release metadata
chore(repo): add upstream integration policy
```

Keep the subject imperative and short. Explain the problem and the reason in
the body; do not use the body as a transcript of commands. Do not combine
formatting-only changes, generated files, SDK migrations, and product features
in one commit.

Normal feature pull requests may use squash merge. Upstream integration pull
requests must retain the upstream merge commit.

## Required checks

Before opening a pull request, run the checks relevant to the change:

```sh
./bin/clang-format-fix
pio check -e default --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run -e simulator
pio run -e default
```

For reader/cache changes, clear the affected `.crosspoint/epub_<hash>/` cache
before hardware testing. For network changes, record serial logs including
free heap, largest free block, HTTP status, and TLS errors.

Every hardware-facing pull request must state:

* device tested (X3 or X4);
* firmware environment and resulting image size;
* exact UI/network path tested;
* cache reset performed, if any;
* expected and observed result.

## Release process

1. Merge the tested integration branch into `main` through a pull request.
2. Add the changelog entry under a new CrossCover version.
3. Create a CrossCover release tag, for example `v1.6.0-crosscover.1`.
4. Build the release artifacts from that tag.
5. Verify the artifact size and SHA-256 checksum.
6. Publish release notes that identify the upstream base and CrossCover
   changes.
7. Delete the temporary integration/release branch after publication.

Upstream tags such as `v1.5.0` are references only. CrossCover releases are
created in the CrossCover repository and must never overwrite upstream tags.

## What must not happen

* Do not force-push `main`.
* Do not push to `upstream`.
* Do not copy an entire upstream tree over CrossCover without reviewing the
  SDK and custom integration files.
* Do not edit generated web or i18n headers directly.
* Do not publish releases to CrossInk-owned repositories, buckets, or catalogs.
* Do not claim an upstream feature is supported until it builds and is tested
  on the target X4 hardware.
