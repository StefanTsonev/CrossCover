#!/usr/bin/env python3
"""Exercise the real simulator HTTP rename route with an isolated SD card.

Run after: pio run -e simulator -j1
The fixtures need not contain readable books: rename must preserve their bytes
and migrate valid bookmark/clipping records without parsing book content.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zlib


ROOT = Path(__file__).resolve().parents[1]
TITLE = "Saved book title"
AUTHOR = "Saved author"


def packed_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return struct.pack("<I", len(encoded)) + encoded


def store_fixture(path: str, clipping: bool = False) -> bytes:
    header = struct.pack("<BH", 4 if clipping else 5, 1)
    header += packed_string(TITLE) + packed_string(AUTHOR) + packed_string(path)
    chapter = b"Chapter 2".ljust(48, b"\0")
    if clipping:
        text = "A saved excerpt with café.".encode("utf-8")
        record = struct.pack("<8HIIH", 1, 2, 2, 10, 3, 8, 100, 4, 123456, 789, 65535)
        record += chapter + struct.pack("<H", len(text)) + text
    else:
        record = struct.pack("<HfI", 1, 0.375, 123456)
        record += chapter + struct.pack("<H", 4) + b"Saved paragraph".ljust(64, b"\0")
    return header + record


def store_contents(data: bytes) -> tuple[list[str], bytes]:
    offset = 3  # version, uint16 count
    fields = []
    for _ in range(3):
        length = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        fields.append(data[offset : offset + length].decode("utf-8"))
        offset += length
    return fields, data[offset:]


def tree_contents(root: Path) -> dict[str, bytes]:
    return {str(p.relative_to(root)): p.read_bytes() for p in root.rglob("*") if p.is_file()}


def store_path(path: str, kind: str, clipping: bool = False) -> Path:
    crc = zlib.crc32(path.encode("utf-8"))
    directory = "clippings" if clipping else "bookmarks"
    return Path(".crosspoint") / directory / f"{kind}_{crc}.bin"


def native_hashes(root: Path, paths: list[str]) -> dict[str, int]:
    # TXT/XTC cache names use the simulator toolchain's std::hash, which varies
    # by standard library. Compile this helper with that native compiler.
    source = root / "hash.cpp"
    source.write_text(
        '#include <functional>\n#include <iostream>\n#include <string>\n'
        'int main(int argc, char** argv) { for (int i = 1; i < argc; ++i) '
        'std::cout << std::hash<std::string>{}(argv[i]) << "\\n"; }\n'
    )
    executable = root / "hash"
    subprocess.run(["g++", "-std=c++20", str(source), "-o", str(executable)], check=True)
    values = subprocess.check_output([str(executable), *paths], text=True).splitlines()
    return dict(zip(paths, map(int, values), strict=True))


def cache_path(path: str, kind: str, hashes: dict[str, int]) -> Path:
    if kind == "epub":
        value = 14695981039346656037
        for byte in path.encode("utf-8"):
            value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    else:
        value = hashes[path]
    return Path(".crosspoint") / f"{kind}_{value}"


def available_port() -> int:
    with socket.socket() as http, socket.socket() as websocket:
        http.bind(("127.0.0.1", 0))
        port = http.getsockname()[1]
        websocket.bind(("127.0.0.1", port + 1))
        return port


def run(program: Path) -> None:
    cases = [
        ("/books/Café.epub", "/books/Renamed café.EPUB", "epub"),
        ("/books/Pages.xtc", "/books/Renamed pages.xtc", "xtc"),
        ("/books/Gray.xtch", "/books/Renamed gray.xtch", "xtc"),
        ("/books/Text.txt", "/books/Renamed text.txt", "txt"),
        ("/books/Notes.md", "/books/Renamed notes.md", "txt"),
        ("/books/Cache conflict.epub", "/books/Occupied cache.epub", "epub"),
        ("/books/Bad clippings.epub", "/books/Rejected clippings.epub", "epub"),
        ("/books/Recent failure.epub", "/books/Rejected recent.epub", "epub"),
    ]
    with tempfile.TemporaryDirectory(prefix="crossink-web-rename-") as temporary:
        root = Path(temporary)
        sd = root / "fs_"
        sd.mkdir()
        hashes = native_hashes(root, [path for old, new, _ in cases for path in (old, new)])
        recent = []
        for old, _, kind in cases:
            book = sd / old.lstrip("/")
            book.parent.mkdir(parents=True, exist_ok=True)
            book.write_bytes(b"Book contents must survive a rename.\n")
            cache = sd / cache_path(old, kind, hashes)
            for name in ("progress.bin", "progress.bin.bak", "stats_v5.bin", "stats_v5.bin.bak",
                         "reader_settings.bin", "reading_stats_off", "dictionary_history.txt",
                         "thumb.bmp", "sections/3.bin", "images/cover.pxc"):
                target = cache / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((old + ":" + name).encode("utf-8"))
            for clipping in (False, True) if kind == "epub" else (False,):
                target = sd / store_path(old, kind, clipping)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(store_fixture(old, clipping))
            recent.append({"path": old, "title": TITLE, "author": AUTHOR,
                           "coverBmpPath": "/" + str(cache.relative_to(sd) / "thumb.bmp")})

        state_path = sd / ".crosspoint/state.json"
        pinned_image = "/books/pinned.bmp"
        (sd / pinned_image.lstrip("/")).write_bytes(b"Pinned image bytes")
        state_path.write_text(json.dumps({"openEpubPath": cases[0][0],
                                         "favoriteSleepImagePath": pinned_image,
                                         "favoriteBootImagePath": pinned_image}))
        recent_path = sd / ".crosspoint/recent.json"
        recent.append({"path": "/books/Old recent.epub", "title": "Previous book"})
        recent_path.write_text(json.dumps({"books": recent}))
        global_stats = sd / ".crosspoint/global_stats.bin"
        global_stats.write_bytes(b"Global reading history stays intact.")
        port = available_port()
        url = f"http://127.0.0.1:{port}"
        env = os.environ.copy()
        # SimulatorLifecycle restores this existing firmware boot token. Target
        # 6 is FILE_TRANSFER; payload 2 selects CREATE_HOTSPOT without UI timing.
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_HTTP_PORT=str(port),
                   CROSSPOINT_SIM_SILENT_REBOOT_MAGIC=str(0xC1EAB007),
                   CROSSPOINT_SIM_SILENT_REBOOT_TARGET="6", CROSSPOINT_SIM_SILENT_REBOOT_PAYLOAD="2")
        env.pop("CROSSINK_SIMULATOR_SMOKE_TEST", None)
        env.pop("CROSSPOINT_SIM_INPUT_SCRIPT", None)
        log_path = root / "simulator.log"
        with log_path.open("w") as log:
            process = subprocess.Popen([str(program)], cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 45
                while True:
                    try:
                        with urllib.request.urlopen(url + "/", timeout=1) as response:
                            if response.status == 200:
                                break
                    except (OSError, urllib.error.URLError):
                        if process.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError("Simulator web portal did not start")
                        time.sleep(0.1)

                def rename(old: str, new: str) -> tuple[int, str]:
                    body = urllib.parse.urlencode({"path": old, "name": Path(new).name}).encode()
                    request = urllib.request.Request(url + "/rename", data=body)
                    try:
                        response = urllib.request.urlopen(request, timeout=10)
                    except urllib.error.HTTPError as error:
                        response = error
                    with response:
                        return response.code, response.read().decode()

                checks = 0

                def unchanged_after_rejection(old: str, new: str, status: int) -> None:
                    nonlocal checks
                    before = tree_contents(sd)
                    actual, message = rename(old, new)
                    assert actual == status, (old, actual, message)
                    assert tree_contents(sd) == before, f"Rejected rename changed saved files: {old}"
                    checks += 1
                    print(f"PASS: rejected rename preserves all files ({Path(old).name})")

                unchanged_after_rejection(cases[0][0], "/books/Changed.txt", 400)
                unchanged_after_rejection(cases[0][0], "/books/Changed.bin", 400)
                target = sd / "books/Existing.epub"
                target.write_bytes(b"Existing book")
                unchanged_after_rejection(cases[0][0], "/books/Existing.epub", 409)

                old, new, kind = cases[5]
                conflict = sd / cache_path(new, kind, hashes)
                conflict.mkdir()
                (conflict / "stats_v5.bin").write_bytes(b"Other saved history")
                unchanged_after_rejection(old, new, 409)

                # Orphaned destination state must never attach to an uncached
                # source book. Cover cache, saved-item and recovery files.
                uncached = "/books/Uncached source.epub"
                (sd / uncached.lstrip("/")).write_bytes(b"A different book")
                for category in ("cache", "bookmarks", "clippings", "clipping backup", "recent"):
                    destination = f"/books/Old {category}.epub"
                    if category == "recent":
                        unchanged_after_rejection(uncached, destination, 409)
                        continue
                    if category == "cache":
                        stale = sd / cache_path(destination, "epub", hashes) / "stats_v5.bin"
                    else:
                        stale = sd / store_path(destination, "epub", category.startswith("clipping"))
                        if category == "clipping backup":
                            stale = Path(str(stale) + ".bak")
                    stale.parent.mkdir(parents=True, exist_ok=True)
                    stale.write_bytes(b"Previous book's saved history")
                    unchanged_after_rejection(uncached, destination, 409)

                old, new, kind = cases[6]
                (sd / store_path(old, kind, True)).write_bytes(b"corrupt clipping store")
                unchanged_after_rejection(old, new, 500)

                # Force recent-store replacement to fail after the book and
                # cache moved. This must restore both plus staged saved items.
                blocked_temp = sd / ".crosspoint/recent.json.tmp"
                blocked_temp.mkdir()
                (blocked_temp / "block").write_bytes(b"Cannot replace this directory")
                unchanged_after_rejection(cases[7][0], cases[7][1], 500)
                (blocked_temp / "block").unlink()
                blocked_temp.rmdir()

                for old, new, kind in cases[:5]:
                    old_cache = sd / cache_path(old, kind, hashes)
                    new_cache = sd / cache_path(new, kind, hashes)
                    cache_before = tree_contents(old_cache)
                    book_before = (sd / old.lstrip("/")).read_bytes()
                    status, message = rename(old, new)
                    assert status == 200, (old, status, message)
                    assert not (sd / old.lstrip("/")).exists()
                    assert (sd / new.lstrip("/")).read_bytes() == book_before
                    assert not old_cache.exists()
                    assert tree_contents(new_cache) == cache_before
                    for clipping in (False, True) if kind == "epub" else (False,):
                        assert not (sd / store_path(old, kind, clipping)).exists()
                        fields, records = store_contents((sd / store_path(new, kind, clipping)).read_bytes())
                        assert fields == [TITLE, AUTHOR, new]
                        assert records == store_contents(store_fixture(old, clipping))[1]
                    entry = next(book for book in json.loads(recent_path.read_text())["books"]
                                 if book["path"] == new)
                    assert entry["coverBmpPath"] == "/" + str(new_cache.relative_to(sd) / "thumb.bmp")
                    assert entry["title"] == TITLE and entry["author"] == AUTHOR
                    assert global_stats.read_bytes() == b"Global reading history stays intact."
                    assert json.loads(state_path.read_text())["openEpubPath"] == cases[0][1]
                    checks += 1
                    print(f"PASS: full book-state migration ({Path(old).name})")

                for filename in ("Unopened.epub", "image.png"):
                    old = "/books/" + filename
                    new = "/books/Renamed " + filename
                    (sd / old.lstrip("/")).write_bytes(b"Uncached file")
                    status, message = rename(old, new)
                    assert status == 200, (filename, status, message)
                    assert (sd / new.lstrip("/")).read_bytes() == b"Uncached file"
                    assert not (sd / old.lstrip("/")).exists()
                    checks += 1
                    print(f"PASS: rename without cached book state ({filename})")

                pinned_renamed = "/books/pinned-renamed.bmp"
                status, message = rename(pinned_image, pinned_renamed)
                assert status == 200, (status, message)
                state = json.loads(state_path.read_text())
                assert state["favoriteSleepImagePath"] == pinned_renamed
                assert state["favoriteBootImagePath"] == pinned_renamed
                assert (sd / pinned_renamed.lstrip("/")).read_bytes() == b"Pinned image bytes"
                checks += 1
                print("PASS: pinned boot and sleep image references follow portal rename")

                # If the resume reference cannot be saved or rolled back, the
                # helper keeps the new filename and data together. The portal
                # must report this partial recovery instead of claiming success.
                old = cases[0][1]
                new = "/books/Partial recovery.epub"
                cache_before = tree_contents(sd / cache_path(old, "epub", hashes))
                saved_state = state_path.read_bytes()
                state_path.unlink()
                blocked_state = state_path
                # Force an actual file-open failure through the native HAL.
                blocked_state.symlink_to(root / "missing-state/state.json")
                status, message = rename(old, new)
                assert status == 500 and message.startswith("File was renamed"), (status, message)
                assert not (sd / old.lstrip("/")).exists()
                assert (sd / new.lstrip("/")).read_bytes() == b"Book contents must survive a rename.\n"
                assert tree_contents(sd / cache_path(new, "epub", hashes)) == cache_before
                for clipping in (False, True):
                    assert not (sd / store_path(old, "epub", clipping)).exists()
                    fields, records = store_contents((sd / store_path(new, "epub", clipping)).read_bytes())
                    assert fields == [TITLE, AUTHOR, new]
                    assert records == store_contents(store_fixture(old, clipping))[1]
                assert any(book["path"] == new for book in json.loads(recent_path.read_text())["books"])
                checks += 1
                print("PASS: incomplete reference recovery reports the actual filename and preserves book data")
                unchanged_after_rejection(pinned_renamed, "/books/pinned-rejected.bmp", 500)
                blocked_state.unlink()
                state_path.write_bytes(saved_state)
                status, message = rename(pinned_renamed, "/books/pinned-final.bmp")
                assert status == 200, (status, message)
                state = json.loads(state_path.read_text())
                assert state["openEpubPath"] == new
                assert state["favoriteSleepImagePath"] == "/books/pinned-final.bmp"
                assert state["favoriteBootImagePath"] == "/books/pinned-final.bmp"
                checks += 1
                print("PASS: restored storage can persist the recovered book and pinned image references")
                print(f"{checks} web rename checks passed.")
            except BaseException:
                print(log_path.read_text()[-12000:])
                raise
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", type=Path, default=ROOT / ".pio/build/simulator/program")
    args = parser.parse_args()
    run(args.program.resolve())
