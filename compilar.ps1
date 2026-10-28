# Compila Mapache y deja el programa listo en .\programa
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File compilar.ps1 [-Limpiar] [-Publicacion] [-Pruebas]
#
#   -Limpiar      borra la carpeta build antes de compilar
#   -Publicacion  versión limpia (la de version.txt, sin "-local"); la usa GitHub Actions
#   -Pruebas      solo para desarrollo: incluye las opciones ocultas de prueba (--limpieza-de-prueba=, --lento).
#                 Nunca se publica: no se puede usar junto con -Publicacion.
#
# Necesita Visual Studio 2022 (o sus Build Tools) con C++ y Git. Si la variable VCPKG_ROOT no está puesta,
# baja vcpkg (versión fija) a vendor\vcpkg la primera vez.
param(
    [switch]$Limpiar,
    [switch]$Publicacion,
    [switch]$Pruebas
)

$ErrorActionPreference = 'Stop'
if ($Publicacion -and $Pruebas) { throw 'Una versión para publicar no puede llevar las opciones de prueba (-Pruebas)' }
$codigo = $PSScriptRoot
$build = Join-Path $codigo 'build'
$programa = Join-Path $codigo 'programa'
$vcpkgEtiqueta = '2026.10.16'

function Paso($texto) { Write-Host "`n=== $texto ===" -ForegroundColor Cyan }

Paso 'Buscando Visual Studio 2022'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'No encuentro Visual Studio 2022 (falta vswhere.exe)' }
$vs = & $vswhere -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -latest -property installationPath
if (-not $vs) { throw 'No encuentro Visual Studio 2022 con las herramientas de C++' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { throw "No encuentro el CMake de Visual Studio en $cmake" }

Paso 'Preparando vcpkg'
if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = Join-Path $codigo 'vendor\vcpkg'
    if (-not (Test-Path (Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'))) {
        & git clone --depth 1 --branch $vcpkgEtiqueta https://github.com/microsoft/vcpkg $env:VCPKG_ROOT
        if ($LASTEXITCODE -ne 0) { throw 'No pude bajar vcpkg' }
    }
}
if (-not (Test-Path (Join-Path $env:VCPKG_ROOT 'vcpkg.exe'))) {
    & (Join-Path $env:VCPKG_ROOT 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'No pude preparar vcpkg' }
}
$env:VCPKG_DISABLE_METRICS = '1'
Write-Host "vcpkg: $env:VCPKG_ROOT"

$version = (Get-Content (Join-Path $codigo 'version.txt') -TotalCount 1).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "version.txt debe tener el formato 1.2.3 (tiene '$version')" }
$texto = $version
if (-not $Publicacion) {
    # Con $ErrorActionPreference = 'Stop', PowerShell 5.1 corta el script si git escribe un error
    # (por ejemplo, antes del primer commit). Por eso aquí se relaja un momento.
    $sha = $null
    $antes = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $sha = & git -C $codigo rev-parse --short=7 HEAD 2>$null
        if ($LASTEXITCODE -ne 0) { $sha = $null }
    }
    catch { $sha = $null }
    finally { $ErrorActionPreference = $antes }
    $texto = if ($sha) { "$version-local+$("$sha".Trim())" } else { "$version-local" }
    if ($Pruebas) { $texto += '-pruebas' }
}
Write-Host "Versión: $texto"

if ($Limpiar -and (Test-Path $build)) {
    Paso 'Borrando la compilación anterior'
    Remove-Item -Recurse -Force $build
}

Paso 'Configurando (la primera vez vcpkg compila wxWidgets; tarda unos minutos)'
& $cmake -S $codigo -B $build -G 'Visual Studio 17 2022' -A x64 -T v143 `
    "-DAPP_VERSION_TEXTO=$texto" `
    "-DMAPACHE_PRUEBAS=$(if ($Pruebas) { 'ON' } else { 'OFF' })" `
    "-DVCPKG_INSTALL_OPTIONS=--clean-buildtrees-after-build;--clean-packages-after-build"
if ($LASTEXITCODE -ne 0) { throw "Falló la configuración (código $LASTEXITCODE)" }

Paso 'Compilando'
& $cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Falló la compilación (código $LASTEXITCODE)" }

Paso 'Copiando el programa'
New-Item -ItemType Directory -Force $programa | Out-Null
Copy-Item (Join-Path $build 'Release\Mapache.exe') $programa -Force
Write-Host "Listo: $programa" -ForegroundColor Green
