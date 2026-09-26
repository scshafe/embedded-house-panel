#!/usr/bin/env python3
"""Deterministic PNG decoding, comparison and contact sheets for panel renders.

Supports the 8-bit RGB/RGBA PNG contract emitted by LVGL/libpng and QEMU.
No platform font, screen, image-library version or display server is involved.
"""
from __future__ import annotations
import binascii
import html
import os
import struct
import zlib
from pathlib import Path


def read_png(path: Path) -> tuple[int, int, bytes]:
    """Read a bounded RGB/RGBA PNG as tightly packed RGBA; reject other formats."""
    data = path.read_bytes()
    if len(data) > 16 * 1024 * 1024 or data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('PANEL_SCREENSHOT.PNG_INVALID')
    offset, compressed, header = 8, bytearray(), None
    while offset + 12 <= len(data):
        size = struct.unpack('>I', data[offset:offset+4])[0]
        kind = data[offset+4:offset+8]
        payload = data[offset+8:offset+8+size]
        end = offset+12+size
        if end > len(data) or binascii.crc32(kind+payload) & 0xffffffff != struct.unpack('>I', data[end-4:end])[0]:
            raise ValueError('PANEL_SCREENSHOT.PNG_CRC_INVALID')
        if kind == b'IHDR':
            header = struct.unpack('>IIBBBBB', payload)
        elif kind == b'IDAT':
            compressed.extend(payload)
        elif kind == b'IEND':
            break
        offset = end
    if header is None:
        raise ValueError('PANEL_SCREENSHOT.PNG_HEADER_MISSING')
    width, height, depth, color, compression, filtering, interlace = header
    if not (0 < width <= 8192 and 0 < height <= 8192 and depth == 8 and color in (2, 6)
            and compression == filtering == interlace == 0):
        raise ValueError('PANEL_SCREENSHOT.PNG_FORMAT_UNSUPPORTED')
    channels = 3 if color == 2 else 4
    stride = width*channels
    expected = (stride+1)*height
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed, expected+1)
    if len(raw) != expected or not decoder.eof or decoder.unconsumed_tail:
        raise ValueError('PANEL_SCREENSHOT.PNG_SIZE_INVALID')
    output = bytearray()
    prior = bytearray(stride)
    for row_index in range(height):
        index = row_index*(stride+1)
        filter_type = raw[index]
        row = bytearray(raw[index+1:index+1+stride])
        if filter_type not in range(5):
            raise ValueError('PANEL_SCREENSHOT.PNG_FILTER_INVALID')
        for x in range(stride):
            left = row[x-channels] if x >= channels else 0
            up = prior[x]
            upper_left = prior[x-channels] if x >= channels else 0
            if filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = up
            elif filter_type == 3:
                predictor = (left+up)//2
            elif filter_type == 4:
                p = left+up-upper_left
                distances = [abs(p-left), abs(p-up), abs(p-upper_left)]
                predictor = (left, up, upper_left)[distances.index(min(distances))]
            else:
                predictor = 0
            row[x] = (row[x]+predictor) & 255
        if channels == 4:
            output.extend(row)
        else:
            for x in range(0, stride, 3):
                output.extend(row[x:x+3]); output.append(255)
        prior = row
    return width, height, bytes(output)


def write_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    """Write fixed-filter RGBA bytes; exact dimensions and buffer size are required."""
    if not 0 < width <= 8192 or not 0 < height <= 8192 or len(rgba) != width*height*4:
        raise ValueError('PANEL_SCREENSHOT.BUFFER_SIZE_INVALID')
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',binascii.crc32(kind+payload)&0xffffffff)
    raw = b''.join(b'\0'+rgba[y*width*4:(y+1)*width*4] for y in range(height))
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))
                     +chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b''))


def compare_png(expected_path: Path, actual_path: Path, diff_path: Path,
                channel_tolerance: int = 0, maximum_changed_pixels: int = 0) -> dict:
    """Compare explicit absolute per-channel and pixel-count budgets, writing failure diff."""
    if not 0 <= channel_tolerance <= 255 or maximum_changed_pixels < 0:
        raise ValueError("PANEL_SCREENSHOT.TOLERANCE_INVALID")
    ew,eh,expected = read_png(expected_path)
    aw,ah,actual = read_png(actual_path)
    if (ew,eh)!=(aw,ah):
        write_png(diff_path,aw,ah,bytes((255,30,100,255))*aw*ah)
        return {'passed':False,'error_code':'PANEL_SCREENSHOT.DIMENSIONS_MISMATCH',
                'expected_dimensions':[ew,eh],'actual_dimensions':[aw,ah]}
    changed, maximum = 0, 0
    diff = bytearray(len(actual))
    for i in range(0,len(actual),4):
        delta = [abs(actual[i+c]-expected[i+c]) for c in range(4)]
        maximum = max(maximum,*delta)
        mismatch = max(delta)>channel_tolerance
        changed += int(mismatch)
        diff[i:i+4] = bytes((255,30,100,255) if mismatch else (actual[i]//5,actual[i+1]//5,actual[i+2]//5,255))
    passed = changed<=maximum_changed_pixels
    if not passed:
        write_png(diff_path,aw,ah,bytes(diff))
    elif diff_path.exists():
        diff_path.unlink()
    return {'passed':passed,'changed_pixels':changed,'maximum_channel_delta':maximum,
            'channel_tolerance':channel_tolerance,'maximum_changed_pixels':maximum_changed_pixels}


def contact_sheet(paths: list[Path], output: Path, columns: int=4) -> None:
    """Create ordered thumbnails and a labelled HTML companion for review."""
    if not paths:
        raise ValueError('PANEL_SCREENSHOT.EMPTY_CONTACT_SHEET')
    if not 1<=columns<=20:
        raise ValueError('PANEL_SCREENSHOT.COLUMNS_INVALID')
    gap=12
    rows=(len(paths)+columns-1)//columns
    # Keep growing scenario reviews inside the bounded PNG reader/writer contract.
    tile_h=min(225,(8192-gap)//rows-gap)
    if tile_h<1:raise ValueError('PANEL_SCREENSHOT.CONTACT_SHEET_TOO_LARGE')
    tile_w=tile_h*1024//600
    width,height=columns*(tile_w+gap)+gap,rows*(tile_h+gap)+gap
    canvas=bytearray(bytes((20,24,31,255))*width*height)
    for index,path in enumerate(paths):
        sw,sh,pixels=read_png(path)
        ox=gap+(index%columns)*(tile_w+gap); oy=gap+(index//columns)*(tile_h+gap)
        for y in range(tile_h):
            sy=y*sh//tile_h
            for x in range(tile_w):
                sx=x*sw//tile_w
                target=((oy+y)*width+ox+x)*4; source=(sy*sw+sx)*4
                canvas[target:target+4]=pixels[source:source+4]
    write_png(output,width,height,bytes(canvas))
    figures=[]
    for path in paths:
        source=html.escape(os.path.relpath(path,output.parent),quote=True)
        figures.append('<figure><a href="'+source+'"><img src="'+source+'"></a><figcaption>'
                       +html.escape(path.stem)+'</figcaption></figure>')
    output.with_suffix('.html').write_text(
        '<!doctype html><meta charset="utf-8"><title>Panel scenario review</title>'
        '<style>body{background:#14181f;color:#eee;font:16px system-ui;margin:24px}'
        'main{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:20px}'
        'img{width:100%}figure{margin:0}figcaption{padding:8px 0}</style>'
        '<h1>Panel scenarios</h1><p>Simulation only — no hardware evidence. '
        'Select an image for its full 1024 × 600 capture.</p><main>'+''.join(figures)+'</main>')
