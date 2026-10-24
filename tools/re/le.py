# Minimal static RE helpers over TESV.exe 1.9.32.0.8 (x86): pip install pefile capstone
# Usage: TESV_EXE=/path/TESV.exe python -c "from le import *; dis(0x0083E4C0, 40)"
import struct, sys, re
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

# Point TESV_EXE at your own copy of TESV.exe 1.9.32.0.8. It is never committed.
import os
EXE = os.environ.get('TESV_EXE', 'TESV.exe')
pe = pefile.PE(EXE, fast_load=True)
BASE = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = False
secs = [(s.Name.rstrip(b'\0').decode(), BASE + s.VirtualAddress, s.Misc_VirtualSize) for s in pe.sections]
TEXT = [s for s in secs if s[0] == '.text'][0]

def rd(va, n): return img[va - BASE: va - BASE + n]
def u32(va): return struct.unpack('<I', rd(va, 4))[0]
def f32(va): return struct.unpack('<f', rd(va, 4))[0]
def cstr(va, n=200):
    b = rd(va, n); return b.split(b'\0')[0].decode('latin1')

def dis(va, n=40, stop_ret=True, out=True):
    res = []
    for i in md.disasm(rd(va, n * 8), va):
        res.append(i)
        if out: print('%08X  %-28s %s %s' % (i.address, i.bytes.hex(), i.mnemonic, i.op_str))
        if len(res) >= n or (stop_ret and i.mnemonic in ('ret', 'retn')): break
    return res

def find_bytes(pat, start=None, end=None):
    start = start or BASE; end = end or BASE + len(img)
    data = img[start - BASE:end - BASE]
    out = []; i = data.find(pat)
    while i >= 0:
        out.append(start + i); i = data.find(pat, i + 1)
    return out

def find_str(s): return find_bytes(s.encode() + b'\0')

def xref_imm(va):
    """addresses in .text holding va as a 4-byte immediate"""
    return find_bytes(struct.pack('<I', va), TEXT[1], TEXT[1] + TEXT[2])

def xref_data(va):
    return [a for a in find_bytes(struct.pack('<I', va)) if not (TEXT[1] <= a < TEXT[1] + TEXT[2])]

_calls = None
def calls_to(target):
    """E8 rel32 call sites in .text"""
    t0, n = TEXT[1], TEXT[2]
    data = img[t0 - BASE:t0 - BASE + n]
    out = []
    for m in re.finditer(b'[\xe8\xe9]', data):
        i = m.start()
        if i + 5 > n: break
        rel = struct.unpack('<i', data[i+1:i+5])[0]
        if (t0 + i + 5 + rel) & 0xffffffff == target: out.append(t0 + i)
    return out

def func_start(va, maxback=0x4000):
    """walk back to CC/90 padding before a likely prologue"""
    a = va
    while a > va - maxback:
        if rd(a - 1, 1) in (b'\xcc', b'\x90', b'\xc3') and rd(a - 2, 1) in (b'\xcc', b'\x90', b'\xc3', b'\x00'):
            return a
        a -= 1
    return None

def rtti_vtables(name):
    """vtables whose COL points at the type descriptor named name (e.g. '.?AVButtonEvent@@')"""
    out = []
    for td_name in find_bytes(name.encode() + b'\0'):
        td = td_name - 8
        for col_ref in find_bytes(struct.pack('<I', td)):
            col = col_ref - 0xC
            if u32(col) != 0: 
                off = u32(col + 4)
            else: off = 0
            for vt_ref in find_bytes(struct.pack('<I', col)):
                out.append((vt_ref + 4, u32(col + 4)))
    return out

def rtti_vtables(name):
    """vtables whose COL points at the type descriptor named name (e.g. '.?AVButtonEvent@@')"""
    out = []
    for td_name in find_bytes(name.encode() + b'\0'):
        td = td_name - 8
        for col_ref in find_bytes(struct.pack('<I', td)):
            col = col_ref - 0xC
            for vt_ref in find_bytes(struct.pack('<I', col)):
                out.append((vt_ref + 4, u32(col + 4)))
    return out

_mdskip = Cs(CS_ARCH_X86, CS_MODE_32); _mdskip.skipdata = True
_textcache = None
def text_insns():
    """(address, mnemonic, op_str) for the whole .text, cached. A plain linear
    capstone sweep stops at the first data byte; skipdata walks past it."""
    global _textcache
    if _textcache is None:
        t0, n = TEXT[1], TEXT[2]
        _textcache = [(a, m, o) for (a, s, m, o) in _mdskip.disasm_lite(rd(t0, n), t0)]
    return _textcache
