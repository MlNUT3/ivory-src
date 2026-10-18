#!/usr/bin/env python3
"""
Ivory Raycast Auto-Updater (IDA-free)
- Fetches instance offsets from https://offsets.imtheo.lol/offsets.json (version check)
- Live-scans a running RobloxPlayerBeta.exe for Raycast descriptor/slot (same 3-stage logic as raycast.cpp:get_raycast_slot)
- Computes new RVAs: RaycastBoundDesc + RaycastBoundFn (0x80)
- Auto-patches Source/source/sdk/offsets/offsets.h and Source/source/sdk/offsets/rva.h

Usage:
  python tools/raycast_auto_updater.py              # fetch + live scan if Roblox running, else file scan
  python tools/raycast_auto_updater.py --patch      # also patch headers
  python tools/raycast_auto_updater.py --exe "C:/path/RobloxPlayerBeta.exe"  # offline file scan

No IDA, no pefile required (pefile optional for offline). Live scan needs only stdlib + ctypes.
"""
import argparse, ctypes, ctypes.wintypes, json, os, re, struct, sys, urllib.request, pathlib
from pathlib import Path

# --- Config ---
THEO_URL = "https://offsets.imtheo.lol/offsets.json"
BOUND_FN_OFF = 0x80  # Offsets::WorldRoot::RaycastBoundFn (stable across versions, verified in rva.h:213)
PROJECT_ROOT = Path(__file__).resolve().parents[1]  # .../Source
OFFSETS_H = PROJECT_ROOT / "source" / "sdk" / "offsets" / "offsets.h"
RVA_H = PROJECT_ROOT / "source" / "sdk" / "offsets" / "rva.h"

# --- WinAPI helpers (live scan) ---
kernel32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
MEM_COMMIT = 0x1000
PAGE_GUARD = 0x100
PAGE_NOACCESS = 0x01
PAGE_EXECUTE = 0x10
PAGE_EXECUTE_READ = 0x20
PAGE_EXECUTE_READWRITE = 0x40
PAGE_EXECUTE_WRITECOPY = 0x80

def is_executable(protect): return (protect & 0xFF) in (PAGE_EXECUTE, PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_WRITECOPY)

def fetch_theo():
    try:
        req = urllib.request.Request(THEO_URL, headers={"User-Agent": "Mozilla/5.0", "Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=10) as r:
            data = json.loads(r.read().decode())
            print(f"[theo] version={data.get('Roblox Version')} dumped={data.get('Dumped At')} total={data.get('Total Offsets')}")
            return data
    except Exception as e:
        print(f"[theo] fetch failed: {e}")
        return None

def find_roblox_processes():
    try:
        import subprocess, csv
        out = subprocess.check_output('tasklist /FI "IMAGENAME eq RobloxPlayerBeta.exe" /FO CSV /NH', shell=True).decode(errors='ignore')
        pids = []
        for line in out.splitlines():
            if 'RobloxPlayerBeta' in line:
                parts = line.split('","')
                if len(parts) >= 2:
                    try: pids.append(int(parts[1].strip('"')))
                    except: pass
        return pids
    except: return []

def get_module_base(pid, target="RobloxPlayerBeta.exe"):
    # Toolhelp snapshot is more reliable for 64-bit
    TH32CS_SNAPMODULE = 0x00000008
    TH32CS_SNAPMODULE32 = 0x00000010
    class MODULEENTRY32W(ctypes.Structure):
        _fields_=[("dwSize",ctypes.wintypes.DWORD),("th32ModuleID",ctypes.wintypes.DWORD),("th32ProcessID",ctypes.wintypes.DWORD),
                  ("GlblcntUsage",ctypes.wintypes.DWORD),("ProccntUsage",ctypes.wintypes.DWORD),("modBaseAddr",ctypes.c_void_p),
                  ("modBaseSize",ctypes.wintypes.DWORD),("hModule",ctypes.c_void_p),("szModule",ctypes.wintypes.WCHAR*256),("szExePath",ctypes.wintypes.WCHAR*260)]
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32, pid)
    if snap != ctypes.c_void_p(-1).value:
        me = MODULEENTRY32W(); me.dwSize = ctypes.sizeof(MODULEENTRY32W)
        if kernel32.Module32FirstW(snap, ctypes.byref(me)):
            while True:
                if me.szModule.lower() == target.lower():
                    kernel32.CloseHandle(snap)
                    return ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value, me.modBaseSize
                if not kernel32.Module32NextW(snap, ctypes.byref(me)): break
        kernel32.CloseHandle(snap)
    # fallback psapi
    h = kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not h: return 0,0
    mods = (ctypes.c_void_p * 1024)()
    needed = ctypes.wintypes.DWORD()
    if not psapi.EnumProcessModules(h, mods, ctypes.sizeof(mods), ctypes.byref(needed)):
        kernel32.CloseHandle(h); return 0,0
    cnt = needed.value // ctypes.sizeof(ctypes.c_void_p)
    buf = ctypes.create_unicode_buffer(512)
    for i in range(cnt):
        if not mods[i]: continue
        if psapi.GetModuleBaseNameW(h, ctypes.c_void_p(mods[i]), buf, 512):
            if buf.value.lower() == target.lower():
                base = mods[i]
                class MODULEINFO(ctypes.Structure): _fields_=[("lpBaseOfDll",ctypes.c_void_p),("SizeOfImage",ctypes.wintypes.DWORD),("EntryPoint",ctypes.c_void_p)]
                mic = MODULEINFO()
                if psapi.GetModuleInformation(h, ctypes.c_void_p(mods[i]), ctypes.byref(mic), ctypes.sizeof(mic)):
                    size = mic.SizeOfImage
                else: size = 0x8000000
                kernel32.CloseHandle(h)
                return base, size
    kernel32.CloseHandle(h)
    return 0,0

def list_modules(pid):
    TH32CS_SNAPMODULE = 0x00000008
    TH32CS_SNAPMODULE32 = 0x00000010
    class MODULEENTRY32W(ctypes.Structure):
        _fields_=[("dwSize",ctypes.wintypes.DWORD),("th32ModuleID",ctypes.wintypes.DWORD),("th32ProcessID",ctypes.wintypes.DWORD),
                  ("GlblcntUsage",ctypes.wintypes.DWORD),("ProccntUsage",ctypes.wintypes.DWORD),("modBaseAddr",ctypes.c_void_p),
                  ("modBaseSize",ctypes.wintypes.DWORD),("hModule",ctypes.c_void_p),("szModule",ctypes.wintypes.WCHAR*256),("szExePath",ctypes.wintypes.WCHAR*260)]
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32, pid)
    mods=[]
    if snap != ctypes.c_void_p(-1).value:
        me = MODULEENTRY32W(); me.dwSize = ctypes.sizeof(MODULEENTRY32W)
        if kernel32.Module32FirstW(snap, ctypes.byref(me)):
            while True:
                mods.append((me.szModule, ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value, me.modBaseSize, me.szExePath))
                if not kernel32.Module32NextW(snap, ctypes.byref(me)): break
        kernel32.CloseHandle(snap)
    return mods

def r_mem(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    br = ctypes.c_size_t()
    if not kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(br)) or br.value != size:
        return None
    return buf.raw

def query_protect(h, addr):
    class MBI(ctypes.Structure):
        _fields_=[("BaseAddress",ctypes.c_void_p),("AllocationBase",ctypes.c_void_p),("AllocationProtect",ctypes.wintypes.DWORD),
                  ("PartitionId",ctypes.wintypes.WORD),("RegionSize",ctypes.c_size_t),("State",ctypes.wintypes.DWORD),
                  ("Protect",ctypes.wintypes.DWORD),("Type",ctypes.wintypes.DWORD)]
    mbi = MBI()
    if not kernel32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)): return 0
    return mbi.Protect

def live_scan_raycast_slot(pid, target_mod="RobloxPlayerBeta.exe"):
    mods = list_modules(pid)
    print(f"[live] pid {pid} modules: " + ", ".join(f"{m[0]}@0x{m[1]:X}" for m in mods[:6]))
    base, img_sz = get_module_base(pid, target_mod)
    if not base:
        print(f"[live] pid {pid} no {target_mod}"); return None
    print(f"[live] scanning {target_mod} base=0x{base:X} img_sz=0x{img_sz:X} pid={pid}")
    # also print exe path for sanity
    for m in mods:
        if m[0].lower()==target_mod.lower(): print(f"[live] {target_mod} path {m[3]}")
    h = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        print(f"[live] OpenProcess failed {ctypes.get_last_error()}"); return None

    def addr_ok(a): return 0x10000 <= a < 0x7FFFFFFFFFFF

    # --- Stage 1: try Scan for Reflect-like name pointer (we don't have Reflect::Name impl, so skip to Stage 2 brute force which is sufficient) ---
    # Stage 2: brute force string scan
    # Walk VMA regions
    MEMBERS = []
    addr = base
    string_addrs = []
    target = b"Raycast\x00"  # 8 bytes
    regions = []
    cur = base
    while cur < base + img_sz:
        class MBI(ctypes.Structure):
            _fields_=[("BaseAddress",ctypes.c_void_p),("AllocationBase",ctypes.c_void_p),("AllocationProtect",ctypes.wintypes.DWORD),
                      ("PartitionId",ctypes.wintypes.WORD),("RegionSize",ctypes.c_size_t),("State",ctypes.wintypes.DWORD),
                      ("Protect",ctypes.wintypes.DWORD),("Type",ctypes.wintypes.DWORD)]
        mbi = MBI()
        if not kernel32.VirtualQueryEx(h, ctypes.c_void_p(cur), ctypes.byref(mbi), ctypes.sizeof(mbi)): break
        rb = ctypes.cast(mbi.BaseAddress, ctypes.c_void_p).value or cur
        rs = mbi.RegionSize
        nxt = rb + rs
        if nxt <= cur: break
        if mbi.State == MEM_COMMIT and not (mbi.Protect & (PAGE_GUARD|PAGE_NOACCESS)) and not is_executable(mbi.Protect):
            s = max(rb, base)
            e = min(nxt, base+img_sz)
            if e > s:
                data = r_mem(h, s, e-s)
                if data:
                    # find strings
                    off = 0
                    while True:
                        idx = data.find(target, off)
                        if idx == -1: break
                        string_addrs.append(s+idx)
                        off = idx+1
                regions.append((s,e))
        cur = nxt

    print(f"[live] Stage2 found {len(string_addrs)} 'Raycast' strings")
    if not string_addrs:
        kernel32.CloseHandle(h); return None

    # Collect name candidates: for each str, scan all regions for pointers to it
    name_cands = set(string_addrs)
    for s,e in regions:
        data = r_mem(h, s, e-s)
        if not data or len(data) < 8: continue
        for i in range(0, len(data)-8, 8):
            v = struct.unpack_from("<Q", data, i)[0]
            if v in string_addrs:
                loc = s+i
                name_cands.add(loc)
                for back in (8,16,24,32,48,56,64):
                    if loc >= base+back: name_cands.add(loc-back)
                # also the pointer value itself may be the name struct
                # add nearby structs that point to it
    print(f"[live] {len(name_cands)} name/descriptor candidates")
    # Dump first string's neighborhood
    if string_addrs:
        sa = string_addrs[0]
        print(f"[dump] first Raycast string at 0x{sa:X}")
        # read 64 bytes before to find name object header
        for cand in list(name_cands)[:4]:
            print(f"  cand 0x{cand:X}")
            d = r_mem(h, cand-0x20, 0x60)
            if d:
                hexd = " ".join(f"{b:02X}" for b in d[:48])
                print(f"    -0x20..: {hexd}")
                # try interpret as pointers
                ptrs = struct.unpack("<6Q", d[0x20:0x20+48]) if len(d)>=0x50 else ()
                print(f"    ptrs @cand: {[hex(p) for p in ptrs]}")
    # Diagnostic: try known RVAs
    for test_rva in (0x8089F20, 0x81E7150, 0x81E77F0, 0x81E78A0):
        desc = base + test_rva
        vt_data = r_mem(h, desc, 8)
        if vt_data:
            vt = struct.unpack("<Q", vt_data)[0]
            slot_data = r_mem(h, desc+BOUND_FN_OFF, 8)
            fn = struct.unpack("<Q", slot_data)[0] if slot_data else 0
            print(f"[diag] rva 0x{test_rva:X} desc=0x{desc:X} vt=0x{vt:X} slot_fn=0x{fn:X} vt_in_mod={base <= vt < base+img_sz} fn_exec={is_executable(query_protect(h, fn)) if fn else False}")

    # Find descriptor -> slot (verbose for first 20)
    attempts=0
    for cand in list(name_cands)[:20]:
        for s,e in regions:
            if attempts>200: break
            data = r_mem(h, s, e-s)
            if not data: continue
            for i in range(0, len(data)-8, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if v == cand:
                    ptr_loc = s+i
                    for off_back in (0x08,0x10,0x18,0x00):
                        if ptr_loc < base+off_back: continue
                        desc = ptr_loc - off_back
                        vt_data = r_mem(h, desc, 8)
                        if not vt_data: continue
                        vt = struct.unpack("<Q", vt_data)[0]
                        vt_in = base <= vt < base+img_sz
                        #print(f"[try] cand 0x{cand:X} ptr_loc 0x{ptr_loc:X} desc 0x{desc:X} vt 0x{vt:X} vt_in={vt_in}")
                        if not vt_in: 
                            # try without vt check for debug
                            pass
                        for fn_off in (BOUND_FN_OFF, 0x78,0x88,0x70,0x90,0x80,0x68,0x98):
                            slot = desc + fn_off
                            fn_data = r_mem(h, slot, 8)
                            if not fn_data: continue
                            fn = struct.unpack("<Q", fn_data)[0]
                            if fn==0: continue
                            ok = 0x10000 <= fn < 0x7FFFFFFFFFFF
                            inmod = base <= fn < base+img_sz
                            prot = query_protect(h, fn) if ok else 0
                            exec_ok = is_executable(prot)
                            if attempts<10:
                                print(f"[try] desc 0x{desc:X} off 0x{fn_off:X} slot 0x{slot:X} fn 0x{fn:X} ok={ok} inmod={inmod} exec={exec_ok} prot=0x{prot:X}")
                            attempts+=1
                            if ok and exec_ok:
                                print(f"[live] SUCCESS slot=0x{slot:X} desc=0x{desc:X} fn=0x{fn:X} rva_desc=0x{desc-base:X}")
                                kernel32.CloseHandle(h)
                                return {"slot":slot,"desc":desc,"fn":fn,"base":base,"rva_desc":desc-base,"slot_rva":slot-base,"bound_fn_off":fn_off}
                    # limit
                    if attempts>200: break
            if attempts>200: break
        if attempts>200: break
    # Exhaustive descriptor scan: look for any descriptor with exec fn and Raycast name nearby (no vt filter)
    print("[live] exhaustive scan for descriptor with exec fn (relaxed)...")
    str_set = set(string_addrs)
    name_set = set(name_cands)
    # Dump first few has_ray descriptors with vt validation
    found_dbg=0
    for s,e in regions:
        if found_dbg>=5: break
        data = r_mem(h, s, e-s)
        if not data: continue
        for i in range(0, len(data)-0x100, 8):
            addr = s+i
            has=False
            has_off=-1
            for off in range(0, 0x40, 8):
                p = struct.unpack_from("<Q", data, i+off)[0]
                if p in str_set or p in name_set:
                    has=True; has_off=off; break
            if not has: continue
            vt = struct.unpack_from("<Q", data, i)[0]
            vt_ok=False
            if 0x10000 <= vt < 0x7FFFFFFFFFFF:
                # check vt points to exec table (vtable)
                vt_data = r_mem(h, vt, 16)
                if vt_data:
                    vt_fn = struct.unpack("<Q", vt_data[:8])[0]
                    if is_executable(query_protect(h, vt_fn)):
                        vt_ok=True
            if not vt_ok and found_dbg>=2: continue
            print(f"[dbg] has_ray desc 0x{addr:X} has_off 0x{has_off:X} vt 0x{vt:X} vt_ok={vt_ok}")
            for fn_off in range(0,0x110,8):
                fn = struct.unpack_from("<Q", data, i+fn_off)[0]
                if 0x10000 <= fn < 0x7FFFFFFFFFFF:
                    prot = query_protect(h, fn)
                    if is_executable(prot):
                        print(f"  -> exec at +0x{fn_off:X} fn 0x{fn:X} prot 0x{prot:X}")
            qs = " ".join(f"{struct.unpack_from('<Q', data, i+o)[0]:016X}" for o in range(0,0x100,8))
            print(f"  qwords 0..0xF8: {qs[:320]}")
            found_dbg+=1
            if found_dbg>=5: break
        if found_dbg>=5: break
    for s,e in regions:
        data = r_mem(h, s, e-s)
        if not data: continue
        for i in range(0, len(data)-0xB0, 8):
            addr = s+i
            # check nearby pointers for Raycast string without vt
            has_ray = False
            for off in range(0, 0x40, 8):
                if i+off+8 > len(data): break
                p = struct.unpack_from("<Q", data, i+off)[0]
                if p in str_set: has_ray=True; break
                if p in name_cands: has_ray=True; break
            if not has_ray: continue
            for fn_off in (BOUND_FN_OFF, 0x78,0x88,0x70,0x90,0x80,0x68,0x98,0x60,0xA0,0xB0,0x58,0x50):
                if i+fn_off+8 > len(data): continue
                fn = struct.unpack_from("<Q", data, i+fn_off)[0]
                if not (0x10000 <= fn < 0x7FFFFFFFFFFF): continue
                prot = query_protect(h, fn)
                if not is_executable(prot): continue
                vt = struct.unpack_from("<Q", data, i)[0]
                print(f"[live] EXHAUSTIVE SUCCESS desc=0x{addr:X} fn_off=0x{fn_off:X} slot=0x{addr+fn_off:X} fn=0x{fn:X} vt=0x{vt:X} rva_desc=0x{addr-base:X}")
                kernel32.CloseHandle(h)
                return {"slot":addr+fn_off,"desc":addr,"fn":fn,"base":base,"rva_desc":addr-base,"slot_rva":addr+fn_off-base,"bound_fn_off":fn_off}
    # full scan without verbose (original 2-hop with exec allowed outside module)
    for cand in name_cands:
        for s,e in regions:
            data = r_mem(h, s, e-s)
            if not data: continue
            for i in range(0, len(data)-8, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if v == cand:
                    ptr_loc = s+i
                    for off_back in (0x08,0x10,0x18,0x00):
                        if ptr_loc < base+off_back: continue
                        desc = ptr_loc - off_back
                        vt_data = r_mem(h, desc, 8)
                        if not vt_data: continue
                        vt = struct.unpack("<Q", vt_data)[0]
                        if not (base <= vt < base+img_sz): continue
                        for fn_off in (BOUND_FN_OFF, 0x78,0x88,0x70,0x90,0x80,0x68,0x98):
                            slot = desc + fn_off
                            fn_data = r_mem(h, slot, 8)
                            if not fn_data: continue
                            fn = struct.unpack("<Q", fn_data)[0]
                            if addr_ok(fn) and is_executable(query_protect(h, fn)):
                                print(f"[live] SUCCESS slot=0x{slot:X} desc=0x{desc:X} fn=0x{fn:X} rva_desc=0x{desc-base:X}")
                                kernel32.CloseHandle(h)
                                return {"slot":slot,"desc":desc,"fn":fn,"base":base,"rva_desc":desc-base,"slot_rva":slot-base,"bound_fn_off":fn_off}
    # Stage 3: static fallback uses known RVA - try it anyway
    for rva in (0x8089F20,):
        slot = base + rva + BOUND_FN_OFF
        d = r_mem(h, slot, 8)
        if d:
            fn = struct.unpack("<Q", d)[0]
            if addr_ok(fn):
                print(f"[live] fallback static slot=0x{slot:X} fn=0x{fn:X}")
                kernel32.CloseHandle(h)
                return {"slot":slot,"desc":base+rva,"fn":fn,"base":base,"rva_desc":rva,"slot_rva":rva+BOUND_FN_OFF,"bound_fn_off":BOUND_FN_OFF}
    kernel32.CloseHandle(h)
    print("[live] not found")
    return None

def offline_scan_file(exe_path):
    """Offline PE scan using pefile (static, no Hyperion runtime obfuscation). Finds Raycast descriptor RVA."""
    p = Path(exe_path)
    if not p.exists():
        print(f"[file] not found {p}"); return None
    try:
        import pefile
    except ImportError:
        print("[file] pefile not installed, pip install pefile")
        return None
    try:
        pe = pefile.PE(str(p), fast_load=False)
    except Exception as e:
        print(f"[file] pe load failed {e}"); return None
    image_base = pe.OPTIONAL_HEADER.ImageBase
    print(f"[file] {p} ImageBase=0x{image_base:X} ImageSize=0x{pe.OPTIONAL_HEADER.SizeOfImage:X}")
    # collect sections
    secs = []
    for s in pe.sections:
        name = s.Name.decode(errors='ignore').strip('\x00')
        secs.append((name, s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData))
        print(f"  sec {name:8} RVA 0x{s.VirtualAddress:08X} VSZ 0x{s.Misc_VirtualSize:08X} RAW 0x{s.PointerToRawData:08X}")
    # find strings
    raw = p.read_bytes()
    target = b"Raycast\x00"
    str_rvas = []
    for m in re.finditer(re.escape(target), raw):
        file_off = m.start()
        # convert file_off to RVA
        rva = None
        for name, va, vsz, praw, sraw in secs:
            if praw <= file_off < praw+sraw:
                rva = va + (file_off - praw)
                break
        if rva is not None:
            str_rvas.append((rva, file_off, image_base+rva))
            print(f"[file] string 'Raycast' at fileoff 0x{file_off:X} RVA 0x{rva:X} VA 0x{image_base+rva:X}")
    if not str_rvas:
        print("[file] no strings"); return None
    # build map of RVA->file_off for quick lookup, and search for pointers
    # For each string RVA, search all sections for 8-byte values that equal VA or RVA
    # Use raw bytes search
    str_vas = set(va for _,_,va in str_rvas)
    str_rva_set = set(rva for rva,_,_ in str_rvas)
    candidates = set()
    # scan .rdata, .data
    for name, va, vsz, praw, sraw in secs:
        if name not in (".rdata", ".data", ".pdata", ".text"):
            continue
        sec_raw = raw[praw:praw+sraw]
        for i in range(0, len(sec_raw)-8, 1):  # 1-byte step to catch unaligned? but use 8 for speed first
            if i % 8 != 0: continue
            q = struct.unpack_from("<Q", sec_raw, i)[0]
            if q in str_vas or q in str_rva_set or (q - image_base) in str_rva_set:
                # found pointer to string
                ptr_rva = va + i
                ptr_va = image_base + ptr_rva
                # name object candidate is ptr_rva - back
                for back in (0,8,16,24,32):
                    cand_rva = ptr_rva - back
                    if cand_rva >= va and cand_rva < va+vsz:
                        candidates.add(cand_rva)
        if len(candidates) > 200: break
    print(f"[file] {len(candidates)} name candidates from pointer scan")
    # second hop: find descriptors pointing to candidates
    cand_set = set(image_base+c for c in candidates) | str_vas
    cand_rva_set = set(candidates) | str_rva_set
    desc_cands = set()
    for name, va, vsz, praw, sraw in secs:
        if name not in (".rdata", ".data"): continue
        sec_raw = raw[praw:praw+sraw]
        for i in range(0, len(sec_raw)-8, 8):
            q = struct.unpack_from("<Q", sec_raw, i)[0]
            # check if q points to a candidate (VA or RVA)
            hit=False
            if q in cand_set: hit=True
            elif q in cand_rva_set: hit=True
            elif (q - image_base) in cand_rva_set: hit=True
            if hit:
                ptr_rva = va + i
                for back in (0x08,0x10,0x18,0x00):
                    if ptr_rva < back: continue
                    d_rva = ptr_rva - back
                    # check d_rva in same section or nearby
                    desc_cands.add(d_rva)
    print(f"[file] {len(desc_cands)} descriptor candidates")
    # validate: descriptor should have vtable at +0 and exec fn at +0x80 (+/-)
    text_rva = next((va for n,va,_,_,_ in secs if n==".text"), 0)
    text_sz = next((vsz for n,_,vsz,_,_ in secs if n==".text"), 0)
    print(f"[file] .text RVA 0x{text_rva:X} size 0x{text_sz:X}")
    for d_rva in sorted(desc_cands):
        sec = next((s for s in secs if s[1] <= d_rva < s[1]+s[2]), None)
        if not sec: continue
        _, va, vsz, praw, sraw = sec
        off = d_rva - va
        if off+0x120 > sraw: continue
        # dump qwords
        qs = [struct.unpack_from("<Q", raw, praw+off+o)[0] for o in range(0,0x120,8)]
        has=False
        for o in range(0, 0x80, 8):
            q2 = qs[o//8]
            if q2 in str_vas or q2 in cand_set or (q2 - image_base) in cand_rva_set:
                has=True; break
        # also check fn exec
        exec_offs=[]
        for fn_off in range(0x40,0x110,8):
            if fn_off//8 >= len(qs): continue
            fn_va = qs[fn_off//8]
            fn_rva = fn_va - image_base if fn_va >= image_base and fn_va < image_base+0x20000000 else fn_va
            if text_rva <= fn_rva < text_rva+text_sz:
                exec_offs.append((fn_off, fn_rva))
        if has or exec_offs:
            print(f"[file] cand desc RVA 0x{d_rva:X} has_ray={has} exec_offs={exec_offs[:2]} qs0=0x{qs[0]:X} qs1=0x{qs[1]:X}")
        if has and exec_offs:
            fn_off, fn_rva = exec_offs[0]
            vt = qs[0]
            print(f"[file] SUCCESS desc RVA 0x{d_rva:X} fn_off 0x{fn_off:X} fn RVA 0x{fn_rva:X} vt 0x{vt:X}")
            return {"rva_desc": d_rva, "slot_rva": d_rva+fn_off, "fn_rva": fn_rva, "bound_fn_off": fn_off, "image_base": image_base}
    # fallback: try any desc with exec fn even without has_ray (maybe string via hash)
    for d_rva in sorted(desc_cands):
        sec = next((s for s in secs if s[1] <= d_rva < s[1]+s[2]), None)
        if not sec: continue
        _, va, vsz, praw, sraw = sec
        off = d_rva - va
        if off+0x120 > sraw: continue
        qs = [struct.unpack_from("<Q", raw, praw+off+o)[0] for o in range(0,0x120,8)]
        for fn_off in range(0x40,0x110,8):
            if fn_off//8 >= len(qs): continue
            fn_va = qs[fn_off//8]
            fn_rva = fn_va - image_base if fn_va >= image_base else fn_va
            if text_rva <= fn_rva < text_rva+text_sz:
                # check if any nearby qword is plausible name (printable)
                print(f"[file] FALLBACK desc RVA 0x{d_rva:X} fn_off 0x{fn_off:X} fn RVA 0x{fn_rva:X}")
                return {"rva_desc": d_rva, "slot_rva": d_rva+fn_off, "fn_rva": fn_rva, "bound_fn_off": fn_off, "image_base": image_base}
    print("[file] no descriptor with exec fn found - try broader")
    return None

def patch_instance_offsets(theo_json):
    """Patch offsets.h instance offsets from theo Offsets.* (like Offsets::update() does)"""
    if not theo_json or "Offsets" not in theo_json: return 0
    offs = theo_json["Offsets"]
    txt = OFFSETS_H.read_text(encoding="utf-8", errors="ignore")
    orig = txt
    patched=0
    # also update ClientVersion
    ver = theo_json.get("Roblox Version")
    if ver:
        txt_new, n = re.subn(r'inline std::string ClientVersion = "[^"]+"', f'inline std::string ClientVersion = "{ver}"', txt)
        if n: txt=txt_new; patched+=n
    for ns, fields in offs.items():
        if not isinstance(fields, dict): continue
        for field, val in fields.items():
            # find `namespace ns { ... inline uintptr_t field = 0x...`
            # use regex for that ns block
            pat = rf"(namespace {re.escape(ns)}\s*\{{[^}}]*?inline uintptr_t {re.escape(field)}\s*=\s*)0x[0-9a-fA-F]+"
            # need DOTALL for multiline inside namespace, but we approximate with search
            m = re.search(pat, txt, flags=re.DOTALL)
            if m:
                txt = re.sub(pat, rf"\g<1>0x{val:X}", txt, flags=re.DOTALL)
                patched+=1
            else:
                # try rva.h style? skip
                pass
    if patched and txt != orig:
        OFFSETS_H.write_text(txt, encoding="utf-8")
        print(f"[patch] instance offsets: {patched} fields updated from theo {ver}")
        # also update Dumped At comment if present
        return patched
    else:
        print(f"[patch] instance offsets: no changes or pattern mismatch ({patched})")
        return patched

def patch_headers(rva_desc, bound_off=BOUND_FN_OFF):
    """Patch offsets.h and rva.h with new RVA"""
    hex_desc = f"0x{rva_desc:X}"
    # offsets.h: WorldRoot::RaycastBoundDesc
    if OFFSETS_H.exists():
        txt = OFFSETS_H.read_text(encoding="utf-8", errors="ignore")
        new_txt, n = re.subn(r"(namespace WorldRoot\s*\{\s*inline uintptr_t RaycastBoundDesc\s*=\s*)0x[0-9a-fA-F]+", rf"\g<1>{hex_desc}", txt)
        if n:
            # also ensure RaycastBoundFn
            new_txt2, n2 = re.subn(r"(inline uintptr_t RaycastBoundFn\s*=\s*)0x[0-9a-fA-F]+", rf"\g<1>0x{bound_off:X}", new_txt)
            OFFSETS_H.write_text(new_txt2, encoding="utf-8")
            print(f"[patch] {OFFSETS_H} RaycastBoundDesc -> {hex_desc} ({n} replacements)")
        else:
            print(f"[patch] pattern not found in {OFFSETS_H} - edit manually: RaycastBoundDesc = {hex_desc}")
            print(txt[txt.find("WorldRoot"):txt.find("WorldRoot")+500])
    else:
        print(f"[patch] not found {OFFSETS_H}")

    if RVA_H.exists():
        txt = RVA_H.read_text(encoding="utf-8", errors="ignore")
        # RVA::WorldRoot::RaycastBoundDesc and RVA::Reflection::Raycast::Descriptor
        # Update WorldRoot
        new_txt, n = re.subn(r"(namespace WorldRoot\s*\{\s*inline constexpr std::uintptr_t RaycastBoundDesc\s*=\s*)0x[0-9a-fA-F]+", rf"\g<1>{hex_desc}", txt)
        if n:
            RVA_H.write_text(new_txt, encoding="utf-8")
            print(f"[patch] {RVA_H} RaycastBoundDesc -> {hex_desc}")
        else:
            # try adding if missing
            print(f"[patch] WorldRoot pattern not found in rva.h, add: {hex_desc}")
    print("[patch] done. Rebuild Ivory.sln")

def main():
    ap = argparse.ArgumentParser(description="Ivory Raycast Auto-Updater (IDA-free)")
    ap.add_argument("--patch", action="store_true", help="patch offsets.h/rva.h")
    ap.add_argument("--exe", type=str, help="offline exe path")
    ap.add_argument("--pid", type=int, help="Roblox PID to scan")
    args = ap.parse_args()

    print("=== Ivory Raycast Auto-Updater ===")
    theo = fetch_theo()
    if theo and args.patch:
        patch_instance_offsets(theo)
    if args.exe:
        res = offline_scan_file(args.exe)
        if res and args.patch:
            patch_headers(res["rva_desc"], res["bound_fn_off"])
        return
    pids = [args.pid] if args.pid else find_roblox_processes()
    if pids:
        for pid in pids:
            # try both exe and dll bases - Froststrap loads real code in dll
            for mod_name in ("RobloxPlayerBeta.dll", "RobloxPlayerBeta.exe"):
                res = live_scan_raycast_slot(pid, mod_name)
                if res:
                    print(f"\n[RESULT] {mod_name} Base=0x{res['base']:X} DescRVA=0x{res['rva_desc']:X} SlotRVA=0x{res['slot_rva']:X} fn=0x{res['fn']:X}")
                    print(f"  offsets.h: RaycastBoundDesc = 0x{res['rva_desc']:X}")
                    print(f"  offsets.h: RaycastBoundFn   = 0x{res['bound_fn_off']:X} (stable)")
                    print(f"  rva.h:     WorldRoot::RaycastBoundDesc = 0x{res['rva_desc']:X}")
                    print(f"  Slot absolute = base + 0x{res['rva_desc']:X} + 0x{res['bound_fn_off']:X} = 0x{res['slot']:X}")
                    if args.patch:
                        patch_headers(res['rva_desc'], res['bound_fn_off'])
                    else:
                        print("\nRun with --patch to auto-apply to offsets.h/rva.h")
                    return
        print("[live] found no slot in any pid - instance offsets still updated, Raycast will use dynamic fallback at runtime")
        if args.patch:
            print("[patch] Raycast not patched, but instance offsets are. Rebuild and test - raycast.cpp dynamic scan may still find slot in-game (Hyperion bypass).")
    else:
        print("[live] RobloxPlayerBeta.exe not running")
        # Try find exe on disk
        local = Path(os.environ.get("LOCALAPPDATA","")) / "Roblox" / "Versions"
        frost = Path(os.environ.get("LOCALAPPDATA","")) / "Froststrap" / "Versions"
        for base in (local, frost):
            if base.exists():
                exes = list(base.rglob("RobloxPlayerBeta.exe"))
                if exes:
                    exes = sorted(exes, key=lambda p: p.stat().st_mtime, reverse=True)
                    print(f"[file] newest install {exes[0]}")
                    res = offline_scan_file(str(exes[0]))
                    if res and args.patch:
                        patch_headers(res["rva_desc"], res["bound_fn_off"])
                    break
        print("\nTip: Launch Roblox (in-game, not menu) then re-run with --patch for live scan. Even without Raycast patch, runtime dynamic scan in raycast.cpp covers it.")
        if theo:
            print(f"\n[theo] Instance offsets up to date for {theo.get('Roblox Version')}, Raycast uses runtime scan fallback.")

if __name__ == "__main__":
    main()
