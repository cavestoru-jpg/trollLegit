<#
    Full build chain, in the only order that produces a shippable injector.

    The injector does not load enhance.dll from disk -- it carries the DLL as a
    byte array in injector/bytes.hpp and drops it to %TEMP% at runtime. A plain
    solution build links the injector against whatever bytes.hpp happened to be
    on disk and only then relinks the DLL, so the shipped injector is always one
    revision behind. Building through this script instead keeps the payload in
    step with the sources.

    Usage:   powershell -ExecutionPolicy Bypass -File tools\build.ps1
#>

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
$javac = "C:\Program Files\Java\jdk-21.0.11\bin\javac.exe"
$dll = Join-Path $root "x64\Release\enhance.dll"
$injector = Join-Path $root "x64\Release\injector.exe"

function Step($n, $text) {
    Write-Host ""
    Write-Host "[$n/5] $text" -ForegroundColor Cyan
}

function Fail($text) {
    Write-Host "FAILED: $text" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $msbuild)) { Fail "MSBuild not found at $msbuild" }
if (-not (Test-Path $javac)) { Fail "javac not found at $javac" }

# --- 0. Mapping sanity -----------------------------------------------------
# A stale obfuscated name compiles perfectly and then resolves to a null id at
# runtime, so the feature just quietly stops working. Checking the header
# against the official Yarn file turns that into something you find here.
Step 0 "Verifying mappings against the official Yarn file"

python (Join-Path $root "tools\verify_mappings.py")
if ($LASTEXITCODE -ne 0) {
    Write-Host "WARNING: some mappings do not resolve -- see the list above." -ForegroundColor Yellow
    Write-Host "Continuing, but whatever uses them will silently do nothing at runtime." -ForegroundColor Yellow
}

# --- 1. Java renderer -> embedded bytecode --------------------------------
Step 1 "Compiling the Java renderer"

$classOut = Join-Path $env:TEMP "enhance_java_build"
if (Test-Path $classOut) { Remove-Item -Recurse -Force $classOut }
New-Item -ItemType Directory -Force $classOut | Out-Null

$lwjgl = @(
    (Join-Path $root "tools\lib\lwjgl-3.2.2.jar"),
    (Join-Path $root "tools\lib\lwjgl-opengl-3.2.2.jar")
) -join ";"

& $javac --release 21 -Xlint:all -cp $lwjgl -d $classOut (Join-Path $root "enhance\java\EnhanceRenderer.java")
if ($LASTEXITCODE -ne 0) { Fail "javac" }

$classFile = Join-Path $classOut "enhance\EnhanceRenderer.class"
if (-not (Test-Path $classFile)) { Fail "EnhanceRenderer.class was not produced" }

python (Join-Path $root "tools\embed_bytes.py") $classFile (Join-Path $root "enhance\java\enhance_renderer_class.hpp") "enhance_renderer_class"
if ($LASTEXITCODE -ne 0) { Fail "embedding the Java class" }

# --- 2. Client DLL ---------------------------------------------------------
Step 2 "Building enhance.dll"

& $msbuild (Join-Path $root "enhance.vcxproj") /p:Configuration=Release /p:Platform=x64 /t:Build /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { Fail "building enhance.dll" }
if (-not (Test-Path $dll)) { Fail "enhance.dll was not produced" }

# --- 3. DLL -> injector payload -------------------------------------------
Step 3 "Embedding the DLL into the injector payload"

python (Join-Path $root "tools\embed_bytes.py") $dll (Join-Path $root "injector\bytes.hpp") "dll_bytes" "dll_size"
if ($LASTEXITCODE -ne 0) { Fail "embedding the DLL" }

# --- 4. Injector -----------------------------------------------------------
Step 4 "Building injector.exe"

# OutDir is forced because a direct project build otherwise lands in
# injector\x64\Release while a solution build lands in x64\Release, leaving two
# copies and no way to tell which one is current.
& $msbuild (Join-Path $root "injector\injector.vcxproj") /p:Configuration=Release /p:Platform=x64 /p:OutDir="$root\x64\Release\" /t:Rebuild /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { Fail "building injector.exe" }

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Get-Item $dll, $injector | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize

$dllTime = (Get-Item $dll).LastWriteTime
$injTime = (Get-Item $injector).LastWriteTime
if ($injTime -lt $dllTime) {
    Write-Host "WARNING: injector.exe is older than enhance.dll -- the payload is stale." -ForegroundColor Yellow
    exit 1
}
