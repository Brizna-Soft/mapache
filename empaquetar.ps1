# Arma publicar\Mapache-<versión>-x64.zip (Mapache.exe, LICENSE, AVISOS-DE-TERCEROS.txt y LEEME.txt)
# y su huella publicar\Mapache-<versión>-x64.zip.sha256.
#
# Uso (después de compilar.ps1):
#   powershell -ExecutionPolicy Bypass -File empaquetar.ps1 [-Salida <carpeta>]
param(
    [string]$Salida = (Join-Path $PSScriptRoot 'publicar')
)

$ErrorActionPreference = 'Stop'
$codigo = $PSScriptRoot
$version = (Get-Content (Join-Path $codigo 'version.txt') -TotalCount 1).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "version.txt debe tener el formato 1.2.3 (tiene '$version')" }

$exe = Join-Path $codigo 'programa\Mapache.exe'
if (-not (Test-Path $exe)) { throw 'Falta programa\Mapache.exe: primero ejecuta compilar.ps1' }
$archivos = @($exe) + (@('LICENSE', 'AVISOS-DE-TERCEROS.txt', 'LEEME.txt') | ForEach-Object { Join-Path $codigo $_ })
foreach ($a in $archivos) { if (-not (Test-Path $a)) { throw "Falta $a" } }

New-Item -ItemType Directory -Force $Salida | Out-Null
$temporal = Join-Path ([IO.Path]::GetTempPath()) ('mapache-paquete-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $temporal | Out-Null
try {
    Copy-Item $archivos $temporal
    $zip = Join-Path $Salida "Mapache-$version-x64.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path (Join-Path $temporal '*') -DestinationPath $zip -CompressionLevel Optimal
    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$zip.sha256", "$hash  $(Split-Path $zip -Leaf)`n", (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Listo: $zip"
    Write-Host "SHA256: $hash"
}
finally {
    Remove-Item -Recurse -Force $temporal -ErrorAction SilentlyContinue
}
