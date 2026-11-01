# Offline only: fresh external output, CubeIDE compiler, reproducible receipts. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$CobsRoot,
    [string]$Stm32CubeRoot = (Join-Path $env:USERPROFILE 'STM32Cube\Repository\STM32Cube_FW_H7RS_V1.3.0'),
    [string]$ArmGccBin,
    [string]$CMake,
    [string]$Ninja,
    [switch]$ReadMany,
    [ValidateRange(1,32)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cobs = (Resolve-Path -LiteralPath $CobsRoot).Path
$sdk = (Resolve-Path -LiteralPath $Stm32CubeRoot).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
foreach ($sourceRoot in @($repo, $cobs, $sdk)) {
    if ($output.Equals($sourceRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $output.StartsWith($sourceRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $sourceRoot.StartsWith($output.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'OutputDirectory must be outside all source trees and their parents.'
    }
}
if ((Test-Path -LiteralPath $output) -and @(Get-ChildItem -LiteralPath $output -Force).Count) {
    throw 'OutputDirectory must be fresh and empty.'
}
if (-not $ArmGccBin) {
    $matches = @(Get-ChildItem -Path 'C:\ST\STM32CubeIDE_*\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*\tools\bin\arm-none-eabi-gcc.exe' -File |
        Sort-Object FullName -Descending)
    if (-not $matches.Count) { throw 'STM32CubeIDE GNU compiler not found; set ArmGccBin.' }
    $ArmGccBin = $matches[0].DirectoryName
}
$ArmGccBin = (Resolve-Path -LiteralPath $ArmGccBin).Path
if ($ArmGccBin -notlike '*STM32CubeIDE*') { throw 'Select the STM32CubeIDE compiler, not CubeCLT.' }
$gcc = Join-Path $ArmGccBin 'arm-none-eabi-gcc.exe'
$gxx = Join-Path $ArmGccBin 'arm-none-eabi-g++.exe'
function Resolve-BuildTool([string]$Requested, [string]$Name, [string]$Fallback) {
    if ($Requested) {
        if (Test-Path -LiteralPath $Requested -PathType Leaf) {
            return (Resolve-Path -LiteralPath $Requested).Path
        }
        $command = Get-Command -Name $Requested -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command) { return $command.Source }
        throw "Missing $Name tool '$Requested'; pass its executable path or command name."
    }
    $command = Get-Command -Name $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    if (Test-Path -LiteralPath $Fallback -PathType Leaf) {
        return (Resolve-Path -LiteralPath $Fallback).Path
    }
    throw "$Name was not found on PATH; pass its executable with -$Name."
}
# Standalone CMake/Ninja installations work without installing a Qt kit.
# Explicit selections must resolve; an invalid override never silently falls back.
$CMake = Resolve-BuildTool $CMake 'cmake' 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$Ninja = Resolve-BuildTool $Ninja 'ninja' 'C:\Qt\Tools\Ninja\ninja.exe'
foreach ($tool in @($gcc, $gxx, $CMake, $Ninja)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing tool $tool" }
}
New-Item -ItemType Directory -Path $output | Out-Null
$receipt = [ordered]@{passed=$false; hardware=$false; flash=$false; readMany=[bool]$ReadMany; startedUtc=[DateTime]::UtcNow.ToString('o');
    sourceRoot=$repo; cobsRoot=$cobs; stm32CubeRoot=$sdk; toolchain=$gcc; commands=@()}
$oldPath = $env:PATH
$env:PATH = "$ArmGccBin;$oldPath"
function Run-Logged([string]$Name, [string]$Tool, [string[]]$Arguments) {
    $log = Join-Path $output ($Name + '.log')
    $start = [DateTime]::UtcNow
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Tool @Arguments 2>&1 | Out-File -LiteralPath $log -Encoding utf8; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $previous }
    $script:receipt.commands += [ordered]@{name=$Name; tool=$Tool; arguments=$Arguments; exit=$code;
        seconds=([DateTime]::UtcNow-$start).TotalSeconds; log=$log; sha256=(Get-FileHash -LiteralPath $log -Algorithm SHA256).Hash.ToLowerInvariant()}
    Write-Host "$Name exit=$code"
    if ($code -ne 0) { Get-Content -LiteralPath $log -Tail 45; throw "$Name failed" }
    return (Get-Content -LiteralPath $log -Raw)
}
function Input-Hashes {
    $dirs = @($PSScriptRoot, (Join-Path $repo 'lib'),
        (Join-Path $cobs 'src'), (Join-Path $cobs 'libs\spsc'),
        (Join-Path $sdk 'Drivers\STM32H7RSxx_HAL_Driver\Inc'),
        (Join-Path $sdk 'Drivers\STM32H7RSxx_HAL_Driver\Src'),
        (Join-Path $sdk 'Drivers\CMSIS\Include'),
        (Join-Path $sdk 'Drivers\CMSIS\Device\ST\STM32H7RSxx\Include'),
        (Join-Path $sdk 'Drivers\CMSIS\Device\ST\STM32H7RSxx\Source\Templates'))
    $hashes = [ordered]@{}
    $files = foreach ($dir in $dirs) {
        Get-ChildItem -LiteralPath $dir -Recurse -File | Where-Object {
            $_.Extension -in '.cpp','.c','.hpp','.h','.s','.ld','.cmake','.txt','.ps1' -and
            $_.FullName -notmatch '\\(out|build|tests|\.git)\\'
        }
    }
    $files += Get-Item -LiteralPath (Join-Path $repo 'examples\inspector_serial\SerialProfile.hpp')
    foreach ($file in ($files | Sort-Object FullName -Unique)) {
        $hashes[$file.FullName] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return $hashes
}
try {
    $receipt.compilerVersion = (Run-Logged 'gcc-version' $gcc @('--version')).Trim()
    $receipt.toolHashes = [ordered]@{}
    foreach ($tool in @($gcc, $gxx, $CMake, $Ninja)) {
        $receipt.toolHashes[$tool] = (Get-FileHash -LiteralPath $tool -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $receipt.inputHashesBefore = Input-Hashes
    $build = Join-Path $output 'build'
    Run-Logged 'configure' $CMake @('-S',$PSScriptRoot,'-B',$build,'-G','Ninja',
        "-DCMAKE_TOOLCHAIN_FILE=$PSScriptRoot\arm-gcc.cmake", "-DARM_GCC_BIN=$ArmGccBin",
        "-DCOBS_ROOT=$cobs", "-DSTM32_CUBE_ROOT=$sdk", "-DCMAKE_MAKE_PROGRAM=$Ninja",
        "-DNUCLEO_ENABLE_READ_MANY=$($ReadMany.IsPresent.ToString().ToUpperInvariant())",
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON') | Out-Null
    Run-Logged 'build' $CMake @('--build',$build,'-j',"$Jobs") | Out-Null
    $elf = Join-Path $build 'telemetry-nucleo-h7s3.elf'
    $size = Run-Logged 'size' (Join-Path $ArmGccBin 'arm-none-eabi-size.exe') @($elf)
    $symbols = Run-Logged 'symbols' (Join-Path $ArmGccBin 'arm-none-eabi-nm.exe') @('-S','-C',$elf)
    if ($symbols -match '(?m)\b(operator new|operator delete|malloc|calloc|realloc|free|_sbrk)\b') {
        throw 'Unexpected allocator symbol in fixed-pool firmware.'
    }
    Run-Logged 'binary' (Join-Path $ArmGccBin 'arm-none-eabi-objcopy.exe') @('-O','binary',$elf,(Join-Path $build 'telemetry-nucleo-h7s3.bin')) | Out-Null
    $receipt.size = $size.Trim()
    $receipt.artifacts = [ordered]@{}
    foreach ($file in (Get-ChildItem -LiteralPath $output -Recurse -File | Where-Object { $_.Extension -in '.elf','.map','.bin','.su' })) {
        $receipt.artifacts[$file.FullName.Substring($output.Length+1)] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    if (-not @($receipt.artifacts.Keys | Where-Object { $_ -like '*.su' }).Count) { throw 'No stack-usage receipt produced.' }
    $receipt.inputHashesAfter = Input-Hashes
    if (($receipt.inputHashesBefore | ConvertTo-Json -Compress -Depth 4) -ne ($receipt.inputHashesAfter | ConvertTo-Json -Compress -Depth 4)) {
        throw 'Source inputs changed during offline qualification.'
    }
    $receipt.passed = $true
} catch { $receipt.error = $_.ToString(); Write-Error -Message $_ -ErrorAction Continue }
finally {
    $env:PATH = $oldPath
    $receipt.finishedUtc = [DateTime]::UtcNow.ToString('o')
    $receipt | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8
}
if (-not $receipt.passed) { exit 1 }
