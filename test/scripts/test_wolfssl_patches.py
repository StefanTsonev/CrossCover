"""Exercise the patch hook and the real wolfCrypt SHA-384 implementation."""
import hashlib
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WolfsslPatchTest(unittest.TestCase):
    def test_sha384_patch_and_real_digest(self):
        source = ROOT / '.pio/libdeps/default/Arduino-wolfSSL/src'
        compiler = shutil.which('gcc') or str(ROOT / '.cache/windows-simulator/w64devkit/bin/gcc.exe')
        if not source.exists() or not Path(compiler).exists():
            self.skipTest('wolfSSL sources and a host C compiler are required')
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            class Env:
                def subst(self, value):
                    return str(work)
                def Append(self, **_):
                    pass
            module = runpy.run_path(str(ROOT / 'scripts/patch_wolfssl.py'),
                                   init_globals={'Import': lambda *_: None, 'env': Env()})
            settings = work / 'user_settings.h'
            settings.write_text('''#include <stdio.h>
#define WC_NO_HASHDRBG
#define WC_NO_RNG
#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define NO_ASM
#define NO_RSA
#define NO_DH
#define NO_DSA
#define NO_ECC
#define NO_AES
#define NO_DES3
#define NO_RC4
#define NO_MD5
#define NO_SHA
#define NO_SHA256
#define NO_FILESYSTEM
''')
            module['patch_user_settings'](settings)
            once = settings.read_text()
            module['patch_user_settings'](settings)
            self.assertEqual(once, settings.read_text())
            self.assertEqual(once.count('#define WOLFSSL_SHA384'), 1)
            self.assertNotIn('#define WOLFSSL_SHA512', once)
            test = work / 'digest.c'
            test.write_text('''#include <wolfssl/wolfcrypt/hash.h>
#include <stdio.h>
int main(void) {
 unsigned char digest[48];
 if (wc_HashGetDigestSize(WC_HASH_TYPE_SHA384) != 48) return 1;
 if (wc_Hash(WC_HASH_TYPE_SHA384, (const unsigned char*)"abc", 3, digest, 48)) return 2;
 for (int i=0; i<48; ++i) printf("%02x", digest[i]);
 return 0;
}
''')
            output = work / 'digest.exe'
            result = subprocess.run([compiler, '-std=c99', '-DWOLFSSL_USER_SETTINGS', '-DWOLFSSL_OPTIONS_H',
                            '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                            '-I' + str(work), '-I' + str(source), str(test),
                            str(source / 'wolfcrypt/src/hash.c'), str(source / 'wolfcrypt/src/sha512.c'),
                            '-o', str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            actual = subprocess.check_output([str(output)], text=True)
            self.assertEqual(actual, hashlib.sha384(b'abc').hexdigest())


if __name__ == '__main__':
    unittest.main()
