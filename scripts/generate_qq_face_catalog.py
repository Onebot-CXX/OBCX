#!/usr/bin/env python3
"""Refresh embedded QQ system faces offline (no QQ directories needed at build/runtime).

Requires ffmpeg at import time only. Writes one asset per ID: GIF when an
animation is available, otherwise PNG. index.inc describes the chosen format.
Example from the checkout root (use the installed QQ version's resource directory):
  python3 scripts/generate_qq_face_catalog.py \
    dev/onebot/QQ/global/nt_data/Emoji/emoji-resource src/onebot11/resources/qq_faces \
    --default-emojis dev/onebot/QQ/versions/3.2.35-54395/resource/default-emojis \
    --system-cache dev/onebot/QQ/nt_qq_*/nt_data/Emoji/BaseEmojiSyastems/EmojiSystermResource
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import struct
import subprocess
from tempfile import TemporaryDirectory
import zlib
from zipfile import ZipFile

ID = re.compile(r'0|[1-9][0-9]{0,19}')


def png_frames(data: bytes) -> int:
    """Zero for invalid PNG, one for still PNG, declared frame count for APNG."""
    frames = 1
    if not data.startswith(b'\x89PNG\r\n\x1a\n'):
        return False
    pos, pixels = 8, False
    while pos + 12 <= len(data):
        size, kind = struct.unpack_from('>I4s', data, pos)
        end = pos + 8 + size
        if end + 4 > len(data):
            return 0
        if kind == b'acTL':
            if size != 8 or pixels:
                return 0
            frames = struct.unpack_from('>I', data, pos + 8)[0]
        if pos == 8 and (kind != b'IHDR' or size != 13):
            return False
        if zlib.crc32(data[pos + 4:end]) != struct.unpack_from('>I', data, end)[0]:
            return False
        pos = end + 4
        pixels |= kind == b'IDAT' and size > 0
        if kind == b'IEND':
            return frames if pixels and size == 0 and pos == len(data) else 0
    return False


def convert_apng(item):
    i, data = item
    with TemporaryDirectory(prefix='qq-face-') as temp:
        path = Path(temp) / 'input.png'
        path.write_bytes(data)
        cmd = ['ffmpeg', '-nostdin', '-v', 'error', '-threads', '1', '-ignore_loop', '1',
               '-i', str(path), '-filter_complex_threads', '1', '-filter_complex',
               '[0:v]split[a][b];[a]palettegen=reserve_transparent=1[p];[b][p]paletteuse=alpha_threshold=128',
               '-fps_mode', 'passthrough', '-loop', '0', str(path.with_suffix('.gif'))]
        try:
            subprocess.run(cmd, check=True, capture_output=True, timeout=120)
        except subprocess.CalledProcessError as e:
            raise ValueError(f'face {i}: {e.stderr.decode(errors="replace")}') from e
        return i, path.with_suffix('.gif').read_bytes()


def generate(source, output, defaults, caches):
    names, pngs, animations, skipped = {}, {}, {}, set()

    def name(i, label):
        if isinstance(i, str) and ID.fullmatch(i) and isinstance(label, str):
            label = label.removeprefix('/')
            if label and not any(ord(c) < 32 for c in label):
                names.setdefault(i, label)

    def add(i, data):
        frames = png_frames(data)
        if frames == 1:
            pngs.setdefault(i, data)
        elif frames > 1:
            animations.setdefault(i, data)
        else:
            skipped.add(i)

    with ZipFile(source / 'face_config_json.zip') as archive:
        for row in json.loads(archive.read('face_config.json'))['sysface']:
            name(row['QSid'], row['QDes'])
    with ZipFile(source / 'sysface_res.zip') as archive:
        for path in sorted(archive.namelist(), key=lambda p: ('/static/' not in p, p)):
            m = re.fullmatch(r'sysface_res/(?:static|apng)/s([0-9]+)\.png', path)
            if m and ID.fullmatch(m[1]):
                add(m[1], archive.read(path))

    # Only read canonical files from the system emoji cache, never chat/user media.
    for cache in caches:
        if not cache.is_dir():
            raise ValueError(f'not a system emoji cache: {cache}')
        for directory in sorted(cache.iterdir()):
            i = directory.name
            if not ID.fullmatch(i):
                continue
            anim = directory / 'apng' / f'{i}.png'
            if i not in animations and anim.is_file():
                add(i, anim.read_bytes())
            if i in pngs:
                continue
            for filename in (f'{i}.png', f'{i}_0.png'):
                path = directory / 'png' / filename
                if path.is_file():
                    add(i, path.read_bytes())
                    if i in pngs:
                        break

    for root in defaults:
        config = json.loads((root / 'default_config.json').read_text(encoding='utf-8'))
        for panel in config.values():
            for group in panel['SysEmojiGroupList']:
                for row in group['SysEmojiList']:
                    name(row['emojiId'], row.get('describe'))
        for path in sorted(root.glob('*.png')):
            if ID.fullmatch(path.stem) and path.stem not in pngs:
                add(path.stem, path.read_bytes())

    with ThreadPoolExecutor(max_workers=6) as pool:
        gifs = dict(pool.map(convert_apng, animations.items()))
    assets = pngs | gifs
    if not assets:
        raise ValueError('no system faces found')
    # Only prune PNGs superseded by a successfully converted GIF. An omitted
    # source directory must not silently drop previously imported assets.
    stale = {p.name for p in output.glob('*.png')} - {f's{i}.png' for i in assets}
    stale |= {p.name for p in output.glob('*.gif')} - {f's{i}.gif' for i in gifs}
    if stale:
        raise ValueError(f'missing previously imported assets; check input sources: {sorted(stale)}')
    output.mkdir(parents=True, exist_ok=True)
    index = ['// Generated by scripts/generate_qq_face_catalog.py; QQ system face resources.']
    for i, data in sorted(assets.items()):  # C++ binary search uses string order
        ext = 'gif' if i in gifs else 'png'
        filename = f's{i}.{ext}'
        (output / filename).write_bytes(data)
        label = json.dumps(names.get(i, ''), ensure_ascii=False)
        index.extend((f'OBCX_QQ_FACE("{i}", {label}, {"true" if i in gifs else "false"})', f'#embed "{filename}"', 'OBCX_QQ_FACE_END'))
    (output / 'index.inc').write_text('\n'.join(index) + '\n', encoding='utf-8')
    for i in gifs:
        (output / f's{i}.png').unlink(missing_ok=True)
    (output / 'animations.inc').unlink(missing_ok=True)
    print(f'Imported {len(assets) - len(gifs)} PNGs and {len(gifs)} GIFs; skipped: {sorted(skipped - assets.keys(), key=int)}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('source', type=Path, help='QQ emoji-resource directory')
    parser.add_argument('output', type=Path)
    parser.add_argument('--default-emojis', type=Path, nargs='+', default=[])
    parser.add_argument('--system-cache', type=Path, nargs='+', default=[])
    args = parser.parse_args()
    generate(args.source, args.output, args.default_emojis, args.system_cache)
