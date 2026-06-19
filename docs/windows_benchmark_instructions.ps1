# PowerShell benchmarking instructions for Windows 11
# Run this from the repository root in PowerShell (user shell). This script:
#  1) builds the `benchmark` target in Release,
#  2) runs the benchmark and saves textual output to `bench_output.txt`,
#  3) collects hardware/compiler/CMake metadata files, and
#  4) attempts to render `media/benchmark.png` from the textual output using ImageMagick.

# NOTE: On Windows Visual Studio generator, CMake is multi-config; we call `cmake --build . --config Release`.

# ---------------------------
# 1) Configure & build (Release)
# ---------------------------
$buildDir = "build"
if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }
Push-Location $buildDir

# Configure (add -G "Ninja" if you want a single-config generator)
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build the `benchmark` target in Release (multi-config generators supported)
cmake --build . --config Release --target benchmark

# Locate the produced benchmark executable (support common generator layouts)
$exePath = $null
$candidates = @(".\Release\benchmark.exe", ".\benchmark.exe")
foreach ($p in $candidates) {
    if (Test-Path $p) { $exePath = (Resolve-Path $p).Path; break }
}
if (-not $exePath) {
    $found = Get-ChildItem -Path . -Filter benchmark.exe -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $exePath = $found.FullName }
}

if (-not $exePath) {
    Pop-Location
    Write-Error "benchmark.exe not found. Build may have failed; inspect CMake output in the build directory."
    exit 1
}

# ---------------------------
# 2) Run benchmark and capture output
# ---------------------------
# Run and capture both stdout and stderr into `bench_output.txt` at repo root
$benchOut = "..\bench_output.txt"
Write-Host "Running benchmark: $exePath -> $benchOut"
& $exePath > $benchOut 2>&1

Pop-Location

# ---------------------------
# 3) Collect hardware and system metadata
# ---------------------------
# CPU model, logical cores, physical cores, RAM, OS version
Get-CimInstance -ClassName Win32_Processor | Select-Object Name,NumberOfLogicalProcessors,NumberOfCores | Format-List > .\hardware_cpu.txt
Get-CimInstance -ClassName Win32_ComputerSystem | Select-Object TotalPhysicalMemory,Manufacturer,Model | Format-List > .\hardware_system.txt
Get-CimInstance -ClassName Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber | Format-List > .\os_version.txt
systeminfo | Select-String -Pattern 'OS Name','OS Version','System Manufacturer','System Model' > .\systeminfo.txt

# Compiler version detection (MSVC / GCC / Clang)
if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
    Write-Host "cl.exe found on PATH - capturing compiler output"
    & cl.exe > .\compiler_version.txt 2>&1
} elseif (Get-Command g++ -ErrorAction SilentlyContinue) {
    & g++ --version > .\compiler_version.txt 2>&1
} elseif (Get-Command clang++ -ErrorAction SilentlyContinue) {
    & clang++ --version > .\compiler_version.txt 2>&1
} else {
    Write-Host "No common compiler found on PATH; record your compiler manually if publishing results." | Out-File .\compiler_version.txt
}

# CMake version
cmake --version > .\cmake_version.txt

# Environment variables (optional)
Get-ChildItem Env: | Sort-Object Name | Format-Table -AutoSize > .\env_vars.txt

# ---------------------------
# 4) Create media/benchmark.png from bench_output.txt (optional)
# ---------------------------
if (-not (Test-Path "media")) { New-Item -ItemType Directory -Path "media" | Out-Null }

if (Test-Path ".\bench_output.txt") {
    $size = (Get-Item ".\bench_output.txt").Length
    if ($size -gt 0) {
        if (Get-Command magick -ErrorAction SilentlyContinue) {
            Write-Host "Rendering media\benchmark.png using ImageMagick (magick)."
            magick -background white -fill black -font "Consolas" -pointsize 10 caption:@bench_output.txt media\benchmark.png
        } elseif (Get-Command convert -ErrorAction SilentlyContinue) {
            Write-Host "Rendering media\benchmark.png using ImageMagick (convert)."
            convert -background white -fill black -font "Consolas" -pointsize 10 caption:@bench_output.txt media\benchmark.png
        } else {
            Write-Host "ImageMagick not found. Install ImageMagick and ensure 'magick' or 'convert' is on PATH to generate media\benchmark.png."
        }
    } else {
        Write-Host "bench_output.txt exists but is empty; skipping image render."
    }
} else {
    Write-Host "bench_output.txt not found; skipping image render. Run the benchmark to generate bench_output.txt first."
}

# ---------------------------
# Expected output files (relative to repo root)
# ---------------------------
# bench_output.txt        - textual benchmark output (calibrated ns + calibration factor)
# media\benchmark.png    - image rendered from textual output (optional)
# hardware_cpu.txt        - CPU name + core counts
# hardware_system.txt     - RAM, manufacturer, model
# compiler_version.txt    - captured compiler output or note
# cmake_version.txt       - cmake --version
# systeminfo.txt          - Windows system info excerpt
# env_vars.txt            - captured environment variables

# Notes:
# - If `cl.exe` is not on PATH, run this script from a "Developer Command Prompt for VS" or run the Visual Studio vcvars batch file first to populate the toolchain in the environment.
# - The harness prints the calibration factor (cycles/ns); bench_output.txt contains the calibrated nanosecond latencies and the calibration value. If you need raw cycle dumps, modify the harness to also persist raw tick deltas.

# End of script
