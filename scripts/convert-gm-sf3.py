"""Convert each SF2 sample to mono Vorbis (FFmpeg libvorbis quality 3).

Preserves preset/instrument tables and sample-relative loops. Only used for
this experiment; not a general-purpose SoundFont editor.
"""
import concurrent.futures
from pathlib import Path
import struct
import subprocess
import sys

def chunks(data):
    pos = 0
    while pos < len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        yield tag, data[pos + 8:pos + 8 + size]
        pos += 8 + size + size % 2
    assert pos == len(data)

def chunk(tag, data):
    return struct.pack('<4sI', tag, len(data)) + data + b'\0' * (len(data) % 2)

def convert(source, dest):
    data = Path(source).read_bytes()
    assert data[:4] == b'RIFF' and data[8:12] == b'sfbk'
    lists = list(chunks(data[12:]))
    sdta = dict(chunks(next(v[4:] for k, v in lists if k == b'LIST' and v[:4] == b'sdta')))
    pdta = dict(chunks(next(v[4:] for k, v in lists if k == b'LIST' and v[:4] == b'pdta')))
    fmt = '<20sIIIIIBbHH'
    headers = list(struct.iter_unpack(fmt, pdta[b'shdr']))
    def encode(h):
        name, start, end, loop_start, loop_end, rate, pitch, correction, link, kind = h
        assert kind in (1, 2, 4), (name, kind)
        pcm = sdta[b'smpl'][start * 2:end * 2]
        p = subprocess.run(['mise', 'exec', '--', 'ffmpeg', '-v', 'error', '-f', 's16le',
                            '-ar', str(rate), '-ac', '1', '-i', 'pipe:0', '-c:a', 'libvorbis',
                            '-q:a', '3', '-fflags', '+bitexact', '-flags:a', '+bitexact',
                            '-f', 'ogg', 'pipe:1'], input=pcm, capture_output=True)
        if p.returncode: raise RuntimeError(p.stderr.decode())
        return h, p.stdout
    pool = bytearray()
    new_headers = bytearray()
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as workers:
        for h, ogg in workers.map(encode, headers[:-1]):
            h = list(h)
            start = h[1]
            h[1], h[2] = len(pool), len(pool) + len(ogg)
            h[3] -= start
            h[4] -= start
            h[9] |= 0x10
            pool.extend(ogg)
            new_headers.extend(struct.pack(fmt, *h))
    new_headers.extend(struct.pack(fmt, b'EOS', 0, 0, 0, 0, 0, 0, 0, 0, 0))
    # Keep smpl itself even-sized: TinySoundFont does not skip RIFF pad bytes.
    if len(pool) % 2: pool.append(0)
    output = bytearray(b'sfbk')
    for tag, value in lists:
        if tag == b'LIST' and value[:4] in (b'INFO', b'sdta', b'pdta'):
            kind = value[:4]
            rebuilt = bytearray(kind)
            for subtag, subvalue in chunks(value[4:]):
                if kind == b'INFO' and subtag == b'ifil': subvalue = struct.pack('<HH', 3, 0)
                if kind == b'sdta' and subtag == b'smpl': subvalue = bytes(pool)
                if kind == b'sdta' and subtag == b'sm24': continue
                if kind == b'pdta' and subtag == b'shdr': subvalue = bytes(new_headers)
                rebuilt.extend(chunk(subtag, subvalue))
            value = bytes(rebuilt)
        output.extend(chunk(tag, value))
    Path(dest).write_bytes(chunk(b'RIFF', bytes(output)))
    print(f'Converted {len(headers) - 1} samples to {dest}', flush=True)

if __name__ == '__main__': convert(*sys.argv[1:])
