#!/usr/bin/env python3
"""Check silent-restart light restoration using the X4 Pro simulator's RTC bridge.

Build first with: pio run -e x4-pro-simulator -j1
USB's physical storage handoff and reset still require a device check.
"""

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

PROGRAM = Path(__file__).resolve().parents[1] / ".pio/build/x4-pro-simulator/program"
MAGIC = 0xC1EAB007
LIGHT_VALID = 1 << 31
LIGHT_ON = 1 << 30


def run(work, target=None, magic=MAGIC):
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("CROSSPOINT_SIM_", "CROSSINK_SIMULATOR_SMOKE"))}
    env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_INPUT_SCRIPT="500:QUIT",
               CROSSPOINT_SIM_WAKE_REASON="power")
    if target is not None:
        env.update(CROSSPOINT_SIM_SILENT_REBOOT_MAGIC=str(magic),
                   CROSSPOINT_SIM_SILENT_REBOOT_TARGET=str(target),
                   CROSSPOINT_SIM_SILENT_REBOOT_PAYLOAD="0")
    result = subprocess.run([str(PROGRAM)], cwd=work, env=env, capture_output=True,
                            text=True, timeout=20)
    output = result.stdout + result.stderr
    if result.returncode:
        raise AssertionError(output)
    return output


def expect_light(output, on, silent):
    expected = f"Frontlight boot state: {'on' if on else 'off'} (silent={int(silent)})"
    states = re.findall(r"Frontlight boot state: (?:on|off) \(silent=[01]\)", output)
    if not states or any(state != expected for state in states):
        raise AssertionError(f"Missing {expected}\n{output}")


def check_state(live_on):
    with tempfile.TemporaryDirectory(prefix="crossink-restart-light-") as directory:
        work = Path(directory)
        state_dir = work / "fs_/.crosspoint"
        state_dir.mkdir(parents=True)
        now = time.gmtime()
        minute = now.tm_hour * 60 + now.tm_min
        # Saved state and the schedule both disagree with the live restart state.
        settings = {
            "frontlightOn": int(not live_on), "frontlightRestoreOnWake": 0,
            "frontlightBrightness": 37, "frontlightWarmth": 63,
            "frontlightScheduleEnabled": 1, "clockUtcOffsetQ": 48,
            "frontlightScheduleStart": (minute + (60 if live_on else -60)) % 1440,
            "frontlightScheduleEnd": (minute + (120 if live_on else 60)) % 1440,
        }
        settings_path = state_dir / "crossink-settings.json"
        settings_path.write_text(json.dumps(settings))
        (state_dir / "state.json").write_text(json.dumps({"showBootScreen": False}))
        flags = LIGHT_VALID | (LIGHT_ON if live_on else 0)
        # Home, reader, and every network destination use the same boot policy.
        for target in range(8):
            output = run(work, flags | target)
            expect_light(output, live_on, silent=True)
            route = f"target={target}" if target >= 2 else "Entering activity: Home"
            if route not in output:
                raise AssertionError(f"Restart flags changed the destination\n{output}")
            # Missing OPDS configuration takes a real silent restart back Home,
            # exercising live-state capture as well as the injected boot token.
            if target == 3 and ("Silent restart (target=home)" not in output
                                or output.count("Frontlight boot state:") != 2):
                raise AssertionError(f"OPDS fallback did not restart through Home\n{output}")
            restored = json.loads(settings_path.read_text())
            for key, value in settings.items():
                if restored.get(key) != value:
                    raise AssertionError(f"Restart changed saved preference {key}")
        # Old tokens have no live state; invalid tokens must never restore it.
        expect_light(run(work, 0), not live_on, silent=True)
        expect_light(run(work, flags, magic=0), not live_on, silent=False)
        expect_light(run(work), not live_on, silent=False)
        print(f"PASS: live light {'on' if live_on else 'off'}, all destinations, "
              "saved preferences, legacy token, invalid token, and later wake")


if __name__ == "__main__":
    check_state(True)
    check_state(False)
