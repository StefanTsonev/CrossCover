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

    def test_trace_is_opt_in_and_settings_are_idempotent(self):
        path = self.root / "user_settings.h"
        path.write_text("#define DEBUG_WOLFSSL\n")
        self.module["patch_user_settings"](path)
        once = path.read_bytes()
        self.module["patch_user_settings"](path)
        self.assertEqual(once, path.read_bytes())
        normal = subprocess.check_output(["cc", "-E", "-dM", "-x", "c", str(path)], text=True)
        debug = subprocess.check_output(["cc", "-E", "-dM", "-x", "c", "-DFREEINK_WOLFSSL_DEBUG", str(path)], text=True)
        self.assertNotIn("#define DEBUG_WOLFSSL", normal)
        self.assertIn("#define DEBUG_WOLFSSL", debug)

if __name__ == "__main__": unittest.main()
