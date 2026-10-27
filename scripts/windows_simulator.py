"""Build/run the native Windows simulator with project-local portable tools."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.cache/windows-simulator'
SOURCE = ROOT / '.cache/crossink-simulator-windows'
REVISION = '7e6f78529b4a087c5a6528a09cc547e0da66631c'
BINARY = ROOT / '.pio/build/simulator-windows/program.exe'
COMPILER = CACHE / 'w64devkit/bin/g++.exe'
SDL = CACHE / 'SDL2-2.32.10/x86_64-w64-mingw32'
PATCH = ROOT / 'scripts/windows-simulator.patch'


def run(arguments, **kwargs):
    subprocess.run([str(value) for value in arguments], check=True, **kwargs)


def download(name, url, checksum):
    destination = CACHE / name
    if not destination.exists() or hashlib.sha256(destination.read_bytes()).hexdigest() != checksum:
        print(f'Downloading {name} into {CACHE}', flush=True)
        partial = destination.with_suffix(destination.suffix + '.part')
        with urllib.request.urlopen(url, timeout=60) as response, partial.open('wb') as output:
            shutil.copyfileobj(response, output)
        if hashlib.sha256(partial.read_bytes()).hexdigest() != checksum:
            raise RuntimeError(f'Checksum verification failed: {partial}')
        partial.replace(destination)
    return destination


def setup():
    CACHE.mkdir(parents=True, exist_ok=True)
    if not COMPILER.exists():
        archive = download('w64devkit.7z.exe',
            'https://github.com/skeeto/w64devkit/releases/download/v2.10.1/w64devkit-x64-2.10.1.7z.exe',
            'add1edf984ce4d473cfeb7c52e0ab38db73a855bba6c7f564d5e16648eb2896a')
        run([archive, '-y', f'-o{CACHE}'])
    if not (SDL / 'bin/SDL2.dll').exists():
        archive = download('SDL2-devel.tar.gz',
            'https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz',
            '83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2')
        with tarfile.open(archive) as bundle:
            bundle.extractall(CACHE, filter='data')
    patch_hash = hashlib.sha256(PATCH.read_bytes()).hexdigest()
    marker = SOURCE / '.windows-patch-applied'
    if not SOURCE.exists():
        run(['git', 'clone', 'https://github.com/uxjulia/crossink-simulator', SOURCE])
        run(['git', '-C', SOURCE, 'checkout', '--detach', REVISION])
    if marker.exists() and marker.read_text().strip() == patch_hash:
        return
    # Never overwrite edits in this separate source checkout on a patch update.
    reverse = subprocess.run(['git', '-C', str(SOURCE), 'apply', '--reverse', '--check', str(PATCH)],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if reverse.returncode != 0:
        run(['git', '-C', SOURCE, 'apply', '--check', PATCH])
        run(['git', '-C', SOURCE, 'apply', PATCH])
    marker.write_text(patch_hash + '\n')


def build(jobs):
    setup()
    config = (ROOT / 'platformio.ini').read_text(encoding='utf-8')
    flags = '\n  '.join([
        f'-I{SDL.as_posix()}/include/SDL2', f'-L{SDL.as_posix()}/lib',
        '-lSDL2', '-lws2_32', '-lbcrypt', '-lwinhttp', '-DNOMINMAX',
        '-DWIN32_LEAN_AND_MEAN', '-DSDL_MAIN_HANDLED',
        f'-include {(SOURCE / "src/WindowsHostCompat.h").as_posix()}',
    ])
    config = config.replace('!sdl2-config --cflags --libs', flags)
    config = config.replace('simulator=https://github.com/uxjulia/crossink-simulator',
                            'simulator=symlink://.cache/crossink-simulator-windows')
    config = config.replace('post:.pio/libdeps/$PIOENV/simulator/run_simulator_project.py',
                            'post:.cache/crossink-simulator-windows/run_simulator_project.py')
    config += '\n[env:simulator-windows]\nextends = env:simulator\n'
    destination = CACHE / 'platformio.ini'
    destination.write_text(config, encoding='utf-8')
    environment = os.environ.copy()
    environment['PATH'] = str(COMPILER.parent) + os.pathsep + environment['PATH']
    environment['PYTHONUTF8'] = '1'
    if (ROOT / '.pio-core312/penv').exists():
        environment['PLATFORMIO_CORE_DIR'] = str(ROOT / '.pio-core312')
    run([sys.executable, '-m', 'platformio', 'run', '-d', ROOT, '-c', destination,
         '-e', 'simulator-windows', '-j', jobs], env=environment)
    shutil.copy2(SDL / 'bin/SDL2.dll', BINARY.parent / 'SDL2.dll')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true', help='Rebuild before opening the simulator')
    parser.add_argument('--build-only', action='store_true', help='Build without opening a window')
    parser.add_argument('-j', '--jobs', type=int, default=4)
    args = parser.parse_args()
    if os.name != 'nt' or sys.maxsize <= 2**32:
        parser.error('This launcher requires 64-bit Windows and 64-bit Python')
    if args.jobs < 1:
        parser.error('Jobs must be positive')
    os.chdir(ROOT)
    if args.build or args.build_only or not BINARY.exists():
        build(args.jobs)
    if args.build_only:
        return
    (ROOT / 'fs_/books').mkdir(parents=True, exist_ok=True)
    print('Arrows: navigate | Enter: select | Esc: back | P: power | F12: screenshot', flush=True)
    print(f'Screenshots: {CACHE / "screenshots"}', flush=True)
    run([BINARY])


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'Windows simulator failed: {error}', file=sys.stderr)
        sys.exit(1)
