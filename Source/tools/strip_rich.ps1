param([string]$In, [string]$Out)
# Post-process a PE: zero the DOS stub + Rich header region (the compiler/build
# fingerprint sitting between the DOS header and the PE header). Nothing at
# runtime reads it - it exists only for DOS-era tooling, and it gives AV
# machine-learning models a strong toolchain fingerprint to key on.
#
# Defender races the fresh link with a scan lock on the new file; without a
# retry the strip loses that race and the plaintext DLL keeps its Rich header
# (which is exactly what got the payload flagged). Retry hard, fast.
$max = 12
for ($attempt = 1; $attempt -le $max; $attempt++) {
    try {
        $b = [System.IO.File]::ReadAllBytes($In)
        $e_lfanew = [BitConverter]::ToInt32($b, 0x3C)
        if ($e_lfanew -le 0x40 -or $e_lfanew -ge $b.Length) { Write-Error "bad PE header in $In"; exit 1 }
        for ($i = 0x40; $i -lt $e_lfanew; $i++) { $b[$i] = 0 }
        for ($w = 0; $w -lt $max; $w++) {
            try { [System.IO.File]::WriteAllBytes($Out, $b); break }
            catch { Start-Sleep -Milliseconds 200 }
        }
        if ($w -ge $max) { throw "write kept failing" }
        Write-Output ("stripped {0} bytes (0x40-0x{1:X})" -f ($e_lfanew - 0x40), $e_lfanew)
        exit 0
    }
    catch {
        Start-Sleep -Milliseconds 250
    }
}
Write-Error "strip_rich: file unreadable after $max attempts (AV lock held)"
exit 1
