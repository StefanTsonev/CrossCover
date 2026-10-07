"""Focused, host-only checks for the wolfSSL build patches."""
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

class WolfSslPatchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        class Env:
            def subst(inner, value): return str(self.root)
            def Append(inner, **kwargs): pass
        self.module = runpy.run_path(str(Path(__file__).with_name("patch_wolfssl.py")),
                                    init_globals={"Import": lambda _: None, "env": Env()})

    def test_shrink_patch_is_exact_and_idempotent(self):
        path = self.root / "internal.c"
        path.write_text("before\n" + self.module["SHRINK_ORIGINAL"] + "after\n")
        self.module["patch_input_buffer_shrink"](path)
        once = path.read_bytes()
        self.module["patch_input_buffer_shrink"](path)
        self.assertEqual(once, path.read_bytes())
        self.assertIn(b"if (!forcedFree)", once)

    def test_reverting_build_flag_restores_original_guard(self):
        path = self.root / "guard.c"
        path.write_text(self.module["SHRINK_PATCHED"])
        original = subprocess.check_output(["cc", "-E", "-P", "-x", "c", str(path)], text=True)
        retained = subprocess.check_output(["cc", "-E", "-P", "-x", "c", "-DFREEINK_WOLFSSL_RETAIN_INPUT_BUFFER", str(path)], text=True)
        self.assertIn("usedLength > STATIC_BUFFER_LEN", original)
        self.assertNotIn("usedLength > STATIC_BUFFER_LEN", retained)
        self.assertIn("if (!forcedFree)", retained)

    def test_unknown_source_is_rejected(self):
        path = self.root / "internal.c"
        path.write_text("upstream changed")
        with self.assertRaises(RuntimeError): self.module["patch_input_buffer_shrink"](path)
        self.assertEqual(path.read_text(), "upstream changed")


if __name__ == "__main__": unittest.main()
