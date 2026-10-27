"""The displayed version must survive frameworks bypassing source middleware."""
import contextlib
import io
import os
from pathlib import Path
import runpy
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


class BuildEnv(dict):
    def __init__(self):
        super().__init__(PROJECT_DIR=str(ROOT), PIOENV='default', CPPDEFINES=[])

    def Append(self, **values):
        for key, value in values.items():
            self.setdefault(key, []).extend(value)

    def AddBuildMiddleware(self, callback, pattern):
        # Intentionally do not invoke it: version injection must not require it.
        pass


class BuildVersionTest(unittest.TestCase):
    def test_version_is_available_without_source_middleware(self):
        for override in (None, '9.0.0-crosscover.3'):
            with self.subTest(override=override), patch.dict(os.environ):
                os.environ.pop('CROSSINK_RC_HASH', None)
                os.environ.pop('CROSSINK_RELEASE_VERSION', None)
                if override:
                    os.environ['CROSSINK_RELEASE_VERSION'] = override
                with contextlib.redirect_stdout(io.StringIO()):
                    script = runpy.run_path(str(ROOT / 'scripts/git_branch.py'))
                    env = BuildEnv()
                    script['inject_version'](env)
                defines = dict(env['CPPDEFINES'])
                expected = override or script['get_crossink_version'](str(ROOT))
                self.assertEqual(defines['CROSSINK_VERSION'].replace('\\', ''), f'"{expected}"')


if __name__ == '__main__':
    unittest.main()
