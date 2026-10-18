# detect.ac / Sonar — Detection Mechanics & Evasion Surface

How every pillar of the scanner catches cheat residue, and what a target has to
do (or never do) to leave nothing behind. Written from the actual source.

**Core thesis of the scanner:** it never inspects the game. It audits the
machine's forensic state as admin, and — this is the important part — **the
cleanup is the loudest signal**. Every wiping primitive (BAM, prefetch, USN,
event logs, Amcache, Windows.db, install date, clock, recycle bin, hosts) has a
paired "was this tampered with" check. So there are two ways to get cooked:

1. Leave artifacts behind.
2. Delete artifacts — the deletion *is* the detection.

The only clean win is to never generate the artifact in the first place.

Log semantics: only `DETECTION`/`CRITICAL` lines are verdicts (hash-chained,
uploaded, ban-grade). `WARNING` is strong supporting evidence, `INFO` is
baseline telemetry used for correlation.

---

## 0. How a scan actually runs

- Admin required; aborts instantly if test signing is on (instant DETECTION).
- `c_detection::start_scan` → heartbeat every 10s → integrity snapshot seeded
  into the tamper-guarded log chain → ~150 modules enqueued on a
  `hardware_concurrency`-sized thread pool → 12 UI stages → logs + results
  uploaded to the pin-bound session.
- Per-game builds compile different module sets (Fortnite skips 33 modules,
  Minecraft adds atlas_trace + JVM checks, etc). Stage counts are hardcoded
  per game.
- Server-side, per-pin string lists (`/getstrings/{pin}`) let the operator
  target any process live without shipping a new binary.

---

## 1. Execution artifacts — "what ran on this box"

### BAM (Background Activity Moderator registry hive)
**How detected:** live parse of
`HKLM\SYSTEM\ControlSet001\Services\bam\State\UserSettings\<SID>`; a second
pass raw-reads the SYSTEM hive cluster-by-cluster off `\\.\C:`
(`FSCTL_GET_RETRIEVAL_POINTERS` + hand-rolled hbin/nk/vk parser) and flags any
BAM-like value found in *freed cells or transaction logs* but not live.
Additionally: every BAM FILETIME is checked against `InstallDate` — if no entry
is older than "now", the hive was wiped (`BAM Has Been Cleaned`). BAM service
stopped/restarted is separately flagged via SCM.

**To not be detected:**
- Never stop/restart the `bam` service.
- Never delete BAM values — the raw-hive pass recovers freed cells, and
  "everything is timestamped now" is itself a DETECTION.
- The only real option: the executed image must never touch disk as a
  file-backed PE (BAM records execution of on-disk binaries per user session).

### Amcache
**How detected:** raw NTFS read of `Amcache.hve` (locked live, so the scanner
copies it off the volume, or triggers the Compatibility Appraiser task first),
hand-parsed `InventoryApplicationFile` / `InventoryDriverBinary` keys → SHA1
matched against ~550-entry per-game cheat tables + live LOLDrivers list.
Zero entries = "Amcache Cleaned" (unless within 30-day fresh-install grace).

**To not be detected:**
- Wiping the hive = flagged. Don't.
- Known builds are burned by hash — only unmodified-known-cheat builds hit.
  But see YARA below: content rules arrive server-side and update remotely,
  so hash evasion is temporary.

### Prefetch
**How detected:** seven separate modules:
- `EnablePrefetcher` ≠ 3 or key missing / key recreated after logon.
- `FileInfo.sys` service key modified in last 24h or `Start==4` (disables
  prefetch logging kernel-side).
- `.pf` files set READONLY; duplicate-SHA1 `.pf` pairs (type/echo forgery).
- `.pf` deleted — via USN `FILE_DELETE` with parent `c:\windows\prefetch\`;
  after-logon deletion = DETECTION (pre-logon = WARNING only). Bulk deletes
  (>5/sec) are treated as OS noise and *ignored* — see evasion.
- Prefetch contents parsed (MAM decompressed via `RtlDecompressBufferEx`) and
  associated filenames resolved; FiveM-gated "injector" correlation via
  `CITIZENFX.INI`.
- Prefetch folder entirely missing.
- `.tpm`-renamed files into `%TEMP%` from prefetch = "FLK Bypass".

**To not be detected:**
- Do not disable prefetcher, do not touch FileInfo.sys, do not set READONLY,
  do not rename payloads to `.tpm`.
- Deleting individual `.pf` after you ran the cheat is *worse* than leaving it
  — USN catches the delete and timestamps it against your logon.
- The 5/sec burst filter means a bulk prefetch wipe looks like OS maintenance —
  but combined with the "EnablePrefetcher modified" and journal checks it's a
  cluster of tells, and deleted-`.exe.pf` matching is separate anyway.
- Winning move: nothing file-backed ever executes (no prefetch entry ever
  created).

### USN journal itself
**How detected:** journal size ≠ 32MB default; Ntfs-Operational EVTX 501
(create/clear) after boot; `$UsnJrnl:$J` file creation/write timestamps after
boot; `UsnJournalID` (interpreted as FILETIME) after boot; oldest journal
record ≥ boot time (everything older wiped).

**To not be detected:** never reset/shrink/recreate the journal — every one of
those actions has a dedicated detector. The journal is not your problem; your
problem is the ~300 filename-substring rules and delete-reason rules inside it
(see next).

### USN content rules
**How detected:** full-journal read per drive; rule table maps
`{USN reason, filename substring}` → label. Matches include: cheat
aimbot/noclip/hwid_spoofer deletions, `kdmapper`, PSReadLine
`ConsoleHost_history.txt`, AnyDesk `file_transfer_trace.txt`, `imgui.ini`,
CrashDumps, `.rpf` (FiveM), deleted `Windows.db` / `ActivitiesCache.db`
(exact-name module), junction/reparse deletions (`DELETE|REPARSE|CLOSE` =
USN-redirection bypass), deleted `.evtx`, `icacls.exe` data-extend (ACL
stealth), `Amcache.hve`, PCA databases.

**To not be detected:**
- The file's *name* is the signature. Cheats named `aimbot.dll` get caught at
  the moment of deletion, forever.
- Never delete the cheat while the scanner can see the journal — the delete
  record is permanent until the journal rolls.
- Junction tricks to hide paths are a named detection.
- Deleting the activity-history databases is a named exact-match detection.
- Real answer: the payload never exists as a named file on an NTFS volume the
  scanner reads (all mounted volumes are scanned, including letters-less ones).

### SRUM, PCA, Windows.db (Search index)
**How detected:** SRUM `SRUDB.dat` network table parsed (raw MFT copy or
`esentutl` soft-recovery when locked) — LOLBin rows with traffic =
"possible PE injection". PCA databases deleted (via USN rules) flagged.
Windows Search `Windows.db` property store scanned with Aho-Corasick over URL
values (per-game pattern blocks). Deleted `Windows.db`/`ActivitiesCache.db`
flagged exactly.

**To not be detected:**
- Past injection staging through notepad/osk/calc/cmd/conhost leaves firewall
  rules AND SRUM rows. Cleaning the firewall is visible in USN; cleaning SRUM
  requires replacing a system DB. Don't stage through LOLBins.
- Don't let cheat URLs get into Windows Search (opening URL files, indexable
  docs) — and don't delete the DB to hide it.

### Autoruns / persistence
**How detected:** full autoruns sweep (Run keys, Winlogon, startup .lnk, Active
Setup, BHO/ShellEx CLSIDs, hidden scheduled tasks, auto-start services/drivers,
AppInit_DLLs, Winsock catalogs, LSA packages, WMI event consumers) + signature
verification + per-entry USN history. Verdicts: image hijack, unsigned
autorun, `cmd/notepad` masquerade, IFEO modified since logon. WMI: *every*
event consumer is reported with its trigger query.

**To not be detected:** no persistence, memory-only. Any unsigned on-disk
autorun, any IFEO Debugger, any WMI subscription = flag. Deleting them after
the fact = USN/registry lastWrite flags.

---

## 2. Anti-forensics — the cleanup detectors

| If you do this | It gets flagged as |
|---|---|
| Clear event logs (EventID 104 after logon, System/PS channels worst) | `Cleared EventLog Since Logon` |
| Set EVTX files READONLY (blocks `wevtutil cl`) | `Eventlog Bypass` |
| Create `HKLM\...\MiniNt` (WinPE spoof — kills Security log after reboot) | `Eventlog Exploit Found` |
| Disable ScriptBlockLogging (`=0`) | `Powershell Exploit Found` |
| Shrink PS event log to 1028KB (self-wrapping) | `Powershell Bypass Flush Found` |
| Spoof `InstallDate` (≤0, pre-2001, or >now+26h) | `Spoofed Windows Install Date` |
| Change system time after logon | `Time Change` (Kernel-General EVTX) |
| Empty recycle bin after logon | `Recycle Bin Modified` |
| Modify hosts file since boot | `hosts file has been modified` |
| Delete prefetch files after logon | `Manually Deleted Prefetch File (After Logon)` |
| Wipe BAM (all entries "now") | `BAM Has Been Cleaned` |
| Reset/shrink USN journal | 6 different USN tamper detections |
| Delete/replace registry hives | freed-cell IFEO/BAM recovery via VSS + transaction logs |
| Delete VSS copies after the fact | shadow-copy lifecycle leaves more traces |
| Clear browser history | `Browsing History Cleared Recently` (DB younger than 7 days) |
| Delete Windows.db / ActivitiesCache.db | exact USN match |
| Delete measured-boot logs / edit TPM log | EFI log tamper detections |
| Mount a letterless volume | `Disk Volume Without Drive Letter` |
| Empty USN-cheat deletions with "wipe tools" | every wipe primitive has a detector |

**Rule of thumb: the scanner assumes anyone cleaning evidence is guilty.**
There is no safe cleaning primitive. Fresh OS install inside the 30-day grace
window is the only reset some checks respect — and even that has install-date,
BAM-age, and measured-boot correlations.

---

## 3. Live memory / runtime catches (scan-time state)

These don't care what you cleaned — they read what's resident *right now*:

- **LSASS string scan** — ~200 hardcoded cheat C2 domains (Eulen AWS hosts,
  panels) matched in lsass memory → cheat comms resident in the process that
  owns network state.
- **Dnscache string scan** — DNS lookups persist until reboot. Any resolution
  of a flagged domain since boot is visible.
- **DPS memory** — Diagnostic Policy Service caches monitored-file records
  (`!!<name>!<date>`); 1000-entry table maps known cheat *build dates* to
  names ("Vape Lite Signed Cheat ran since logon"). Also catches
  double-extension runs and execution from non-C: volumes.
- **Appinfo memory** — carves UAC-elevated launch paths (unsigned only).
- **~25 per-process custom-string scanners** — chrome/edge/brave/opera/
  vivaldi/firefox/discord/javaw/fivem/powershell/lghub_agent/runtimebroker/
  sihost/taskhostw + services (Appinfo, CryptSvc, Diagtrack, DPS, DusmSvc,
  EventLog, MpsSvc, PcaSvc, Schedule, SgrmBroker, SysMain, MsMpEng). Filters
  are **server-pushed per pin** — the operator can add a string for any process
  and every already-shipped scanner picks it up.
- **Hardcoded memory markers** — javaw: `triggerbot`/`crystalaura`/`dopium`…
  runtimebroker: Blackhole injector PDB paths (`engine/dll_inject.cpp`);
  roblox: `solara`/`matrixclient` while in-instance; winmgmt: `-jar` clients.
- **Injection checks** — PE headers in memory not backed by a file; injected
  DLL via OpenSave MRU; suspended service/lsass threads (SuspendThread probe);
  firewall rules naming LOLBins; SRUM traffic on LOLBin rows.
- **Discord alt accounts** — parses discord.exe memory for the users cache →
  every loaded account ID/username.
- **atlas_trace (Minecraft)** — live JVM: unbacked RX pages, manual-mapped /
  PEB-unlinked modules, JNIEnv table hooks, JNI cheat-class refs, inline
  detours, IAT hooks, thread start addresses in hidden memory, unsigned JVMTI
  agents, impostor `net/minecraft` namespaces, recently-unloaded modules.
  Corroboration model: ≥2 signals on the same region = Critical. Regions owned
  by Microsoft/Oracle/Mojang/Badlion/Overwolf-signed modules are trusted.

**To not be detected:**
- Nothing cheat-related may be resident at scan time: no loaded DLLs, no open
  handles, no cached C2 strings in lsass, no DNS entries (reboot clears
  dnscache only).
- No alt accounts loaded in Discord/game launchers before the scan.
- atlas: unloading before the scan is *partially* defeated by the
  unloaded-module check — it looks for recently-unlinked unknown modules.
- The strongest runtime control the operator has: custom strings can be pushed
  for *your* process specifically, per-pin, minutes before the scan.

---

## 4. Browsers

- History DBs (12 Chromium + Firefox places.sqlite) copied and queried;
  ~370 cheat URL signatures (per-game gated) → `User Visited <X Website>`.
- Downloads table joined with URL chains → downloaded cheat archives with
  target path.
- Cleared-history heuristic: `MIN(last_visit_time)` ≤ 7 days = wiped DB.
- Same URLs re-caught via dnscache strings, Windows.db, and USN on the DB files.

**To not be detected:** cheat sites never touched from this machine, this
browser profile, this OS install. Clearing is a detection; *not* clearing
leaves the visit. There is no third state on the scanned profile — use a
different device.

---

## 5. Hardware / drivers / boot

| Check | Catch | Evadable? |
|---|---|---|
| DMA pads | SetupAPI PCI enum — spoof heuristics (ID mostly `F`s, FFFFFFFF subsys, REV_FF, all-FF ContainerId) | A real (unspoofed) DMA device is visible by name; spoofing IDs is itself the flag |
| DMA online verify | Live lookup vs pci-ids/devicehunt/linux-hardware; "kmbox"/"fuser"/"direct memory access" in name = DMA; unresolvable = spoofed | Only genuinely unknown hardware evades the name match — but unverified = flagged |
| DMA protection | DmaGuard disabled + Kernel-IoIommu EVTX 7/8/12 (definitive hardware DMA event) | Leave IOMMU events alone; can't hide a fired IOMMU block |
| HDMI fuser | EDID size/checksum/native-res checks; missing DDC/CI capability | Only by not using the fuser |
| NVIDIA patch | memcmp of `nvd3dumx.dll` vs 2 hardcoded patch signatures (overlay/streamproof bypass); ShadowPlay FTS registry tamper | Different patch bytes dodge the memcmp (arms race — rules are server-side updatable elsewhere) |
| CFG | Kernel MitigationOptions nibble == 2 ("MachoCheats indicator") | Don't disable CFG machine-wide |
| Test signing | Enabled → scan aborts + instant DETECTION | Off, always |
| SecureBoot | Registry vs live firmware mismatch; fake PolicyPublisher GUID; HVCI off | Can't fake — registry must match hardware |
| Measured boot | TCG log parse → USB boot device paths = EFI cheat stick; disk log vs live TPM log byte-compare; holes in boot sequence | Boot internal only; never delete/edit MeasuredBoot logs |
| Crash dumps | `%LOCALAPPDATA%\CrashDumps` minidumps + EVTX 1000 → unsigned crashed exe = "suspicious crashed file" | Don't crash; disable dumps machine-wide *before* (changing dump settings leaves its own traces) |

---

## 6. Accounts / alt detection

- Minecraft alts (launcher caches), Steam alts, R6 alts, Epic alts, Discord
  accounts (memory), amcache-driven game-instance check ("unknown Minecraft
  client"), Roblox in-instance executor strings.
- AnyDesk `file_transfer_trace.txt` = cheat files moved over remote access.
- Club44: duplicate "Microsoft Root Certificate Authority 2010" in the ROOT
  store = bypass certificate residue.

**To not be detected:** alt accounts never loaded on this install; no remote
transfer of cheat files through AnyDesk; no duplicate root CAs installed.

---

## 7. YARA (server-delivered content rules)

- Rules fetched over X25519 + HKDF + AES-GCM + P256 signature, anti-rollback
  versioning (`rv.bin`), compiled in-process, then a **full sweep of every
  volume**: dir-stack walk, ~35 extensions (PE, scripts, archives, media,
  `.tpm`, `.detect`, `.rpf`, `.lua`…), `clamp(hw*2, 4, 32)` scan threads.
- Rule vars: `filename`, `is_hidden`, compile-time `current_time`.
- Tagging: hidden drive (path without drive letter), control-char filenames,
  unsigned drivers cross-checked against live LOLDrivers SHA1 list,
  Authenticode suppression for `_d_Suspicious_*` rules, WARNING-class rules
  via `// @dn ... WARNING` markers.
- Excluded dirs: `\Windows\WinSxS`, `\Program Files`, `\Windows\assembly`,
  `\Windows\ServiceProfiles\` — plus non-cloud reparse handling and
  cloud-placeholder skip.

**To not be detected:**
- Rules change server-side without a client update — offline you cannot know
  the current ruleset. Hash-based avoidance of Amcache tables is strictly
  weaker than this.
- The exclusion list is a real surface: files under `Program Files` /
  WinSxS / assembly are never YARA-scanned (but still hit Amcache on
  execution, plus MFT/ADS and signature heuristics elsewhere).
- Unsigned `.sys` on any hidden volume is a guaranteed flag via LOLDrivers
  cross-ref.

---

## 8. The cheat-sheet (what actually gets people caught)

Ranked by how often the residue exists regardless of skill:

1. **The cheat file existed on an NTFS volume, at any point since the journal
   rolled** — USN name rules, Amcache hash, prefetch names, SRUM, PCA,
   OpenSave MRU, saved-file MRU.
2. **Any wiping attempt** — every primitive has a detector; wiping converts a
   WARNING-class leftover into a DETECTION-class tamper.
3. **DNS / LSASS / DPS residency** — C2 domains and build-date watermarks live
   in system processes until reboot (or longer for DPS records).
4. **Browser + search history** — 370 URL signatures across History, Windows.db,
   and dnscache; clearing is its own detection.
5. **Persistence** — autoruns, IFEO, WMI consumers, scheduled tasks,
   file-handler hijacks. Unsigned + after-logon = flag.
6. **Hardware** — DMA pads, fusers, patched NVIDIA drivers, disabled
   DmaGuard/CFG/test-signing: hardware state is not cleanable.
7. **Runtime state at scan time** — anything loaded/injected/hooked right now
   (atlas, string scans, suspended threads, PE-in-memory).
8. **Alts and remote transfer** — client caches and AnyDesk traces.

**The only robust evasion is prophylactic:** nothing named like a cheat ever
written to disk, no cheat domains resolved on this OS install, no browser
sessions on this profile, no persistence, nothing resident at scan time, no
wiping after the fact. Every shortcut from that list has a dedicated module
pointed at it.

---

## 9. Operator notes — known evasion surfaces (for hardening)

If you're running this scanner, these are the gaps targets can actually use;
each is a candidate for the Sonar 10x pass:

1. **YARA excluded dirs** — `Program Files` / WinSxS / assembly are never
   content-scanned. Amcache still catches execution, but dormant files hide.
2. **USN delete burst threshold** — >5 deletes/sec in the same second are
   discarded as noise; a scripted bulk wipe of a cheat directory may fall
   under it (other journal-size/recreation checks still fire).
3. **Amcache = hash tables only** — a one-byte-patched build misses the
   per-game SHA1 list; rely on YARA + signature heuristics to catch it.
4. **Pre-logon artifact timing** — several checks severity-split on
   "before logon" (recycle bin, prefetch deletes, autoruns files). A boot-time
   cleaner that runs before interactive logon downgrades verdicts.
5. **Runtime scans are point-in-time** — dnscache clears on reboot; lsass
   strings age out; a reboot + cold scan window loses runtime residue. The
   heartbeat/integrity snapshot don't cover "user rebooted to sanitize RAM".
6. **Custom strings are per-pin** — if the operator doesn't push strings for
   the currently-trending tool, the ~25 process scanners only catch their
   hardcoded lists.
7. **Fresh-install grace (30 days)** — `FreshInstall::WithinGrace` suppresses
   some verdicts (EFI artifacts, empty Amcache). A fresh VM/install is a
   partial reset — correlate HWID (BIOS serial), motherboard IDs, and
   prior pins server-side.
8. **test_signing module is broken** — it calls `NtQuerySystemInformation`
   class 64 with a 4-byte buffer and tests a bit that never gets written
   (`c_test_signing.cpp:22-29`); the real CI-options check lives in
   `integrity_snapshot.hpp:342-372` and should be reused.
9. **Volume coverage** — confirm every mounted/letterless/USB volume gets the
   USN + YARA passes; network-mapped drives and subst'd paths are a classic
   off-scope hiding spot.
10. **Server-side correlation is the real net** — single-machine clean state
    means little if the same pin/HWID showed residue in earlier scans; keep
    `detected_bits`, session history, and HWID chained across sessions.
