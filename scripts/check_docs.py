#!/usr/bin/env python3
"""Validate CrossCover documentation links, branding, and release metadata."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]
MARKDOWN_LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
FENCED_CODE_RE = re.compile(r"```.*?```", re.DOTALL)
HEADING_RE = re.compile(r"^#{1,6}\s+(.+?)\s*#*$", re.MULTILINE)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def heading_anchors(text: str) -> set[str]:
    anchors: set[str] = set()
    occurrences: dict[str, int] = {}
    for heading in HEADING_RE.findall(FENCED_CODE_RE.sub("", text)):
        heading = re.sub(r"<[^>]+>", "", heading)
        heading = re.sub(r"[`*_~]", "", heading).strip().lower()
        base = re.sub(r"[^\w\- ]", "", heading).replace(" ", "-")
        occurrence = occurrences.get(base, 0)
        occurrences[base] = occurrence + 1
        anchors.add(base if occurrence == 0 else f"{base}-{occurrence}")
    return anchors


def check_links(errors: list[str]) -> None:
    paths = [ROOT / "README.md", *(ROOT / "docs").rglob("*.md")]
    anchors_by_path = {path.resolve(): heading_anchors(read(path)) for path in paths}
    for path in paths:
        text = FENCED_CODE_RE.sub("", read(path))
        for match in MARKDOWN_LINK_RE.finditer(text):
            target = match.group(1).strip().split()[0].strip("<>")
            if not target or target.startswith(("#", "http://", "https://", "mailto:")):
                continue
            file_part, _, anchor = target.partition("#")
            resolved = (path.parent / file_part).resolve() if file_part else path.resolve()
            if file_part and not resolved.exists():
                line = text.count("\n", 0, match.start()) + 1
                errors.append(f"{path.relative_to(ROOT)}:{line}: missing link target {target}")
            elif anchor and resolved in anchors_by_path and unquote(anchor) not in anchors_by_path[resolved]:
                line = text.count("\n", 0, match.start()) + 1
                errors.append(f"{path.relative_to(ROOT)}:{line}: missing heading anchor {target}")


def capture(pattern: str, text: str, label: str, errors: list[str]) -> str:
    match = re.search(pattern, text, re.MULTILINE)
    if match:
        return match.group(1)
    errors.append(f"Could not read {label}")
    return ""


def check_release_metadata(errors: list[str]) -> None:
    readme_version = capture(
        r"^Current release: `([^`]+)`$", read(ROOT / "README.md"), "README release version", errors
    )
    changelog_version = capture(
        r"^## \[v([^\]]+)\]", read(ROOT / "CHANGELOG.md"), "latest changelog version", errors
    )
    display_version = capture(
        r'^#define CROSSINK_DISPLAY_VERSION "([^"]+)"$',
        read(ROOT / "lib/AppVersion/AppVersion.h"),
        "firmware display version",
        errors,
    )
    base_version = capture(
        r"^version = (\S+)$", read(ROOT / "platformio.ini"), "PlatformIO base version", errors
    )

    versions = {readme_version, changelog_version, display_version}
    if "" not in versions and len(versions) != 1:
        errors.append(
            "Release version mismatch: "
            f"README={readme_version}, CHANGELOG={changelog_version}, firmware={display_version}"
        )
    if readme_version and base_version and not readme_version.startswith(f"{base_version}-crosscover."):
        errors.append(
            f"CrossCover release {readme_version} is not based on PlatformIO version {base_version}"
        )


def check_release_targets(errors: list[str]) -> None:
    unsupported = ("firmware-sticky", "firmware-x4-pro", "firmware-x4-classic")
    for relative in (".github/workflows/release.yml", ".github/workflows/release_candidate.yml"):
        text = read(ROOT / relative)
        for firmware in unsupported:
            if firmware in text:
                errors.append(f"{relative}: official workflow references unsupported artifact {firmware}")


def check_public_branding(errors: list[str]) -> None:
    for relative in ("docs/index.md", "docs/user-guide.md"):
        for line_number, line in enumerate(read(ROOT / relative).splitlines(), 1):
            if line.startswith("# CrossInk"):
                errors.append(f"{relative}:{line_number}: public heading must use CrossCover branding")


def main() -> int:
    errors: list[str] = []
    check_links(errors)
    check_release_metadata(errors)
    check_release_targets(errors)
    check_public_branding(errors)
    if errors:
        print("Documentation validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("Documentation validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
