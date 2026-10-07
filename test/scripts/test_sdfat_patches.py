#!/usr/bin/env python3
"""Test the patch hook and fault-inject the real SdFat cache, without a device.

Usage: python3 test/scripts/test_sdfat_patches.py /path/to/unpatched/SdFat-2.3.1
Only a temporary copy of the supplied dependency is changed.
"""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("patch_sdfat", ROOT / "scripts/patch_sdfat.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def expect_rejection(project, dependency):
    try:
        patch.apply_patches(project, dependency)
    except RuntimeError:
        return
    raise AssertionError("Unsafe patch input was accepted")


def run(source):
    with tempfile.TemporaryDirectory(prefix="crossink-sdfat-") as tmp:
        project = Path(tmp)
        shutil.copytree(ROOT / "scripts/sdfat_patches", project / "scripts/sdfat_patches")
        dependency = project / ".pio/libdeps/test/SdFat"
        shutil.copytree(source, dependency)
        patch.apply_patches(project, dependency)
        paths = [dependency / item[1] for item in patch.PATCHES]
        timestamps = [p.stat().st_mtime_ns for p in paths]
        patch.apply_patches(project, dependency)
        assert timestamps == [p.stat().st_mtime_ns for p in paths], "Reapplied patch rewrote files"
        # Shim only the transport selector; compile FsCache and its interfaces unchanged.
        stub = project / "stubs/SdCard"
        stub.mkdir(parents=True)
        (stub / "SdCard.h").write_text('class __FlashStringHelper;\n#include <common/SysCall.h>\n#include <common/FsBlockDeviceInterface.h>\n')
        for mode in (0, 1):
            executable = project / f"fault-test-{mode}"
            subprocess.run(["c++", "-std=c++17", "-include", "cstring", "-DENABLE_ARDUINO_FEATURES=0",
                            "-DUSE_BLOCK_DEVICE_INTERFACE=1", f"-DUSE_SEPARATE_FAT_CACHE={mode}",
                            f"-DEXPECTED_FAT_CACHE={mode}", "-I" + str(project / "stubs"),
                            "-I" + str(dependency / "src"), str(ROOT / "test/sdfat_cache/FsCacheFaultTest.cpp"),
                            str(dependency / "src/common/FsCache.cpp"), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
        # Validate every patch before changing any file (even if the first is pristine).
        paths[0].write_bytes((source / patch.PATCHES[0][1]).read_bytes())
        paths[1].write_text("unrecognized source\n")
        before = paths[0].read_bytes()
        expect_rejection(project, dependency)
        assert before == paths[0].read_bytes()
        (dependency / "library.properties").write_text("version=2.4.0\n")
        expect_rejection(project, dependency)
        expect_rejection(project, source)
        # A symlinked target may not escape the selected dependency directory.
        (dependency / "library.properties").write_text("version=2.3.1\n")
        outside = project / "outside.cpp"
        outside.write_bytes(before)
        paths[0].unlink()
        paths[0].symlink_to(outside)
        expect_rejection(project, dependency)
        assert outside.read_bytes() == before
        class SdkPass(dict):
            def GetLibBuilders(self):
                raise AssertionError("SDK-only pass must not resolve app dependencies")
        patch.patch_selected_dependency(SdkPass(ARDUINO_LIB_COMPILE_FLAG="Build"))
    print("PASS: cache read failures at all 513 lengths in both modes, dirty write retry, patch safety/idempotence")


if __name__ == "__main__":
    run(Path(sys.argv[1]).resolve())
