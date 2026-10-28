# Arma AVISOS-DE-TERCEROS.txt con las licencias de las bibliotecas que van dentro de Mapache.exe:
# los archivos «copyright» que deja vcpkg en build\vcpkg_installed\x64-windows-static-rel\share\*\.
# Hay que compilar antes (compilar.ps1).
#
# Uso: powershell -ExecutionPolicy Bypass -File herramientas\avisos.ps1
param(
    [string]$Instaladas = (Join-Path $PSScriptRoot '..\build\vcpkg_installed\x64-windows-static-rel\share'),
    [string]$Salida = (Join-Path $PSScriptRoot '..\AVISOS-DE-TERCEROS.txt')
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Instaladas)) { throw "No encuentro $Instaladas. Compila primero con compilar.ps1" }

function Version([string]$carpeta, [string]$nombre) {
    $spdx = Join-Path $carpeta 'vcpkg.spdx.json'
    if (-not (Test-Path $spdx)) { return '' }
    $j = Get-Content $spdx -Raw -Encoding UTF8 | ConvertFrom-Json
    $p = $j.packages | Where-Object { $_.name -eq $nombre } | Select-Object -First 1
    if ($p) { return ($p.versionInfo -replace '#\d+$', '') }
    return ''
}

$texto = New-Object System.Text.StringBuilder
[void]$texto.AppendLine('Avisos de terceros de Mapache')
[void]$texto.AppendLine('=============================')
[void]$texto.AppendLine('')
[void]$texto.AppendLine('Mapache es un programa libre de Brizna Soft, con licencia MIT (archivo LICENSE).')
[void]$texto.AppendLine('Dentro de Mapache.exe van estas bibliotecas de otras personas, cada una con su propia licencia.')
[void]$texto.AppendLine('Aquí están sus avisos de derechos de autor y sus licencias, como ellas lo piden.')
[void]$texto.AppendLine('')

# wxWidgets primero (es la principal) y después las demás en orden alfabético
$paquetes = Get-ChildItem $Instaladas -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'copyright') } |
    Sort-Object @{ Expression = { $_.Name -ne 'wxwidgets' } }, Name
foreach ($p in $paquetes) {
    $v = Version $p.FullName $p.Name
    [void]$texto.AppendLine('-' * 78)
    [void]$texto.AppendLine(("{0} {1}" -f $p.Name, $v).Trim())
    [void]$texto.AppendLine('-' * 78)
    [void]$texto.AppendLine('')
    [void]$texto.AppendLine((Get-Content (Join-Path $p.FullName 'copyright') -Raw -Encoding UTF8).TrimEnd())
    # vcpkg solo trae un aviso corto de PCRE2: se agrega su licencia completa
    if ($p.Name -eq 'pcre2') {
        [void]$texto.AppendLine('')
        [void]$texto.AppendLine((Get-Content (Join-Path $PSScriptRoot 'licencias\pcre2-LICENCE.md') -Raw -Encoding UTF8).TrimEnd())
    }
    [void]$texto.AppendLine('')
}

$contenido = $texto.ToString() -replace "`r?`n", "`r`n"
[IO.File]::WriteAllText([IO.Path]::GetFullPath($Salida), $contenido, (New-Object System.Text.UTF8Encoding($true)))
"Listo: {0} ({1} bibliotecas, {2:N0} bytes)" -f $Salida, $paquetes.Count, (Get-Item $Salida).Length
