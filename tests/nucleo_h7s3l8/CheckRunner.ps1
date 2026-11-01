# Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
# Maintained OFFLINE controls: no real programmer, ARM tools, USB, SWD or COM.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$ExternalOutput)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'These process/PnP runner controls require Windows.' }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output = [IO.Path]::GetFullPath($ExternalOutput)
if ($output.StartsWith($repo.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or $output -ieq $repo -or
    (Test-Path -LiteralPath $output)) {
    throw 'ExternalOutput must be a NEW directory outside the Telemetry checkout.'
}
$runner = Join-Path $PSScriptRoot 'RunHardware.ps1'
$mockSource = Join-Path $PSScriptRoot 'MockHardware.cs'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw 'Windows .NET Framework C# compiler is required.' }
[void][IO.Directory]::CreateDirectory($output)
$mockDirectory = Join-Path $output 'mock-tools'
[void][IO.Directory]::CreateDirectory($mockDirectory)
$mockProgrammer = Join-Path $mockDirectory 'MockProgrammer.exe'
$compiled = & $compiler /nologo /target:exe ("/out:" + $mockProgrammer) $mockSource 2>&1
$compiled | Set-Content -LiteralPath (Join-Path $output 'mock-compile.txt') -Encoding UTF8
if ($LASTEXITCODE -ne 0) { throw 'Offline mock compilation failed.' }
foreach ($name in @('MockGui.exe', 'MockReadMany.exe', 'MockGit.exe', 'arm-none-eabi-g++.exe', 'arm-none-eabi-readelf.exe', 'arm-none-eabi-objcopy.exe')) {
    Copy-Item -LiteralPath $mockProgrammer -Destination (Join-Path $mockDirectory $name)
}
# Function precedence shadows the real Windows PnP cmdlets in RunHardware's scope.
# Even their inventory path therefore does not enumerate a physical device.
function Get-PnpDevice {
    [CmdletBinding()]param([string]$Class, [switch]$PresentOnly, [string]$InstanceId)
    [IO.File]::AppendAllText($env:TELEMETRY_MOCK_PNP_CALLS, "Get-PnpDevice MOCK`n")
    if (-not $InstanceId) {
        return [pscustomobject]@{ Status = 'OK'; FriendlyName = 'OFFLINE MOCK STLink (COM6)';
            InstanceId = 'USB\VID_0483&PID_3754&MI_01\MOCK' }
    }
    return [pscustomobject]@{ Status = 'OK'; FriendlyName = 'OFFLINE MOCK'; InstanceId = $InstanceId }
}
function Get-PnpDeviceProperty {
    [CmdletBinding()]param([string]$InstanceId, [string]$KeyName)
    [IO.File]::AppendAllText($env:TELEMETRY_MOCK_PNP_CALLS, "Get-PnpDeviceProperty MOCK`n")
    return [pscustomobject]@{ Data = 'USB\VID_0483&PID_3754\002A001F3033510135393935' }
}
function Put-U16([byte[]]$Bytes, [int]$Offset, [uint16]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 2)
}
function Put-U32([byte[]]$Bytes, [int]$Offset, [uint32]$Value) {
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $Bytes, $Offset, 4)
}
function New-ElfFixture {
    # ELF32 LE ARM executable, one 64-byte flash LOAD, one covered PROGBITS section.
    $bytes = New-Object byte[] 400
    [Array]::Copy([byte[]](0x7f, 0x45, 0x4c, 0x46, 1, 1, 1), $bytes, 7)
    Put-U16 $bytes 16 2; Put-U16 $bytes 18 40; Put-U32 $bytes 20 1
    Put-U32 $bytes 24 0x08000009; Put-U32 $bytes 28 52; Put-U32 $bytes 32 320
    Put-U16 $bytes 40 52; Put-U16 $bytes 42 32; Put-U16 $bytes 44 1
    Put-U16 $bytes 46 40; Put-U16 $bytes 48 2
    Put-U32 $bytes 52 1; Put-U32 $bytes 56 256
    Put-U32 $bytes 60 0x08000000; Put-U32 $bytes 64 0x08000000
    Put-U32 $bytes 68 64; Put-U32 $bytes 72 64; Put-U32 $bytes 76 5; Put-U32 $bytes 80 256
    for ($i = 256; $i -lt 320; ++$i) { $bytes[$i] = [byte]($i - 255) }
    Put-U32 $bytes 256 0x24010000; Put-U32 $bytes 260 0x08000009
    Put-U32 $bytes 364 1; Put-U32 $bytes 368 6
    Put-U32 $bytes 372 0x08000000; Put-U32 $bytes 376 256
    Put-U32 $bytes 380 64; Put-U32 $bytes 392 4
    return ,$bytes
}
$elf = Join-Path $output 'valid-arm-fixture.elf'
$original = New-ElfFixture
[IO.File]::WriteAllBytes($elf, $original)
$results = New-Object Collections.Generic.List[object]
$python = (Get-Command python -CommandType Application -ErrorAction Stop | Where-Object {
    (Get-Item -LiteralPath $_.Source -ErrorAction Stop).Length -gt 0 } | Select-Object -First 1).Source
$readManyVerifier = Join-Path $PSScriptRoot 'VerifyReadManyEvidence.py'
$mockCobs = Join-Path $output 'mock-cobs'
[void][IO.Directory]::CreateDirectory((Join-Path $mockCobs 'src'))
[void][IO.Directory]::CreateDirectory((Join-Path $mockCobs 'libs\spsc'))
[IO.File]::WriteAllText((Join-Path $mockCobs 'src\MockCobs.h'), '// Offline COBS source identity fixture.')
[IO.File]::WriteAllText((Join-Path $mockCobs 'libs\spsc\MockSpsc.hpp'), '// Offline SPSC source identity fixture.')
$environmentNames = @('TELEMETRY_MOCK_MODE', 'TELEMETRY_MOCK_FLASH', 'TELEMETRY_MOCK_CALLS', 'TELEMETRY_MOCK_PNP_CALLS')
$oldEnvironment = @{}
foreach ($name in $environmentNames) { $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name) }
$pnpCallsPath = Join-Path $output 'mock-pnp-calls.txt'
$env:TELEMETRY_MOCK_PNP_CALLS = $pnpCallsPath
$passed = $false
$failure = $null
try {
    foreach ($mode in @('pass', 'checkfail', 'guifail', 'guitimeout', 'flashfail',
            'restorefail', 'readbackwrong', 'wrongdevice')) {
        $case = Join-Path $output $mode
        [void][IO.Directory]::CreateDirectory($case)
        $env:TELEMETRY_MOCK_MODE = $mode
        $env:TELEMETRY_MOCK_FLASH = Join-Path $case 'FAKE-flash.bin'
        $env:TELEMETRY_MOCK_CALLS = Join-Path $case 'FAKE-calls.txt'
        $bytes = New-Object byte[] 65536
        for ($i = 0; $i -lt $bytes.Length; ++$i) { $bytes[$i] = [byte](($i * 17 + 23) % 256) }
        [IO.File]::WriteAllBytes($env:TELEMETRY_MOCK_FLASH, $bytes)
        $before = (Get-FileHash -LiteralPath $env:TELEMETRY_MOCK_FLASH).Hash
        $failed = $false
        try {
            & $runner -Elf $elf -GuiExe (Join-Path $mockDirectory 'MockGui.exe') -Programmer $mockProgrammer `
                -ArmTools $mockDirectory -ExternalOutput (Join-Path $case 'receipt') -GuiTimeoutSeconds 10
        } catch { $failed = $true; Write-Host "Control $mode`: $($_.Exception.Message)" }
        $receipt = Get-Content -LiteralPath (Join-Path $case 'receipt\summary.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        $after = (Get-FileHash -LiteralPath $env:TELEMETRY_MOCK_FLASH).Hash
        $calls = Get-Content -LiteralPath $env:TELEMETRY_MOCK_CALLS
        if ($mode -eq 'pass') {
            $okay = -not $failed -and $receipt.qualification_passed -and $receipt.restored_and_verified -and $before -eq $after
        } elseif ($mode -eq 'restorefail') {
            $okay = $failed -and -not $receipt.qualification_passed -and -not $receipt.restored_and_verified -and $before -ne $after
        } elseif ($mode -eq 'readbackwrong') {
            $okay = $failed -and -not $receipt.qualification_passed -and -not $receipt.restored_and_verified -and $before -eq $after
        } elseif ($mode -eq 'wrongdevice') {
            $okay = $failed -and -not $receipt.flash_attempted -and -not $receipt.restoration_required -and
                $before -eq $after -and -not ($calls -match ' -w | -u ')
        } else {
            $okay = $failed -and -not $receipt.qualification_passed -and $receipt.restored_and_verified -and $before -eq $after
        }
        if ($mode -in @('checkfail', 'flashfail')) { $okay = $okay -and -not ($calls -match ' --smoke ') }
        if ($mode -eq 'guitimeout') {
            $timed = @($receipt.commands | Where-Object { $_.tag -eq 'serial-gui-smoke' })
            $okay = $okay -and $timed.Count -eq 1 -and $timed[0].timed_out
        }
        if (-not $okay) { throw "Offline mock control failed: $mode" }
        # Every invoked native process must be one of this run's mock executables.
        foreach ($command in $receipt.commands) {
            if (-not $command.executable.StartsWith($mockDirectory + '\', [StringComparison]::OrdinalIgnoreCase)) {
                throw 'A control attempted to invoke a real external tool.'
            }
        }
        $results.Add([ordered]@{ mode = $mode; passed = $true; runner_failed = $failed;
            simulated_flash_restored = ($before -eq $after); command_count = $receipt.commands.Count })
    }
    foreach ($mode in @('outside-load', 'orphan-section', 'not-arm', 'truncated')) {
        $fixture = $original.Clone()
        if ($mode -eq 'outside-load') { Put-U32 $fixture 64 0x09000000 }
        if ($mode -eq 'orphan-section') { Put-U32 $fixture 372 0x50000000 }
        if ($mode -eq 'not-arm') { Put-U16 $fixture 18 3 }
        if ($mode -eq 'truncated') { $fixture = [byte[]](0..20) }
        $fixturePath = Join-Path $output ($mode + '.elf')
        [IO.File]::WriteAllBytes($fixturePath, $fixture)
        $failed = $false
        try {
            & $runner -Elf $fixturePath -GuiExe (Join-Path $mockDirectory 'MockGui.exe') -Programmer $mockProgrammer `
                -ArmTools $mockDirectory -ExternalOutput (Join-Path $output ($mode + '-receipt')) -ValidateOnly
        } catch { $failed = $true; Write-Host "Control $mode`: $($_.Exception.Message)" }
        $receipt = Get-Content -LiteralPath (Join-Path $output ($mode + '-receipt\summary.json')) -Raw -Encoding UTF8 | ConvertFrom-Json
        if (-not $failed -or $receipt.commands.Count -ne 0 -or $receipt.flash_attempted -or
            $receipt.restoration_required -or $receipt.qualification_passed) {
            throw "Invalid ELF was not rejected before external tools: $mode"
        }
        $results.Add([ordered]@{ mode = $mode; passed = $true; runner_failed = $true; command_count = 0 })
    }
    # The existing twelve Inspector/ELF controls remain above. These controls
    # explicitly select the separate ReadMany parameter set and frozen mock CLI.
    foreach ($mode in @('readmany-pass', 'readmany-missing', 'readmany-badjson', 'readmany-failed',
            'readmany-counts', 'readmany-hash', 'readmany-capability', 'readmany-session',
            'readmany-recovery', 'readmany-status', 'readmany-duplicate', 'readmany-mixed',
            'readmany-timeout', 'readmany-exit', 'readmany-restore-retry', 'flashfail', 'restorefail', 'readbackwrong')) {
        $case = Join-Path $output ('read-many-' + $mode)
        [void][IO.Directory]::CreateDirectory($case)
        $env:TELEMETRY_MOCK_MODE = $mode
        $env:TELEMETRY_MOCK_FLASH = Join-Path $case 'FAKE-flash.bin'
        $env:TELEMETRY_MOCK_CALLS = Join-Path $case 'FAKE-calls.txt'
        $bytes = New-Object byte[] 65536
        for ($i = 0; $i -lt $bytes.Length; ++$i) { $bytes[$i] = [byte](($i * 17 + 23) % 256) }
        [IO.File]::WriteAllBytes($env:TELEMETRY_MOCK_FLASH, $bytes)
        $before = (Get-FileHash -LiteralPath $env:TELEMETRY_MOCK_FLASH).Hash
        $failed = $false
        $caseReceipt = Join-Path $case 'receipt'
        try {
            & $runner -Elf $elf -ReadManyExe (Join-Path $mockDirectory 'MockReadMany.exe') -Python $python `
                -CobsRoot $mockCobs -Git (Join-Path $mockDirectory 'MockGit.exe') `
                -Programmer $mockProgrammer -ArmTools $mockDirectory -ExternalOutput $caseReceipt -ReadManyTimeoutSeconds 10
        } catch { $failed = $true; Write-Host "Control $mode`: $($_.Exception.Message)" }
        $receipt = Get-Content -LiteralPath (Join-Path $caseReceipt 'summary.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        $after = (Get-FileHash -LiteralPath $env:TELEMETRY_MOCK_FLASH).Hash
        $calls = Get-Content -LiteralPath $env:TELEMETRY_MOCK_CALLS
        if ($mode -in @('readmany-pass', 'readmany-restore-retry')) {
            $okay = -not $failed -and $receipt.qualification_passed -and $receipt.restored_and_verified -and $before -eq $after
            & $python -B $readManyVerifier --receipt $caseReceipt
            if ($LASTEXITCODE -ne 0) { throw 'Independent ReadMany mock receipt verification failed.' }
        } elseif ($mode -eq 'restorefail') {
            $okay = $failed -and -not $receipt.qualification_passed -and -not $receipt.restored_and_verified -and $before -ne $after
        } elseif ($mode -eq 'readbackwrong') {
            $okay = $failed -and -not $receipt.qualification_passed -and -not $receipt.restored_and_verified -and $before -eq $after
        } else {
            $okay = $failed -and -not $receipt.qualification_passed -and $receipt.restored_and_verified -and $before -eq $after
        }
        $okay = $okay -and $receipt.source_inputs_unchanged -and $receipt.cobs_head -eq ('1' * 40) -and
            $receipt.cobs_head_after -eq $receipt.cobs_head
        $expectedReasons = @{
            'readmany-badjson' = 'invalid JSON report'; 'readmany-failed' = 'profile/result mismatch'
            'readmany-counts' = 'four ReadMany cases'; 'readmany-hash' = 'reply SHA256 mismatch'
            'readmany-capability' = '24-byte TRM1'; 'readmany-session' = 'strictly increase'
            'readmany-recovery' = 'lifecycle proof is not true'; 'readmany-status' = 'reply statuses mismatch'
            'readmany-duplicate' = 'duplicate reply ID'; 'readmany-mixed' = 'mixed result must contain'
        }
        if ($expectedReasons.ContainsKey($mode)) {
            $verifyCommand = @($receipt.commands | Where-Object { $_.tag -eq 'verify-read-many-report' })
            $verifierError = [IO.File]::ReadAllText((Join-Path $caseReceipt 'logs\verify-read-many-report.stderr.txt'))
            $okay = $okay -and $verifyCommand.Count -eq 1 -and $verifyCommand[0].exit_code -ne 0 -and
                $verifierError.Contains($expectedReasons[$mode])
        }
        if ($calls -match ' --smoke | --check-device |MockGui.exe') { throw 'ReadMany invoked the Inspector/GUI path.' }
        if ($mode -eq 'readmany-timeout') {
            $timed = @($receipt.commands | Where-Object { $_.tag -eq 'serial-read-many-check' })
            $okay = $okay -and $timed.Count -eq 1 -and $timed[0].timed_out
            $stillRunning = Get-Process -Id $timed[0].pid -ErrorAction SilentlyContinue
            $okay = $okay -and -not $stillRunning
        }
        if ($mode -eq 'flashfail') { $okay = $okay -and -not ($calls -match 'tested-read-many.exe') }
        if (-not $okay) { throw "Offline ReadMany mock control failed: $mode" }
        foreach ($command in $receipt.commands) {
            if ($command.tag -eq 'verify-read-many-report') {
                if ($command.executable -ne $python -or $command.arguments[1] -ne $readManyVerifier) {
                    throw 'A control invoked an unexpected report verifier.'
                }
            } elseif ($command.tag -eq 'serial-read-many-check') {
                if ($command.executable -ne (Join-Path $caseReceipt 'tested-read-many.exe')) {
                    throw 'A control invoked a non-frozen CLI.'
                }
            } elseif (-not $command.executable.StartsWith($mockDirectory + '\', [StringComparison]::OrdinalIgnoreCase)) {
                throw 'A ReadMany control attempted to invoke a real external tool.'
            }
        }
        $results.Add([ordered]@{ mode = ('read-many-' + $mode); passed = $true; runner_failed = $failed;
            simulated_flash_restored = ($before -eq $after); command_count = $receipt.commands.Count })
    }
    $ambiguousOutput = Join-Path $output 'ambiguous-parameters'
    $rejected = $false
    try {
        & $runner -Elf $elf -GuiExe (Join-Path $mockDirectory 'MockGui.exe') `
            -ReadManyExe (Join-Path $mockDirectory 'MockReadMany.exe') -Programmer $mockProgrammer `
            -CobsRoot $mockCobs -Git (Join-Path $mockDirectory 'MockGit.exe') `
            -ArmTools $mockDirectory -ExternalOutput $ambiguousOutput
    } catch { $rejected = $true }
    if (-not $rejected -or (Test-Path -LiteralPath $ambiguousOutput)) { throw 'Mutually exclusive checker parameters were accepted.' }
    $results.Add([ordered]@{ mode = 'read-many-mutually-exclusive'; passed = $true; runner_failed = $true; command_count = 0 })
    & $python -B $readManyVerifier --self-test
    if ($LASTEXITCODE -ne 0) { throw 'ReadMany report negative self-tests failed.' }
    $passed = $true
} catch { $failure = $_.ToString(); throw }
finally {
    Write-Host 'Offline controls: restoring harness environment.'
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name]) }
    Write-Host "Offline controls: hashing receipt inputs, controls=$($results.Count)."
    $summary = [ordered]@{
        schema = 1; passed = $passed; offline_mock_only = $true; physical_hardware_access = $false
        runner_sha256 = (Get-FileHash -LiteralPath $runner).Hash.ToLowerInvariant()
        controls_sha256 = (Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant()
        mock_source_sha256 = (Get-FileHash -LiteralPath $mockSource).Hash.ToLowerInvariant()
        read_many_verifier_sha256 = (Get-FileHash -LiteralPath $readManyVerifier).Hash.ToLowerInvariant()
        mock_executable_sha256 = (Get-FileHash -LiteralPath $mockProgrammer).Hash.ToLowerInvariant()
        framework_compiler = $compiler; framework_compiler_sha256 = (Get-FileHash -LiteralPath $compiler).Hash.ToLowerInvariant()
        valid_elf_sha256 = (Get-FileHash -LiteralPath $elf).Hash.ToLowerInvariant()
        # Get-Content strings carry PowerShell 5 provider metadata; keep plain
        # strings so JSON cannot recursively serialize PSDrive/PSProvider objects.
        pnp_calls = @([IO.File]::ReadAllLines($pnpCallsPath)); controls = @($results.ToArray()); error = $failure
    }
    $encoded = $summary | ConvertTo-Json -Depth 8
    [IO.File]::WriteAllText((Join-Path $output 'summary.json'), $encoded, (New-Object Text.UTF8Encoding($false)))
}
Write-Host "Offline runner controls: $($results.Count)/31 passed; no physical hardware access."
