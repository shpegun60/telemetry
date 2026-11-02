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
foreach ($name in @('MockGui.exe', 'MockReadMany.exe', 'MockGit.exe', 'MockPythonFail.exe', 'arm-none-eabi-g++.exe', 'arm-none-eabi-readelf.exe', 'arm-none-eabi-objcopy.exe')) {
    Copy-Item -LiteralPath $mockProgrammer -Destination (Join-Path $mockDirectory $name)
}
$unlaunchablePython = Join-Path $mockDirectory 'UnlaunchablePython.exe'
[IO.File]::WriteAllText($unlaunchablePython, 'OFFLINE non-executable interpreter fixture.')
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
[IO.File]::WriteAllText($pnpCallsPath, '')
$passed = $false
$failure = $null
try {
    # Fail at the actual verifier launcher before any target, programmer, ARM,
    # Git or PnP operation. An explicit failed interpreter must never fall back.
    foreach ($pythonMode in @('python-exit', 'python-unlaunchable')) {
        foreach ($validate in @($false, $true)) {
            $mode = $pythonMode + $(if ($validate) { '-validate' } else { '-hardware' })
            $case = Join-Path $output $mode
            [void][IO.Directory]::CreateDirectory($case)
            $env:TELEMETRY_MOCK_MODE = 'pass'
            $env:TELEMETRY_MOCK_FLASH = Join-Path $case 'FAKE-flash.bin'
            $env:TELEMETRY_MOCK_CALLS = Join-Path $case 'FAKE-calls.txt'
            [IO.File]::WriteAllBytes($env:TELEMETRY_MOCK_FLASH, (New-Object byte[] 65536))
            $before = (Get-FileHash -LiteralPath $env:TELEMETRY_MOCK_FLASH).Hash
            $pnpBefore = if (Test-Path -LiteralPath $pnpCallsPath) { [IO.File]::ReadAllText($pnpCallsPath) } else { '' }
            $candidate = if ($pythonMode -eq 'python-exit') {
                Join-Path $mockDirectory 'MockPythonFail.exe'
            } else { $unlaunchablePython }
            $failed = $false
            $caseReceipt = Join-Path $case 'receipt'
            try {
                & $runner -Elf $elf -ReadManyExe (Join-Path $mockDirectory 'MockReadMany.exe') -Python $candidate `
                    -CobsRoot $mockCobs -Git (Join-Path $mockDirectory 'MockGit.exe') -Programmer $mockProgrammer `
                    -ArmTools $mockDirectory -ExternalOutput $caseReceipt -ValidateOnly:$validate
            } catch { $failed = $true; Write-Host "Control $mode`: $($_.Exception.Message)" }
            $receipt = Get-Content -LiteralPath (Join-Path $caseReceipt 'summary.json') -Raw -Encoding UTF8 | ConvertFrom-Json
            $pnpAfter = if (Test-Path -LiteralPath $pnpCallsPath) { [IO.File]::ReadAllText($pnpCallsPath) } else { '' }
            $calls = @()
            if (Test-Path -LiteralPath $env:TELEMETRY_MOCK_CALLS) {
                $calls = @([IO.File]::ReadAllLines($env:TELEMETRY_MOCK_CALLS))
            }
            $preflight = @($receipt.commands | Where-Object { $_.tag -eq 'python-verifier-preflight' })
            if (-not $failed -or $receipt.qualification_passed -or $receipt.test_passed -or
                $receipt.flash_attempted -or $receipt.restoration_required -or $null -ne $receipt.backup -or
                $null -ne $receipt.target -or $receipt.commands.Count -ne 1 -or $preflight.Count -ne 1 -or
                $preflight[0].executable -ne $candidate -or $preflight[0].arguments[2] -ne '--self-test' -or
                $pnpBefore -cne $pnpAfter -or (Get-FileHash -LiteralPath $env:TELEMETRY_MOCK_FLASH).Hash -ne $before -or
                (Test-Path -LiteralPath (Join-Path $caseReceipt 'original-08000000-08010000.bin'))) {
                throw "Interpreter preflight touched a target or failed to reject the explicit interpreter: $mode"
            }
            if ($pythonMode -eq 'python-exit') {
                if ($preflight[0].exit_code -ne 7 -or $calls.Count -ne 1 -or
                    -not $calls[0].StartsWith('MockPythonFail.exe -B ')) { throw 'Interpreter exit failure was not exercised.' }
            } elseif ($null -ne $preflight[0].pid -or $null -ne $preflight[0].exit_code -or $calls.Count -ne 0) {
                throw 'The non-executable interpreter fixture unexpectedly launched a process.'
            }
            $results.Add([ordered]@{ mode = $mode; passed = $true; runner_failed = $true;
                target_commands = 0; pnp_calls_added = 0; simulated_flash_unchanged = $true })
        }
    }
    # Invoke the exact current process function in a fresh runspace. Fault only
    # its receipt dependency on call2, immediately after the owned process starts.
    $tokens = $null; $parseErrors = $null
    $runnerAst = [Management.Automation.Language.Parser]::ParseFile($runner, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) { throw 'Hardware runner did not parse for the process unit control.' }
    $unitText = 'param([string]$Checker, [string]$UnitOutput)' + "`n"
    foreach ($functionName in @('Quote-WindowsArgument', 'Get-Digest', 'Invoke-EvidenceWrite', 'Invoke-Recorded')) {
        $definition = $runnerAst.Find({ param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName
        }, $true)
        if (-not $definition) { throw "Missing runner function: $functionName" }
        $unitText += $definition.Extent.Text + "`n"
    }
    $unitText += @'
$ErrorActionPreference = 'Stop'
$CommandTimeoutSeconds = 10
$logs = $UnitOutput
$utf8 = New-Object Text.UTF8Encoding($false)
$script:receipt = [ordered]@{ commands = New-Object Collections.Generic.List[object]; error = $null;
    evidence_io_errors = New-Object Collections.Generic.List[object]; owned_process_stop_failed = $false }
$script:receiptWrites = 0
function Write-Receipt {
    ++$script:receiptWrites
    if ($script:receiptWrites -eq 2) { throw 'OFFLINE post-Start receipt I/O failure' }
}
$timer = [Diagnostics.Stopwatch]::StartNew()
$failed = $false
try { [void](Invoke-Recorded 'owned-wait' $Checker @('--unit-wait')) } catch { $failed = $true }
$elapsed = $timer.ElapsedMilliseconds
$entry = $script:receipt.commands[0]
$alive = $null -ne $entry.pid -and $null -ne (Get-Process -Id $entry.pid -ErrorAction SilentlyContinue)
if ($alive) {
    # A failing negative control must not leak its own mock child either.
    $owned = Get-Process -Id $entry.pid -ErrorAction Stop
    $owned.Kill(); [void]$owned.WaitForExit(5000); $owned.Dispose()
}
$fault = [IO.File]::ReadAllText((Join-Path $UnitOutput 'owned-wait.stderr.txt')).Contains('OFFLINE post-Start receipt I/O failure')
[pscustomobject]@{ passed = ($failed -and $fault -and $null -ne $entry.pid -and -not $alive -and
    -not $script:receipt.owned_process_stop_failed -and $elapsed -lt 10000);
    runner_failed = $failed; owned_pid = $entry.pid; owned_pid_gone = -not $alive;
    fault_after_start = $fault; elapsed_ms = $elapsed; receipt_write_count = $script:receiptWrites }
'@
    $unitDirectory = Join-Path $output 'owned-process-post-start-io'
    [void][IO.Directory]::CreateDirectory($unitDirectory)
    $env:TELEMETRY_MOCK_MODE = 'process-wait'
    $env:TELEMETRY_MOCK_CALLS = Join-Path $unitDirectory 'FAKE-calls.txt'
    $unit = [PowerShell]::Create()
    try {
        [void]$unit.AddScript($unitText).AddArgument((Join-Path $mockDirectory 'MockGui.exe')).AddArgument($unitDirectory)
        $unitResult = @($unit.Invoke())
        if ($unit.Streams.Error.Count -ne 0 -or $unitResult.Count -ne 1 -or -not $unitResult[0].passed) {
            throw 'Post-Start receipt failure left the owned mock process running or missed its fault window.'
        }
        $results.Add([ordered]@{ mode = 'owned-process-post-start-io'; passed = $true;
            runner_failed = $true; owned_pid = $unitResult[0].owned_pid;
            owned_pid_gone = $unitResult[0].owned_pid_gone; elapsed_ms = $unitResult[0].elapsed_ms })
    } finally { $unit.Dispose() }
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
    # I/O obstructions occur in the checker, after the test image was programmed.
    # Restoration must still execute when neither command logs nor receipts can be written.
    foreach ($mode in @('receipt-io', 'log-io', 'readmany-receipt-io', 'readmany-log-io', 'restore-log-exists')) {
        $case = Join-Path $output $mode
        [void][IO.Directory]::CreateDirectory($case)
        $env:TELEMETRY_MOCK_MODE = $mode
        $env:TELEMETRY_MOCK_FLASH = Join-Path $case 'FAKE-flash.bin'
        $env:TELEMETRY_MOCK_CALLS = Join-Path $case 'FAKE-calls.txt'
        [IO.File]::WriteAllBytes($env:TELEMETRY_MOCK_FLASH, $bytes)
        $before = (Get-FileHash -LiteralPath $env:TELEMETRY_MOCK_FLASH).Hash
        $failed = $false
        $caseReceipt = Join-Path $case 'receipt'
        try {
            if ($mode.StartsWith('readmany-')) {
                & $runner -Elf $elf -ReadManyExe (Join-Path $mockDirectory 'MockReadMany.exe') -Python $python `
                    -CobsRoot $mockCobs -Git (Join-Path $mockDirectory 'MockGit.exe') `
                    -Programmer $mockProgrammer -ArmTools $mockDirectory -ExternalOutput $caseReceipt
            } else {
                & $runner -Elf $elf -GuiExe (Join-Path $mockDirectory 'MockGui.exe') -Programmer $mockProgrammer `
                    -ArmTools $mockDirectory -ExternalOutput $caseReceipt
            }
        } catch { $failed = $true; Write-Host "Control $mode`: $($_.Exception.Message)" }
        $calls = [IO.File]::ReadAllLines($env:TELEMETRY_MOCK_CALLS)
        $backup = Join-Path $caseReceipt 'original-08000000-08010000.bin'
        $readback = Join-Path $caseReceipt 'restored-readback-1.bin'
        $after = (Get-FileHash -LiteralPath $env:TELEMETRY_MOCK_FLASH).Hash
        $restoreCalls = @($calls | Where-Object { $_.Contains(' -w ') -and $_.Contains('original-08000000-08010000.bin') })
        $readbackCalls = @($calls | Where-Object { $_.Contains(' -u ') -and $_.Contains('restored-readback-1.bin') })
        $verified = (Test-Path -LiteralPath $readback -PathType Leaf) -and
            (Get-Item -LiteralPath $readback).Length -eq 65536 -and
            (Get-FileHash -LiteralPath $readback).Hash -eq (Get-FileHash -LiteralPath $backup).Hash
        if (-not $failed -or $before -ne $after -or -not $verified -or
            $restoreCalls.Count -ne 1 -or $readbackCalls.Count -ne 1 -or
            ($mode -ne 'restore-log-exists' -and ($calls -match ' --smoke '))) {
            throw "Evidence I/O failure suppressed restoration or falsely passed: $mode"
        }
        if ($mode.EndsWith('log-io') -or $mode -eq 'restore-log-exists') {
            $receipt = Get-Content -LiteralPath (Join-Path $caseReceipt 'summary.json') -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($receipt.qualification_passed -or -not $receipt.restored_and_verified -or
                $receipt.evidence_io_errors.Count -lt 1 -or -not $receipt.error) {
                throw 'Log failure was not retained as a failed qualification with verified restoration.'
            }
        } else {
            if (-not (Test-Path -LiteralPath (Join-Path $caseReceipt 'summary.json') -PathType Container)) {
                throw 'Receipt I/O obstruction was unexpectedly removed.'
            }
        }
        if ($mode -eq 'restore-log-exists') {
            for ($attempt = 1; $attempt -le 3; ++$attempt) {
                $sentinel = Join-Path $caseReceipt ("logs\restore-original-$attempt.stdout.txt")
                if ([IO.File]::ReadAllText($sentinel) -cne 'OFFLINE MOCK: preserve existing restoration evidence') {
                    throw 'Emergency restoration overwrote existing command evidence.'
                }
            }
        }
        foreach ($call in $calls) {
            if ($call -notmatch '^(MockProgrammer|MockGui|MockGit|tested-read-many|arm-none-eabi-[a-z+]+)\.exe ') {
                throw 'An evidence I/O control invoked a non-mock tool.'
            }
        }
        $results.Add([ordered]@{ mode = $mode; passed = $true; runner_failed = $true;
            simulated_flash_restored = $true; independent_readback_verified = $true;
            restore_commands = $restoreCalls.Count; readback_commands = $readbackCalls.Count })
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
    # A successful ValidateOnly ReadMany preflight uses real Python but no device.
    $case = Join-Path $output 'python-valid-validate'
    [void][IO.Directory]::CreateDirectory($case)
    $env:TELEMETRY_MOCK_MODE = 'pass'
    $env:TELEMETRY_MOCK_FLASH = Join-Path $case 'FAKE-flash.bin'
    $env:TELEMETRY_MOCK_CALLS = Join-Path $case 'FAKE-calls.txt'
    $pnpBefore = [IO.File]::ReadAllText($pnpCallsPath)
    $caseReceipt = Join-Path $case 'receipt'
    & $runner -Elf $elf -ReadManyExe (Join-Path $mockDirectory 'MockReadMany.exe') -Python $python `
        -CobsRoot $mockCobs -Git (Join-Path $mockDirectory 'MockGit.exe') -Programmer $mockProgrammer `
        -ArmTools $mockDirectory -ExternalOutput $caseReceipt -ValidateOnly
    $receipt = Get-Content -LiteralPath (Join-Path $caseReceipt 'summary.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $calls = [IO.File]::ReadAllLines($env:TELEMETRY_MOCK_CALLS)
    if ($receipt.commands[0].tag -ne 'python-verifier-preflight' -or $receipt.commands[0].exit_code -ne 0 -or
        $receipt.stage -ne 'validated_without_hardware_access' -or $receipt.flash_attempted -or
        $receipt.restoration_required -or $null -ne $receipt.target -or $receipt.error -or
        $pnpBefore -cne [IO.File]::ReadAllText($pnpCallsPath) -or ($calls -cmatch ' -c | -l | -w | -u ')) {
        throw 'Successful interpreter validation performed target operations or omitted the native preflight.'
    }
    $results.Add([ordered]@{ mode = 'python-valid-validate'; passed = $true; runner_failed = $false;
        target_commands = 0; pnp_calls_added = 0 })
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
        if ($mode -eq 'readmany-pass') {
            $cli = @($receipt.commands | Where-Object { $_.tag -eq 'serial-read-many-check' })[0]
            $verifier = @($receipt.commands | Where-Object { $_.tag -eq 'verify-read-many-report' })[0]
            if ($cli.arguments -contains '--load-seconds' -or $verifier.arguments -contains '--minimum-load-seconds' -or
                $cli.timeout_seconds -ne 10 -or $receipt.PSObject.Properties.Name -contains 'read_many_load_seconds') {
                throw 'The default ReadMany path changed its legacy duration/argv.'
            }
        }
        if ($mode -eq 'readmany-timeout') {
            $timed = @($receipt.commands | Where-Object { $_.tag -eq 'serial-read-many-check' })
            $okay = $okay -and $timed.Count -eq 1 -and $timed[0].timed_out
            $stillRunning = Get-Process -Id $timed[0].pid -ErrorAction SilentlyContinue
            $okay = $okay -and -not $stillRunning
        }
        if ($mode -eq 'flashfail') { $okay = $okay -and -not ($calls -match 'tested-read-many.exe') }
        if (-not $okay) { throw "Offline ReadMany mock control failed: $mode" }
        foreach ($command in $receipt.commands) {
            if ($command.tag -in @('python-verifier-preflight', 'verify-read-many-report')) {
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
    foreach ($mode in @('readmany-load', 'readmany-load-missing')) {
        $case = Join-Path $output $mode
        [void][IO.Directory]::CreateDirectory($case)
        $env:TELEMETRY_MOCK_MODE = $mode
        $env:TELEMETRY_MOCK_FLASH = Join-Path $case 'FAKE-flash.bin'
        $env:TELEMETRY_MOCK_CALLS = Join-Path $case 'FAKE-calls.txt'
        [IO.File]::WriteAllBytes($env:TELEMETRY_MOCK_FLASH, $bytes)
        $before = (Get-FileHash -LiteralPath $env:TELEMETRY_MOCK_FLASH).Hash
        $caseReceipt = Join-Path $case 'receipt'
        $failed = $false
        try {
            & $runner -Elf $elf -ReadManyExe (Join-Path $mockDirectory 'MockReadMany.exe') -Python $python `
                -CobsRoot $mockCobs -Git (Join-Path $mockDirectory 'MockGit.exe') -Programmer $mockProgrammer `
                -ArmTools $mockDirectory -ExternalOutput $caseReceipt -ReadManyTimeoutSeconds 10 -ReadManyLoadSeconds 1800
        } catch { $failed = $true; Write-Host "Control $mode`: $($_.Exception.Message)" }
        $receipt = Get-Content -LiteralPath (Join-Path $caseReceipt 'summary.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        $cli = @($receipt.commands | Where-Object { $_.tag -eq 'serial-read-many-check' })
        $verifier = @($receipt.commands | Where-Object { $_.tag -eq 'verify-read-many-report' })
        if ($cli.Count -ne 1 -or $verifier.Count -ne 1 -or $receipt.read_many_load_seconds -ne 1800 -or
            $cli[0].timeout_seconds -ne 1980 -or @($cli[0].arguments | Where-Object { $_ -eq '--load-seconds' }).Count -ne 1 -or
            $cli[0].arguments[-2] -ne '--load-seconds' -or $cli[0].arguments[-1] -ne '1800' -or
            $verifier[0].arguments[-2] -ne '--minimum-load-seconds' -or $verifier[0].arguments[-1] -ne '1800' -or
            -not $receipt.restored_and_verified -or $before -ne (Get-FileHash -LiteralPath $env:TELEMETRY_MOCK_FLASH).Hash) {
            throw 'Load duration/timeout/verifier admission or restoration did not match the request.'
        }
        if ($mode -eq 'readmany-load') {
            if ($failed -or -not $receipt.qualification_passed) { throw 'Valid synthetic load report was rejected.' }
            & $python -B $readManyVerifier --receipt $caseReceipt --minimum-load-seconds 1800
            if ($LASTEXITCODE -ne 0) { throw 'Independent mock load receipt verification failed.' }
        } else {
            $reason = [IO.File]::ReadAllText((Join-Path $caseReceipt 'logs\verify-read-many-report.stderr.txt'))
            if (-not $failed -or $receipt.qualification_passed -or $verifier[0].exit_code -eq 0 -or
                -not $reason.Contains('load')) { throw 'A requested load accepted a report without load evidence.' }
        }
        $results.Add([ordered]@{ mode = $mode; passed = $true; runner_failed = $failed;
            simulated_flash_restored = $true; requested_load_seconds = 1800; process_budget_seconds = 1980 })
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
    # Exercise the public offline build entry point without invoking an ARM
    # compiler: the existing mock answers --version, then missing SDK headers
    # deliberately stop qualification before CMake or any build can run.
    $buildScript = Join-Path $repo 'examples\nucleo_h7s3l8\Build.ps1'
    $mockArm = Join-Path $mockDirectory 'STM32CubeIDE-offline\bin'
    [void][IO.Directory]::CreateDirectory($mockArm)
    foreach ($name in @('arm-none-eabi-gcc.exe', 'arm-none-eabi-g++.exe')) {
        Copy-Item -LiteralPath $mockProgrammer -Destination (Join-Path $mockArm $name)
    }
    $mockSdk = Join-Path $output 'mock-sdk-without-headers'
    [void][IO.Directory]::CreateDirectory($mockSdk)
    foreach ($mode in @('empty', 'nonempty')) {
        $case = Join-Path $output ('build-output-' + $mode)
        [void][IO.Directory]::CreateDirectory($case)
        $sentinel = Join-Path $case 'keep.txt'
        if ($mode -eq 'nonempty') { [IO.File]::WriteAllText($sentinel, 'preserve this output') }
        $env:TELEMETRY_MOCK_CALLS = Join-Path $output ('build-' + $mode + '-calls.txt')
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            & (Join-Path $PSHOME 'powershell.exe') -NoProfile -NonInteractive -ExecutionPolicy Bypass `
                -File $buildScript -OutputDirectory $case -CobsRoot $mockCobs -Stm32CubeRoot $mockSdk `
                -ArmGccBin $mockArm -CMake $mockProgrammer -Ninja $mockProgrammer 2>&1 |
                Out-File -LiteralPath (Join-Path $output ('build-' + $mode + '.log')) -Encoding UTF8
            $buildExit = $LASTEXITCODE
        } finally { $ErrorActionPreference = $previousPreference }
        if ($buildExit -eq 0) { throw 'Incomplete mock SDK unexpectedly built firmware.' }
        if ($mode -eq 'empty') {
            $buildReceipt = Get-Content -LiteralPath (Join-Path $case 'summary.json') -Raw -Encoding UTF8 | ConvertFrom-Json
            $calls = [IO.File]::ReadAllLines($env:TELEMETRY_MOCK_CALLS)
            if ($buildReceipt.passed -or $buildReceipt.hardware -or $buildReceipt.flash -or
                $buildReceipt.commands.Count -ne 1 -or $buildReceipt.commands[0].name -ne 'gcc-version' -or
                $buildReceipt.commands[0].exit -ne 0 -or $buildReceipt.commands[0].tool -ne (Join-Path $mockArm 'arm-none-eabi-gcc.exe') -or
                $calls.Count -ne 1 -or $calls[0] -ne 'arm-none-eabi-gcc.exe --version' -or
                -not $buildReceipt.error.Contains($mockSdk)) {
                throw 'Existing empty build output was not accepted before the deliberate SDK stop.'
            }
            $commandCount = 1
        } else {
            if ((Test-Path -LiteralPath $env:TELEMETRY_MOCK_CALLS) -or
                (Test-Path -LiteralPath (Join-Path $case 'summary.json')) -or
                [IO.File]::ReadAllText($sentinel) -ne 'preserve this output' -or
                @(Get-ChildItem -LiteralPath $case -Force).Count -ne 1) {
                throw 'Nonempty build output was modified or allowed to invoke a tool.'
            }
            $commandCount = 0
        }
        $results.Add([ordered]@{ mode = ('build-output-' + $mode); passed = $true;
            runner_failed = $true; command_count = $commandCount })
    }
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
        build_script_sha256 = (Get-FileHash -LiteralPath (Join-Path $repo 'examples\nucleo_h7s3l8\Build.ps1')).Hash.ToLowerInvariant()
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
Write-Host "Offline runner controls: $($results.Count)/46 passed; no physical hardware access."
