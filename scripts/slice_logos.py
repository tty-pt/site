#!/usr/bin/env python3
"""
slice_logos.py - Extract individual logos from composite logos.png and save to sub-repos.
"""
import os
import struct
import zlib

def decode_png(filename):
    with open(filename, 'rb') as f:
        data = f.read()

    assert data[:8] == b'\x89PNG\r\n\x1a\n', "Invalid PNG signature"
    pos = 8
    idat = []
    w, h = 0, 0
    while pos < len(data):
        length, ctype = struct.unpack('>I4s', data[pos:pos+8])
        pos += 8
        cdata = data[pos:pos+length]
        pos += length
        pos += 4 # skip crc
        if ctype == b'IHDR':
            w, h, bitd, colort, compm, filterm, interm = struct.unpack('>IIBBBBB', cdata)
            assert bitd == 8 and colort == 6, f"Expected 8-bit RGBA, got bitd={bitd}, colort={colort}"
        elif ctype == b'IDAT':
            idat.append(cdata)
        elif ctype == b'IEND':
            break

    raw = zlib.decompress(b''.join(idat))
    bpp = 4
    row_len = 1 + w * bpp
    out = bytearray(w * h * bpp)

    def paeth(a, b, c):
        p = a + b - c
        pa = abs(p - a)
        pb = abs(p - b)
        pc = abs(p - c)
        if pa <= pb and pa <= pc: return a
        elif pb <= pc: return b
        else: return c

    for y in range(h):
        filter_type = raw[y * row_len]
        scanline = raw[y * row_len + 1 : (y + 1) * row_len]
        prev_y = (y - 1) if y > 0 else -1
        for x in range(w * bpp):
            b = out[prev_y * w * bpp + x] if prev_y >= 0 else 0
            a = out[y * w * bpp + x - bpp] if x >= bpp else 0
            c = out[prev_y * w * bpp + x - bpp] if (prev_y >= 0 and x >= bpp) else 0
            val = scanline[x]
            if filter_type == 0: res = val
            elif filter_type == 1: res = (val + a) & 0xff
            elif filter_type == 2: res = (val + b) & 0xff
            elif filter_type == 3: res = (val + ((a + b) >> 1)) & 0xff
            elif filter_type == 4: res = (val + paeth(a, b, c)) & 0xff
            out[y * w * bpp + x] = res

    return w, h, out

def write_png(filename, width, height, rgba_data):
    def chunk(ctype, data):
        crc = zlib.crc32(ctype + data) & 0xffffffff
        return struct.pack('>I', len(data)) + ctype + data + struct.pack('>I', crc)

    header = b'\x89PNG\r\n\x1a\n'
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    raw = bytearray()
    for y in range(height):
        raw.append(0) # filter 0 (None)
        raw.extend(rgba_data[y * width * 4 : (y + 1) * width * 4])
    compressed = zlib.compress(bytes(raw), 9)
    os.makedirs(os.path.dirname(os.path.abspath(filename)), exist_ok=True)
    with open(filename, 'wb') as f:
        f.write(header)
        f.write(chunk(b'IHDR', ihdr))
        f.write(chunk(b'IDAT', compressed))
        f.write(chunk(b'IEND', b''))

def get_logo_id(x, y, alpha):
    if alpha <= 5:
        return None
    # Col 0:
    if 50 <= x <= 540:
        if y <= 370: return 'libcorm'
        elif y <= 695: return 'libislet'
        else: return 'libstoma'
    # Col 1:
    elif 550 <= x <= 980:
        if y <= 370: return 'axil'
        elif y <= 690 or (y <= 706 and x < 760): return 'libjoint'
        else: return 'bud'
    # Col 2:
    elif 1000 <= x <= 1450:
        if y <= 370 or (y <= 380 and x < 1215): return 'libxylem'
        elif y <= 703: return 'libsepal'
        else: return 'hyle'
    return None

TARGET_MAP = {
    'libcorm': 'external/libcorm/assets/logo.png',
    'axil': 'external/axil/assets/logo.png',
    'libxylem': 'external/libxylem/assets/logo.png',
    'libislet': 'external/libislet/assets/logo.png',
    'libjoint': 'external/libjoint/assets/logo.png',
    'libsepal': 'external/libsepal/assets/logo.png',
    'libstoma': 'external/libstoma/assets/logo.png',
    'bud': 'external/libbud/assets/logo.png',
    'hyle': 'external/libhyle/assets/logo.png',
}

def main():
    src_file = 'logos.png'
    print(f"Reading {src_file}...")
    w, h, rgba = decode_png(src_file)

    # 1. Determine bounding boxes for each logo
    boxes = {}
    for y in range(h):
        for x in range(w):
            a = rgba[(y * w + x) * 4 + 3]
            lid = get_logo_id(x, y, a)
            if lid:
                if lid not in boxes:
                    boxes[lid] = [x, x, y, y, 0]
                else:
                    b = boxes[lid]
                    if x < b[0]: b[0] = x
                    if x > b[1]: b[1] = x
                    if y < b[2]: b[2] = y
                    if y > b[3]: b[3] = y
                    b[4] += 1

    pad = 16
    print("\nExtracting and saving logos:")
    for lid, target_path in TARGET_MAP.items():
        min_x, max_x, min_y, max_y, count = boxes[lid]
        crop_w = (max_x - min_x + 1) + 2 * pad
        crop_h = (max_y - min_y + 1) + 2 * pad
        out_rgba = bytearray(crop_w * crop_h * 4)

        for src_y in range(min_y, max_y + 1):
            dst_y = (src_y - min_y) + pad
            for src_x in range(min_x, max_x + 1):
                dst_x = (src_x - min_x) + pad
                src_a = rgba[(src_y * w + src_x) * 4 + 3]
                if get_logo_id(src_x, src_y, src_a) == lid:
                    for c in range(4):
                        out_rgba[(dst_y * crop_w + dst_x) * 4 + c] = rgba[(src_y * w + src_x) * 4 + c]

        write_png(target_path, crop_w, crop_h, out_rgba)
        file_size = os.path.getsize(target_path)
        print(f"  [{lid:9s}] -> {target_path} ({crop_w}x{crop_h}, {file_size:,} bytes, {count} active pixels)")

    print("\nAll 9 logos extracted successfully!")

if __name__ == '__main__':
    main()
