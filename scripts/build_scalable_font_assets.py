"""Embed generated outline files only for opted-in S3 environments."""
from pathlib import Path
import hashlib

def generate(root):
    source = root / 'lib/EpdFont/scalableFonts'
    output = root / 'lib/ScalableFont/ScalableAssets.generated.h'
    lines = ['#pragma once', '#include <cstdint>']
    identity = hashlib.sha256()
    for filename in (source/'manifest.txt').read_text().splitlines():
        data = (source/filename).read_bytes()
        identity.update(filename.encode()); identity.update(data)
        name = Path(filename).stem
        lines.append(f'alignas(4) static const uint8_t {name}Outline[] = {{')
        lines.extend(','.join(f'0x{b:02x}' for b in data[i:i+24])+',' for i in range(0,len(data),24))
        lines.append('};')
    lines.append(f'static constexpr uint32_t OutlineFingerprint = 0x{identity.hexdigest()[:8]}u;')
    content='\n'.join(lines)+'\n'
    if not output.exists() or output.read_text()!=content: output.write_text(content)

try:
    Import('env')
except NameError:
    generate(Path(__file__).resolve().parents[1])
else:
    flags = env.GetProjectOption('build_flags', [])
    if 'CROSSINK_SCALABLE_FONTS=1' in ' '.join(flags):
        generate(Path(env.subst('$PROJECT_DIR')))
