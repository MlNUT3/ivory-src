param([string]$Out)
$stamp = Get-Date -Format "yyyyMMddHHmmssfff"
$content = "#pragma once`r`nstatic const char* kBuildStamp = `"$stamp`";`r`n"
Set-Content -Path $Out -Value $content -Encoding ASCII
