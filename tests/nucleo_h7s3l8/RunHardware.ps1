# Author: Ruslan Kovtun (shpegun60), codexAi
# SPDX-License-Identifier: MIT
# This runner programs only the explicitly identified NUCLEO, then restores it.
[CmdletBinding(DefaultParameterSetName = 'Inspector')]
param(
    [Parameter(Mandatory = $true)][string]$Elf,
    [Parameter(Mandatory = $true, ParameterSetName = 'Inspector')][string]$GuiExe,
    [Parameter(Mandatory = $true, ParameterSetName = 'ReadMany')][string]$ReadManyExe,
    [Parameter(Mandatory = $true, ParameterSetName = 'ReadMany')][string]$CobsRoot,
    [Parameter(ParameterSetName = 'ReadMany')][string]$Git = 'git',
    [Parameter(Mandatory = $true)][string]$ExternalOutput,
    [string]$Programmer,
    [string]$ArmTools,
    [ValidateSet('002A001F3033510135393935')]
    [string]$StLinkSerial = '002A001F3033510135393935',
    [ValidateSet('COM6')][string]$Port = 'COM6',
    [ValidateRange(10, 180)][int]$CommandTimeoutSeconds = 60,
    [ValidateRange(10, 180)][int]$GuiTimeoutSeconds = 60,
    [ValidateRange(10, 180)][int]$ReadManyTimeoutSeconds = 60,
    [string]$Python = 'python',
    [ValidateRange(1, 1000)][int]$MinimumValueRows = 3,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$readManyMode = $PSCmdlet.ParameterSetName -eq 'ReadMany'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output = [IO.Path]::GetFullPath($ExternalOutput)
if ($output.TrimEnd('\', '/') -eq $repo.TrimEnd('\', '/') -or
    $output.StartsWith($repo.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'ExternalOutput must be outside the Telemetry source tree.'
}
if (Test-Path -LiteralPath $output) {
    throw 'ExternalOutput must be a new directory; an earlier backup is never reused or overwritten.'
}
$testExecutable = if ($readManyMode) { $ReadManyExe } else { $GuiExe }
foreach ($inputPath in @($Elf, $testExecutable)) {
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
        throw "Input does not exist: $inputPath"
    }
}
$Elf = (Resolve-Path -LiteralPath $Elf).Path
$testExecutable = (Resolve-Path -LiteralPath $testExecutable).Path
if ($readManyMode) { $ReadManyExe = $testExecutable } else { $GuiExe = $testExecutable }
if ([IO.Path]::GetExtension($Elf) -ine '.elf' -or [IO.Path]::GetExtension($testExecutable) -ine '.exe') {
    throw 'Provide an explicit .elf firmware and .exe checker.'
}
$readManyVerifier = Join-Path $PSScriptRoot 'VerifyReadManyEvidence.py'
if ($readManyMode) {
    if (-not (Test-Path -LiteralPath $readManyVerifier -PathType Leaf)) { throw 'ReadMany verifier is missing.' }
    # Windows app-execution aliases are zero-byte reparse points, not a frozen
    # interpreter binary that can be hashed. Select an actual installed file.
    $pythonCandidates = @(Get-Command $Python -CommandType Application -ErrorAction Stop | Where-Object {
        (Get-Item -LiteralPath $_.Source -ErrorAction Stop).Length -gt 0 })
    if ($pythonCandidates.Count -eq 0) { throw 'Set -Python to an actual Python executable, not a Windows app alias.' }
    $Python = $pythonCandidates[0].Source
    $Git = (Get-Command $Git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $CobsRoot = (Resolve-Path -LiteralPath $CobsRoot).Path
    foreach ($directory in @('src', 'libs\spsc')) {
        if (-not (Test-Path -LiteralPath (Join-Path $CobsRoot $directory) -PathType Container)) {
            throw 'CobsRoot must explicitly identify a COBS checkout with src and libs/spsc.'
        }
    }
}
if (-not $Programmer) {
    $Programmer = 'C:\ST\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
}
if (-not (Test-Path -LiteralPath $Programmer -PathType Leaf)) {
    throw "CubeProgrammer does not exist: $Programmer"
}
$Programmer = (Resolve-Path -LiteralPath $Programmer).Path
if (-not $ArmTools) {
    $plugins = 'C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins'
    $candidates = @(Get-ChildItem -LiteralPath $plugins -Directory -Filter '*gnu-tools-for-stm32*' |
        Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'tools\bin' } |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ 'arm-none-eabi-objcopy.exe') })
    if ($candidates.Count -ne 1) {
        throw 'Set -ArmTools explicitly when the CubeIDE compiler directory is missing or ambiguous.'
    }
    $ArmTools = $candidates[0]
}
$ArmTools = (Resolve-Path -LiteralPath $ArmTools).Path
$readelf = Join-Path $ArmTools 'arm-none-eabi-readelf.exe'
$objcopy = Join-Path $ArmTools 'arm-none-eabi-objcopy.exe'
$compiler = Join-Path $ArmTools 'arm-none-eabi-g++.exe'
foreach ($tool in @($readelf, $objcopy, $compiler)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Tool is missing: $tool" }
}
[void][IO.Directory]::CreateDirectory($output)
$logs = Join-Path $output 'logs'
[void][IO.Directory]::CreateDirectory($logs)
$receiptPath = Join-Path $output 'summary.json'
$utf8 = New-Object Text.UTF8Encoding($false)
$script:receipt = [ordered]@{
    schema = 1
    profile = $(if ($readManyMode) { 'nucleo-h7s3l8-read-many' } else { 'nucleo-h7s3l8-demo' })
    started_utc = [DateTime]::UtcNow.ToString('o')
    expected_board = 'NUCLEO-H7S3L8'
    expected_device_id = '0x485'
    expected_serial = $StLinkSerial
    expected_port = $Port
    baud = 115200
    flash_start = '0x08000000'
    flash_bytes = 65536
    validate_only = [bool]$ValidateOnly
    commands = New-Object Collections.Generic.List[object]
    source_hashes = [ordered]@{}
    inputs = [ordered]@{}
    tools = [ordered]@{}
    target = $null
    backup = $null
    test_passed = $false
    flash_attempted = $false
    restoration_required = $false
    restored_and_verified = $false
    qualification_passed = $false
    error = $null
}

function Write-Receipt {
    [IO.File]::WriteAllText($receiptPath, ($script:receipt | ConvertTo-Json -Depth 12) + "`n", $utf8)
}
function Get-Digest([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Quote-WindowsArgument([string]$Value) {
    $builder = New-Object Text.StringBuilder
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) { ++$slashes; continue }
        if ($character -eq [char]34) {
            [void]$builder.Append([char]92, 2 * $slashes + 1)
            [void]$builder.Append([char]34)
        } else {
            [void]$builder.Append([char]92, $slashes)
            [void]$builder.Append($character)
        }
        $slashes = 0
    }
    [void]$builder.Append([char]92, 2 * $slashes)
    [void]$builder.Append('"')
    return $builder.ToString()
}
function Invoke-Recorded {
    param([string]$Tag, [string]$Executable, [string[]]$Arguments,
        [int]$TimeoutSeconds = $CommandTimeoutSeconds, [switch]$AllowFailure,
        [string]$RuntimeDirectory)
    $stdoutPath = Join-Path $logs ($Tag + '.stdout.txt')
    $stderrPath = Join-Path $logs ($Tag + '.stderr.txt')
    if (Test-Path -LiteralPath $stdoutPath) { throw "Duplicate command tag: $Tag" }
    $entry = [ordered]@{
        tag = $Tag; executable = $Executable; arguments = $Arguments
        started_utc = [DateTime]::UtcNow.ToString('o'); timeout_seconds = $TimeoutSeconds
        pid = $null; exit_code = $null; timed_out = $false
        stdout = ('logs/' + $Tag + '.stdout.txt'); stderr = ('logs/' + $Tag + '.stderr.txt')
    }
    $script:receipt.commands.Add($entry)
    Write-Receipt
    $process = New-Object Diagnostics.Process
    $process.StartInfo = New-Object Diagnostics.ProcessStartInfo
    $process.StartInfo.FileName = $Executable
    $process.StartInfo.Arguments = (($Arguments | ForEach-Object { Quote-WindowsArgument $_ }) -join ' ')
    $process.StartInfo.WorkingDirectory = [IO.Path]::GetDirectoryName($Executable)
    if ($RuntimeDirectory) {
        $process.StartInfo.WorkingDirectory = $RuntimeDirectory
        $process.StartInfo.EnvironmentVariables['PATH'] = $RuntimeDirectory + [IO.Path]::PathSeparator + $env:PATH
    }
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $stdout = ''; $stderr = ''; $startError = $null
    try {
        if (-not $process.Start()) { throw "Could not start $Executable" }
        $entry.pid = $process.Id
        Write-Receipt
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $entry.timed_out = $true
            # Kill this owned PID only; never stop processes by executable name.
            $process.Kill()
            if (-not $process.WaitForExit(5000)) { throw 'Owned process did not terminate after timeout.' }
        }
        $entry.exit_code = $process.ExitCode
        if ($stdoutTask.Wait(2000)) { $stdout = $stdoutTask.Result } else { $stderr += 'stdout pipe did not close.' }
        if ($stderrTask.Wait(2000)) { $stderr += $stderrTask.Result } else { $stderr += 'stderr pipe did not close.' }
    } catch {
        $startError = $_.Exception.Message
        $stderr += "`n$startError"
    } finally {
        $process.Dispose()
        [IO.File]::WriteAllText($stdoutPath, $stdout, $utf8)
        [IO.File]::WriteAllText($stderrPath, $stderr, $utf8)
        $entry.finished_utc = [DateTime]::UtcNow.ToString('o')
        $entry.stdout_sha256 = Get-Digest $stdoutPath
        $entry.stderr_sha256 = Get-Digest $stderrPath
        Write-Receipt
    }
    Write-Host "$Tag`: exit $($entry.exit_code), timeout=$($entry.timed_out)"
    $result = [pscustomobject]@{ ExitCode = $entry.exit_code; TimedOut = $entry.timed_out;
        Text = $stdout + "`n" + $stderr; Started = ($null -ne $entry.pid) }
    if (-not $AllowFailure -and ($startError -or $entry.timed_out -or $entry.exit_code -ne 0)) {
        throw "Command $Tag failed; see $stdoutPath and $stderrPath"
    }
    return $result
}
function Assert-Target([string]$Text) {
    if ($Text -notmatch ('ST-LINK SN\s*:\s*' + [regex]::Escape($StLinkSerial) + '\b') -or
        $Text -notmatch 'Board\s*:\s*NUCLEO-H7S3L8\b' -or
        $Text -notmatch 'Device ID\s*:\s*0x0?485\b' -or
        $Text -notmatch 'Device name\s*:\s*STM32H7RSxx\b' -or
        $Text -notmatch 'Flash size\s*:\s*64\s*KBytes\b') {
        throw 'Target identity does not match the fixed NUCLEO-H7S3L8/0x485/64-KiB profile.'
    }
}
function Assert-SerialIdentity {
    $ports = @(Get-PnpDevice -Class Ports -PresentOnly | Where-Object {
        $_.FriendlyName -match ('\(' + [regex]::Escape($Port) + '\)$') })
    if ($ports.Count -ne 1 -or $ports[0].Status -ne 'OK' -or
        $ports[0].InstanceId -notmatch '^USB\\VID_0483&PID_3754&MI_01\\') {
        throw 'COM6 is not the unique present ST-LINK VCP with the expected VID/PID/interface.'
    }
    $chain = New-Object Collections.Generic.List[object]
    $id = $ports[0].InstanceId
    $matched = $false
    $expectedParent = 'USB\VID_0483&PID_3754\' + $StLinkSerial
    for ($depth = 0; $depth -lt 8 -and $id; ++$depth) {
        $device = Get-PnpDevice -InstanceId $id -ErrorAction Stop
        $chain.Add([ordered]@{ instance_id = $id; name = $device.FriendlyName })
        if ($id -ieq $expectedParent) { $matched = $true; break }
        $id = [string](Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_Parent -ErrorAction Stop).Data
    }
    $script:receipt.serial_pnp_chain = @($chain.ToArray())
    Write-Receipt
    if (-not $matched) { throw 'COM6 USB parent does not match the explicitly selected ST-LINK serial.' }
}
function Assert-Elf([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 52 -or $bytes[0] -ne 0x7f -or $bytes[1] -ne 0x45 -or
        $bytes[2] -ne 0x4c -or $bytes[3] -ne 0x46 -or $bytes[4] -ne 1 -or
        $bytes[5] -ne 1 -or $bytes[6] -ne 1 -or
        [BitConverter]::ToUInt16($bytes, 16) -ne 2 -or [BitConverter]::ToUInt16($bytes, 18) -ne 40) {
        throw 'Expected an ELF32 little-endian ARM executable, not a relocatable object or another architecture.'
    }
    $entry = [uint64][BitConverter]::ToUInt32($bytes, 24)
    if (($entry -band 0xfffffffeL) -lt 0x08000000L -or ($entry -band 0xfffffffeL) -ge 0x08010000L) {
        throw 'ELF entry point is outside the backed-up internal flash.'
    }
    $phOffset = [uint64][BitConverter]::ToUInt32($bytes, 28)
    $shOffset = [uint64][BitConverter]::ToUInt32($bytes, 32)
    $phSize = [uint64][BitConverter]::ToUInt16($bytes, 42)
    $phCount = [uint64][BitConverter]::ToUInt16($bytes, 44)
    $shSize = [uint64][BitConverter]::ToUInt16($bytes, 46)
    $shCount = [uint64][BitConverter]::ToUInt16($bytes, 48)
    if ($phSize -ne 32 -or $phCount -eq 0 -or $phOffset + $phSize * $phCount -gt $bytes.Length -or
        $shSize -ne 40 -or $shCount -eq 0 -or $shOffset + $shSize * $shCount -gt $bytes.Length) {
        throw 'Invalid or unsupported ELF program/section table.'
    }
    $loads = New-Object Collections.Generic.List[object]
    for ($i = 0; $i -lt $phCount; ++$i) {
        $at = [int]($phOffset + $i * $phSize)
        if ([BitConverter]::ToUInt32($bytes, $at) -ne 1) { continue }
        $offset = [uint64][BitConverter]::ToUInt32($bytes, $at + 4)
        $virtual = [uint64][BitConverter]::ToUInt32($bytes, $at + 8)
        $physical = [uint64][BitConverter]::ToUInt32($bytes, $at + 12)
        $fileSize = [uint64][BitConverter]::ToUInt32($bytes, $at + 16)
        $memorySize = [uint64][BitConverter]::ToUInt32($bytes, $at + 20)
        if ($fileSize -eq 0) { continue } # NOBITS RAM is not programmed.
        if ($memorySize -lt $fileSize -or $offset + $fileSize -gt $bytes.Length -or
            $physical -lt 0x08000000L -or $physical + $fileSize -gt 0x08010000L) {
            throw 'A file-backed ELF LOAD lies outside 0x08000000..0x08010000 or has invalid bounds.'
        }
        foreach ($previous in $loads) {
            if ($physical -lt $previous.physical + $previous.bytes -and
                $previous.physical -lt $physical + $fileSize) { throw 'Overlapping ELF physical LOADs.' }
        }
        $loads.Add([ordered]@{ index = $i; offset = $offset; virtual = $virtual;
            physical = $physical; bytes = $fileSize; memory_bytes = $memorySize })
    }
    $minimumPhysical = 0xffffffffL
    foreach ($load in $loads) {
        if ($load.physical -lt $minimumPhysical) { $minimumPhysical = $load.physical }
    }
    if ($loads.Count -eq 0 -or $minimumPhysical -ne 0x08000000L) {
        throw 'Firmware must have a file-backed vector LOAD beginning at 0x08000000.'
    }
    for ($i = 0; $i -lt $shCount; ++$i) {
        $at = [int]($shOffset + $i * $shSize)
        $type = [BitConverter]::ToUInt32($bytes, $at + 4)
        $flags = [BitConverter]::ToUInt32($bytes, $at + 8)
        $address = [uint64][BitConverter]::ToUInt32($bytes, $at + 12)
        $offset = [uint64][BitConverter]::ToUInt32($bytes, $at + 16)
        $size = [uint64][BitConverter]::ToUInt32($bytes, $at + 20)
        if (($flags -band 2) -eq 0 -or $size -eq 0 -or $type -eq 8) { continue }
        $covered = $false
        foreach ($load in $loads) {
            if ($offset -ge $load.offset -and $offset + $size -le $load.offset + $load.bytes -and
                $address -ge $load.virtual -and $address + $size -le $load.virtual + $load.bytes -and
                $offset - $load.offset -eq $address - $load.virtual) { $covered = $true; break }
        }
        if (-not $covered) { throw 'An allocated file-backed ELF section is not covered by a validated LOAD.' }
    }
    return @($loads.ToArray())
}

function Get-SourceDigests {
    $hashes = [ordered]@{}
    foreach ($sourceRoot in @('lib', 'cmake', 'examples\nucleo_h7s3l8', 'examples\inspector_serial',
            'examples\inspector_qml', 'examples\read_many_uart', 'tests\nucleo_h7s3l8')) {
        $sourceDirectory = Join-Path $repo $sourceRoot
        if (-not (Test-Path -LiteralPath $sourceDirectory)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $sourceDirectory -Recurse -File | Sort-Object FullName | Where-Object {
            $_.Extension -in '.hpp', '.h', '.cpp', '.c', '.s', '.ld', '.ps1', '.py', '.md', '.pri', '.cmake', '.txt', '.json' -and
            $_.FullName -notmatch '[\\/](build[^\\/]*|out|Debug[^\\/]*|Release|__pycache__)[\\/]' }) {
            $relative = $file.FullName.Substring($repo.Length + 1).Replace('\', '/')
            $hashes[$relative] = Get-Digest $file.FullName
        }
    }
    if ($readManyMode) {
        foreach ($relativeRoot in @('src', 'libs\spsc')) {
            foreach ($file in Get-ChildItem -LiteralPath (Join-Path $CobsRoot $relativeRoot) -Recurse -File |
                    Sort-Object FullName | Where-Object {
                $_.Extension -in '.hpp', '.h', '.cpp', '.c', '.pri', '.cmake', '.txt' -and
                $_.FullName -notmatch '[\\/](build[^\\/]*|out|Debug[^\\/]*|Release|__pycache__)[\\/]' }) {
                $relative = $file.FullName.Substring($CobsRoot.Length + 1).Replace('\', '/')
                $hashes['@cobs/' + $relative] = Get-Digest $file.FullName
            }
        }
    }
    return $hashes
}

$backupPath = Join-Path $output 'original-08000000-08010000.bin'
$imageElf = Join-Path $output 'tested-firmware.elf'
$imageBin = Join-Path $output 'tested-firmware.bin'
$frozenCli = Join-Path $output 'tested-read-many.exe'
$backupReady = $false
$connection = @('-c', 'port=SWD', "sn=$StLinkSerial", 'mode=UR', 'reset=HWrst', 'freq=4000')
try {
    $script:receipt.inputs.elf = [ordered]@{ path = $Elf; sha256 = Get-Digest $Elf }
    if ($readManyMode) {
        $script:receipt.inputs.read_many = [ordered]@{ path = $ReadManyExe; sha256 = Get-Digest $ReadManyExe }
        Copy-Item -LiteralPath $ReadManyExe -Destination $frozenCli
        $script:receipt.inputs.frozen_cli_path = 'tested-read-many.exe'
        $script:receipt.inputs.frozen_cli_sha256 = Get-Digest $frozenCli
        if ($script:receipt.inputs.frozen_cli_sha256 -ne $script:receipt.inputs.read_many.sha256) {
            throw 'ReadMany CLI changed during snapshot.'
        }
        $script:receipt.tools[$Python] = Get-Digest $Python
        $script:receipt.tools[$Git] = Get-Digest $Git
    } else {
        $script:receipt.inputs.gui = [ordered]@{ path = $GuiExe; sha256 = Get-Digest $GuiExe }
    }
    foreach ($tool in @($Programmer, $readelf, $objcopy, $compiler)) {
        $script:receipt.tools[$tool] = Get-Digest $tool
    }
    $script:receipt.source_hashes = Get-SourceDigests
    Copy-Item -LiteralPath $Elf -Destination $imageElf
    if ((Get-Digest $imageElf) -ne $script:receipt.inputs.elf.sha256) { throw 'ELF changed during snapshot.' }
    $script:receipt.elf_loads = @(Assert-Elf $imageElf)
    Write-Receipt
    if ($readManyMode) {
        $cobsHead = Invoke-Recorded 'cobs-head' $Git @('-C', $CobsRoot, 'rev-parse', 'HEAD')
        $script:receipt.cobs_head = $cobsHead.Text.Trim()
        $script:receipt.cobs_root = $CobsRoot
        if ($script:receipt.cobs_head -notmatch '^[0-9a-f]{40}$') { throw 'COBS HEAD identity is invalid.' }
    }
    [void](Invoke-Recorded 'programmer-version' $Programmer @('--version'))
    [void](Invoke-Recorded 'compiler-version' $compiler @('--version'))
    [void](Invoke-Recorded 'elf-program-headers' $readelf @('-hlW', $imageElf))
    [void](Invoke-Recorded 'image-binary' $objcopy @('-O', 'binary', '--gap-fill', '0xff', $imageElf, $imageBin))
    $imageBytes = (Get-Item -LiteralPath $imageBin).Length
    if ($imageBytes -lt 8 -or $imageBytes -gt 65536) { throw 'Converted firmware does not fit the backed-up flash.' }
    $script:receipt.inputs.frozen_elf_sha256 = Get-Digest $imageElf
    $script:receipt.inputs.frozen_bin_sha256 = Get-Digest $imageBin
    $script:receipt.inputs.frozen_bin_bytes = $imageBytes
    if ($ValidateOnly) {
        $script:receipt.stage = 'validated_without_hardware_access'
    } else {
        Assert-SerialIdentity
        $listed = Invoke-Recorded 'list-stlink-only' $Programmer @('-l', 'stlink-only')
        $serialMatches = [regex]::Matches($listed.Text, 'ST-LINK SN\s*:\s*(\S+)')
        if (@($serialMatches | Where-Object { $_.Groups[1].Value -eq $StLinkSerial }).Count -ne 1) {
            throw 'The exact ST-LINK serial is not uniquely enumerated.'
        }
        $identity = Invoke-Recorded 'target-identity-hotplug' $Programmer @('-c', 'port=SWD',
            "sn=$StLinkSerial", 'mode=HOTPLUG', 'freq=4000')
        Assert-Target $identity.Text
        $script:receipt.target = [ordered]@{ serial = $StLinkSerial; board = 'NUCLEO-H7S3L8';
            device_id = '0x485'; flash_bytes = 65536; port = $Port }
        Write-Receipt
        # From this point the finally block restores the original image after any test failure.
        $backup = Invoke-Recorded 'backup-internal-flash' $Programmer ($connection + @('-u',
            '0x08000000', '0x10000', $backupPath, '-rst'))
        Assert-Target $backup.Text
        if (-not (Test-Path -LiteralPath $backupPath -PathType Leaf) -or
            (Get-Item -LiteralPath $backupPath).Length -ne 65536) { throw 'Complete 64-KiB backup is required.' }
        $script:receipt.backup = [ordered]@{ path = [IO.Path]::GetFileName($backupPath);
            bytes = 65536; sha256 = Get-Digest $backupPath }
        $backupReady = $true
        $script:receipt.restoration_required = $true
        Write-Receipt
        Assert-SerialIdentity
        if ((Get-Digest $imageBin) -ne $script:receipt.inputs.frozen_bin_sha256) { throw 'Test image changed after validation.' }
        if ($readManyMode) {
            if ((Get-Digest $frozenCli) -ne $script:receipt.inputs.frozen_cli_sha256 -or
                (Get-Digest $ReadManyExe) -ne $script:receipt.inputs.read_many.sha256) { throw 'ReadMany CLI changed after validation.' }
        } elseif ((Get-Digest $GuiExe) -ne $script:receipt.inputs.gui.sha256) { throw 'GUI changed after validation.' }
        $script:receipt.flash_attempted = $true
        Write-Receipt
        $flashed = Invoke-Recorded 'flash-test-image' $Programmer ($connection + @('-w',
            $imageBin, '0x08000000', '-v', '-rst'))
        Assert-Target $flashed.Text
        if ($flashed.Text -notmatch 'Download verified successfully') { throw 'Test image verification was not confirmed.' }
        Start-Sleep -Milliseconds 500
        if ($readManyMode) {
            $readManyReportPath = Join-Path $output 'read-many-report.json'
            $verificationPath = Join-Path $output 'read-many-verification.json'
            [void](Invoke-Recorded 'serial-read-many-check' $frozenCli @('--serial', $Port, '--baud', '115200',
                '--report', $readManyReportPath) -TimeoutSeconds $ReadManyTimeoutSeconds `
                -RuntimeDirectory ([IO.Path]::GetDirectoryName($ReadManyExe)))
            if (-not (Test-Path -LiteralPath $readManyReportPath -PathType Leaf)) { throw 'ReadMany report is missing.' }
            [void](Invoke-Recorded 'verify-read-many-report' $Python @('-B', $readManyVerifier,
                '--report', $readManyReportPath, '--port', $Port, '--output', $verificationPath))
            if ((Get-Digest $frozenCli) -ne $script:receipt.inputs.frozen_cli_sha256 -or
                (Get-Digest $ReadManyExe) -ne $script:receipt.inputs.read_many.sha256) { throw 'ReadMany CLI changed during execution.' }
            $script:receipt.read_many_check = [ordered]@{ path = 'read-many-report.json';
                sha256 = Get-Digest $readManyReportPath; verification_path = 'read-many-verification.json';
                verification_sha256 = Get-Digest $verificationPath }
            $script:receipt.test_passed = $true
            Write-Receipt
        } else {
        $deviceReportPath = Join-Path $output 'device-report.json'
        [void](Invoke-Recorded 'serial-device-check' $GuiExe @('--serial', $Port, '--baud', '115200',
            '--check-device', '--report', $deviceReportPath) -TimeoutSeconds $GuiTimeoutSeconds)
        if (-not (Test-Path -LiteralPath $deviceReportPath -PathType Leaf)) { throw 'Headless device report is missing.' }
        $deviceReport = Get-Content -LiteralPath $deviceReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($flag in @('passed', 'voltage', 'exactCounter', 'demonstration')) {
            if ($deviceReport.$flag -isnot [bool] -or -not $deviceReport.$flag) {
                throw "Headless device check did not establish $flag."
            }
        }
        if ($deviceReport.profile -ne 'nucleo-h7s3l8-demo' -or $deviceReport.port -ine $Port -or
            $deviceReport.baud -ne 115200 -or @($deviceReport.fields).Count -lt 3 -or
            $deviceReport.scalarCount -lt 9) { throw 'Headless device report has the wrong profile or incomplete fields.' }
        $counterValues = @($deviceReport.fields | ForEach-Object { $_.values } | Where-Object {
            $_.value -is [string] -and $_.value -ceq '18446744073709551615' })
        if ($counterValues.Count -lt 1) { throw 'Headless report does not preserve the exact U64 Counter string.' }
        $script:receipt.device_check = [ordered]@{ path = 'device-report.json';
            sha256 = Get-Digest $deviceReportPath; field_count = @($deviceReport.fields).Count;
            scalar_count = $deviceReport.scalarCount; voltage = $true; exact_counter = $true;
            demonstration = $true }
        Write-Receipt
        # The successful checker completed and closed its port. The GUI now performs
        # its own full bootstrap on the same ordered, stateless, read-only peer.
        $screenshot = Join-Path $output 'serial-inspector.png'
        $gui = Invoke-Recorded 'serial-gui-smoke' $GuiExe @('--serial', $Port, '--baud', '115200',
            '--smoke', '--screenshot', $screenshot) -TimeoutSeconds $GuiTimeoutSeconds
        $marker = [regex]::Match($gui.Text,
            'Inspector QML smoke: published=1 roots=1 endpointRows=(\d+) valueRows=(\d+)')
        if (-not $marker.Success -or [int]$marker.Groups[1].Value -lt 1 -or
            [int]$marker.Groups[2].Value -lt $MinimumValueRows) { throw 'GUI did not confirm actual endpoint/value delegates.' }
        if (-not (Test-Path -LiteralPath $screenshot -PathType Leaf)) { throw 'GUI screenshot is missing.' }
        $png = [IO.File]::ReadAllBytes($screenshot)
        if ($png.Length -lt 100 -or [BitConverter]::ToString($png, 0, 8) -ne '89-50-4E-47-0D-0A-1A-0A') {
            throw 'GUI screenshot is not a nonempty PNG.'
        }
        $script:receipt.screenshot = [ordered]@{ path = 'serial-inspector.png'; sha256 = Get-Digest $screenshot;
            endpoint_rows = [int]$marker.Groups[1].Value; value_rows = [int]$marker.Groups[2].Value }
        $script:receipt.test_passed = $true
        }
    }
} catch {
    $script:receipt.error = $_.Exception.Message
    $script:receipt.error_position = $_.InvocationInfo.PositionMessage
    $script:receipt.error_stack = $_.ScriptStackTrace
} finally {
    if ($backupReady) {
        $restored = $false
        for ($attempt = 1; $attempt -le 3; ++$attempt) {
            try {
                if ((Get-Digest $backupPath) -ne $script:receipt.backup.sha256) { throw 'Original backup hash changed; refusing corrupted restore.' }
                $restore = Invoke-Recorded "restore-original-$attempt" $Programmer ($connection + @('-w',
                    $backupPath, '0x08000000', '-v', '-rst')) -AllowFailure
                Assert-Target $restore.Text
                if ($restore.ExitCode -eq 0 -and -not $restore.TimedOut -and
                    $restore.Text -match 'Download verified successfully') {
                    $readbackPath = Join-Path $output ("restored-readback-$attempt.bin")
                    $readback = Invoke-Recorded "verify-original-readback-$attempt" $Programmer ($connection + @('-u',
                        '0x08000000', '0x10000', $readbackPath, '-rst')) -AllowFailure
                    Assert-Target $readback.Text
                    if ($readback.ExitCode -eq 0 -and -not $readback.TimedOut -and
                        (Test-Path -LiteralPath $readbackPath -PathType Leaf) -and
                        (Get-Item -LiteralPath $readbackPath).Length -eq 65536) {
                        $actualHash = Get-Digest $readbackPath
                        $script:receipt.restored_readback_sha256 = $actualHash
                        if ($actualHash -eq $script:receipt.backup.sha256) { $restored = $true; break }
                    }
                }
            } catch {
                $script:receipt.restore_error = $_.Exception.Message
            }
            if ($attempt -lt 3) { Start-Sleep -Milliseconds 500 }
        }
        $script:receipt.restored_and_verified = $restored
        if (-not $restored) {
            $script:receipt.error = 'ORIGINAL FIRMWARE RESTORATION NOT VERIFIED. Preserve the backup and stop hardware testing.'
        }
    }
    if ($readManyMode) {
        try {
            if ($script:receipt.Contains('cobs_head')) {
                $cobsHeadAfter = Invoke-Recorded 'cobs-head-after' $Git @('-C', $CobsRoot, 'rev-parse', 'HEAD')
                $script:receipt.cobs_head_after = $cobsHeadAfter.Text.Trim()
                if ($script:receipt.cobs_head_after -cne $script:receipt.cobs_head) { throw 'COBS HEAD identity changed.' }
            }
            $script:receipt.source_hashes_after = Get-SourceDigests
            $script:receipt.source_inputs_unchanged =
                ($script:receipt.source_hashes | ConvertTo-Json -Compress) -ceq
                ($script:receipt.source_hashes_after | ConvertTo-Json -Compress)
            if (-not $script:receipt.source_inputs_unchanged -and -not $script:receipt.error) {
                $script:receipt.error = 'ReadMany source inputs changed during qualification.'
            }
        } catch {
            $script:receipt.source_inputs_unchanged = $false
            if (-not $script:receipt.error) { $script:receipt.error = 'ReadMany source inventory could not be verified: ' + $_.Exception.Message }
        }
    }
    $script:receipt.finished_utc = [DateTime]::UtcNow.ToString('o')
    $script:receipt.qualification_passed = -not $ValidateOnly -and $script:receipt.test_passed -and
        $script:receipt.restored_and_verified -and -not $script:receipt.error
    $script:receipt.evidence_hashes = [ordered]@{}
    foreach ($evidence in Get-ChildItem -LiteralPath $output -Recurse -File |
        Where-Object { $_.FullName -ne $receiptPath }) {
        $relative = $evidence.FullName.Substring($output.Length + 1).Replace('\', '/')
        $script:receipt.evidence_hashes[$relative] = Get-Digest $evidence.FullName
    }
    Write-Receipt
}
Write-Host "Receipt: $receiptPath"
if ($script:receipt.error) { throw $script:receipt.error }
if (-not $ValidateOnly -and -not $script:receipt.qualification_passed) { throw 'Hardware qualification did not pass.' }
