from pathlib import Path

Import("env")


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
MARKER = "/* CrossPoint wolfSSL compatibility overrides */"
OVERRIDES = f"""

{MARKER}
#undef NO_DH
#ifndef HAVE_FFDHE_2048
#define HAVE_FFDHE_2048
#endif
#undef FP_MAX_BITS
#define FP_MAX_BITS 8192
/* Arduino-wolfSSL turns on DEBUG_WOLFSSL, which compiles every WOLFSSL_MSG and
   WOLFSSL_ENTER trace string into flash. Keep it only for FREEINK_WOLFSSL_DEBUG. */
#ifndef FREEINK_WOLFSSL_DEBUG
#undef DEBUG_WOLFSSL
#endif
"""


def patch_user_settings(path: Path) -> None:
    original_text = path.read_text()
    text = original_text
    if MARKER in text:
        text = text.split(MARKER, 1)[0].rstrip()
    patched_text = text.rstrip() + OVERRIDES + "\n"
    if original_text == patched_text:
        return
    path.write_text(patched_text)
    print(f"Patched wolfSSL settings: {path.relative_to(PROJECT_DIR)}")


# wolfSSL grows its input buffer to fit each record larger than the static
# buffer, then frees it again as soon as the record is consumed. A server that
# ignores max_fragment_length sends 16 KB records, so a large download on a
# PSRAM-less C3 needs a fresh ~17 KB contiguous block for every record, and
# fails with MEMORY_E once fragmentation leaves none. Keep the grown buffer
# until the connection is freed: one allocation per connection, failing at the
# first record rather than megabytes into the transfer.
SHRINK_MARKER = "/* CrossInk: keep a grown input buffer until the connection is freed */"
SHRINK_ORIGINAL = """    if (!forcedFree && (usedLength > STATIC_BUFFER_LEN ||
            ssl->buffers.clearOutputBuffer.length > 0))
        return;
"""
# The source lives in PlatformIO's dependency cache. Keep the original branch
# available so reverting this script also disables retention in an already
# patched dependency, without requiring users to delete their package cache.
SHRINK_PATCHED = f"""    {SHRINK_MARKER}
#if defined(FREEINK_WOLFSSL_RETAIN_INPUT_BUFFER)
    if (!forcedFree)
        return;
#else
{SHRINK_ORIGINAL}#endif
"""
env.Append(CPPDEFINES=["FREEINK_WOLFSSL_RETAIN_INPUT_BUFFER"])


def patch_input_buffer_shrink(path: Path) -> None:
    text = path.read_text()
    if SHRINK_MARKER in text:
        if text.count(SHRINK_PATCHED) != 1:
            raise RuntimeError(f"Corrupt wolfSSL input buffer patch in {path}")
        return
    if text.count(SHRINK_ORIGINAL) != 1:
        raise RuntimeError(f"Unsupported wolfSSL ShrinkInputBuffer in {path}; refusing an unverified patch")
    path.write_text(text.replace(SHRINK_ORIGINAL, SHRINK_PATCHED, 1))
    print(f"Patched wolfSSL input buffer: {path.relative_to(PROJECT_DIR)}")


for settings in PROJECT_DIR.glob(".pio/libdeps/*/Arduino-wolfSSL/src/user_settings.h"):
    patch_user_settings(settings)

for internal in PROJECT_DIR.glob(".pio/libdeps/*/Arduino-wolfSSL/src/src/internal.c"):
    patch_input_buffer_shrink(internal)
