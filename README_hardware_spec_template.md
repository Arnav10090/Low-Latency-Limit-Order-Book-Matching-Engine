**Hardware & Build Environment**
- OS: Windows 11 (see `systeminfo.txt`)
- CPU: <CPU model from hardware_cpu.txt>
- Logical cores: <logical cores>
- Physical cores: <physical cores>
- RAM: <TotalPhysicalMemory from hardware_system.txt> (bytes — convert to GB for publication)
- Compiler: <compiler_version.txt>
- CMake: <cmake_version.txt>
- Build config: `Release` (we use `cmake --build . --config Release` for multi-config generators; see `docs/windows_benchmark_instructions.ps1`)

**How to reproduce**
1. Run `docs\windows_benchmark_instructions.ps1` from repo root in PowerShell.
2. Attach `bench_output.txt`, `media/benchmark.png`, and the hardware files when publishing results.

PowerShell: convert `TotalPhysicalMemory` (bytes) to GB for publication:

```powershell
# Example: get total physical memory in GB
(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB
```
