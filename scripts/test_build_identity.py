"""Check that volatile build identity reaches only BuildInfo.cpp."""
from pathlib import Path
import runpy
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = runpy.run_path(str(ROOT / "scripts/git_branch.py"))

class Env(dict):
    def __init__(self, name="default"):
        super().__init__(PROJECT_DIR=str(ROOT), PIOENV=name)
        self.defines = []
        self.middleware = []
    def Append(self, **kwargs): self.defines.extend(kwargs.get("CPPDEFINES", []))
    def Clone(self):
        copy = Env(self["PIOENV"])
        copy.defines = list(self.defines)
        return copy
    def Object(self, node, **kwargs):
        return (node, kwargs.get("CPPDEFINES", self.defines))
    def AddBuildMiddleware(self, callback, pattern): self.middleware.append((callback, pattern))

class BuildIdentityTest(unittest.TestCase):
    def test_identity_only_changes_one_compile_environment(self):
        function = MODULE["inject_version"]
        globals_ = function.__globals__
        previous = globals_["get_git_short_sha"]
        self.addCleanup(globals_.__setitem__, "get_git_short_sha", previous)
        for name in ("default", "sticky", "x4-pro", "x4-classic", "debug", "sticky-debug", "x4-pro-debug", "test", "gh_release_rc", "x4-pro-simulator"):
            captures = []
            for sha in ("abc1234", "def5678"):
                globals_["get_git_short_sha"] = lambda _: sha
                env = Env(name)
                function(env)
                self.assertEqual(len(env.middleware), 1)
                callback, pattern = env.middleware[0]
                self.assertEqual(pattern, "*src/util/BuildInfo.cpp")
                _, scoped = callback(env, "src/util/BuildInfo.cpp")
                names = [d[0] for d in env.defines if isinstance(d, tuple)]
                self.assertNotIn("CROSSINK_GIT_SHA", names)
                self.assertNotIn("CROSSINK_GIT_DIRTY", names)
                self.assertNotIn("CROSSINK_VERSION", names)
                self.assertIn("CROSSINK_GIT_SHA", dict(d for d in scoped if isinstance(d, tuple)))
                captures.append((env.defines, scoped))
            self.assertEqual(captures[0][0], captures[1][0])
            self.assertNotEqual(captures[0][1], captures[1][1])

    def test_identity_is_not_added_to_framework_sources(self):
        env = Env()
        MODULE["inject_version"](env)
        callback, _ = env.middleware[0]

        source = "C:/platform/FrameworkArduino/ColorFormat.c"
        self.assertEqual(callback(env, source), source)

        names = [d[0] for d in env.defines if isinstance(d, tuple)]
        self.assertNotIn("CROSSINK_GIT_SHA", names)
        self.assertNotIn("CROSSINK_GIT_DIRTY", names)
        self.assertNotIn("CROSSINK_VERSION", names)
        self.assertEqual(env.defines, [])

    def test_windows_wrapper_captures_identity_without_registering_objects(self):
        env = Env()
        MODULE["inject_version"](env)
        callback, _ = env.middleware[0]
        captured = []
        env.Object = lambda node, **kwargs: captured.append((node, kwargs))
        callback(env, "C:/platform/FrameworkArduino/ColorFormat.c")
        self.assertEqual(captured, [])
        callback(env, "src/util/BuildInfo.cpp")
        self.assertEqual(len(captured), 1)
        defines = dict(captured[0][1]["CPPDEFINES"])
        self.assertIn("CROSSINK_GIT_SHA", defines)
        self.assertIn("CROSSINK_VERSION", defines)

if __name__ == "__main__": unittest.main()
