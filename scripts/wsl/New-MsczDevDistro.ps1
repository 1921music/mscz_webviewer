<#
.SYNOPSIS
    Creates (or recreates) the disposable 'mscz_dev' WSL2 distribution used to
    build the MuseScore-derived render core.

.DESCRIPTION
    The distro is meant to be thrown away and rebuilt at any time. Nothing is
    installed on the Windows host: the C++ toolchain and Qt6 come from apt
    inside the distro, and everything else comes from mise inside the distro.

    Pair with scripts/wsl/provision.sh, which does the work inside the distro.

.PARAMETER Name
    WSL distribution name. Default 'mscz_dev'.

.PARAMETER BaseDistro
    Base image from 'wsl --list --online'. Default 'Ubuntu-26.04', which ships
    Qt 6.10. aria needs Qt >= 6.8 (muse_global uses QDirListing, new in 6.8),
    so Ubuntu 24.04 LTS and its Qt 6.4 are too old.

.PARAMETER CorporateCA
    'Auto' (default) exports the Windows root CAs and lets the distro install
    them only if plain HTTPS turns out to be intercepted. 'Never' skips the
    export, for a network with no intercepting proxy.

.PARAMETER Location
    Where the distro's VHD lives. Default "$env:LOCALAPPDATA\WSL\<Name>".

.PARAMETER DevUser
    Unprivileged user created inside the distro. Default 'dev'.

.PARAMETER Force
    Unregister an existing distro of the same name first. This destroys its
    filesystem, including anything cloned inside it.

.PARAMETER DestroyOnly
    Unregister the distro and stop, without recreating it.

.PARAMETER SkipProvision
    Register the distro but do not run provision.sh.

.PARAMETER ProvisionOnly
    Re-run provision.sh against the existing distro without recreating it.
    Useful when provisioning failed partway. provision.sh is idempotent.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\wsl\New-MsczDevDistro.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\wsl\New-MsczDevDistro.ps1 -Force

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\wsl\New-MsczDevDistro.ps1 -DestroyOnly
#>
[CmdletBinding()]
param(
    [string] $Name = 'mscz_dev',
    [string] $BaseDistro = 'Ubuntu-26.04',
    [string] $Location,
    [string] $DevUser = 'dev',
    [ValidateSet('Auto', 'Never')]
    [string] $CorporateCA = 'Auto',
    [switch] $Force,
    [switch] $DestroyOnly,
    [switch] $SkipProvision,
    [switch] $ProvisionOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $Location) {
    $Location = Join-Path $env:LOCALAPPDATA "WSL\$Name"
}

function Write-Step([string] $Message) {
    Write-Host ''
    Write-Host "== $Message" -ForegroundColor Cyan
}

function Invoke-Wsl {
    # wsl.exe writes UTF-16LE; decode it so output is readable and parsable.
    param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $WslArgs)

    $previous = [Console]::OutputEncoding
    try {
        [Console]::OutputEncoding = [System.Text.Encoding]::Unicode
        & wsl.exe @WslArgs
    }
    finally {
        [Console]::OutputEncoding = $previous
    }
}

function Export-WindowsRootCAs([string] $TargetDir) {
    # Where TLS is terminated by an intercepting proxy, a fresh distro cannot
    # reach any https:// host until it trusts the same roots Windows does.
    # Exports the whole machine trust store rather than looking for any
    # particular issuer, so this works on any such network. One PEM file per
    # certificate, because that is what update-ca-certificates expects.
    New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
    Get-ChildItem -Path $TargetDir -Filter '*.crt' | Remove-Item -Force

    $count = 0
    foreach ($storeName in @('Root', 'CA')) {
        foreach ($cert in Get-ChildItem "Cert:\LocalMachine\$storeName") {
            $base64 = [System.Convert]::ToBase64String($cert.RawData, 'InsertLineBreaks')
            $pem = "-----BEGIN CERTIFICATE-----`n" + ($base64 -replace "`r`n", "`n").TrimEnd() + "`n-----END CERTIFICATE-----`n"
            $file = Join-Path $TargetDir "win-$storeName-$($cert.Thumbprint).crt"
            [System.IO.File]::WriteAllText($file, $pem, (New-Object System.Text.UTF8Encoding $false))
            $count++
        }
    }
    Write-Host "Exported $count certificate(s) to $TargetDir"
}

function ConvertTo-WslPath([string] $WindowsPath) {
    # Done here rather than by calling wslpath, because backslashes do not
    # survive the trip through wsl.exe's argument handling.
    $full = [System.IO.Path]::GetFullPath($WindowsPath)
    if ($full -notmatch '^([A-Za-z]):\\(.*)$') {
        throw "Cannot translate to a WSL path (expected a local drive): $WindowsPath"
    }
    $drive = $Matches[1].ToLowerInvariant()
    $rest = $Matches[2] -replace '\\', '/'
    return "/mnt/$drive/$rest"
}

function Test-DistroExists([string] $DistroName) {
    $names = Invoke-Wsl '--list' '--quiet'
    if (-not $names) { return $false }
    foreach ($line in $names) {
        if ($line.Trim() -eq $DistroName) { return $true }
    }
    return $false
}

# --- preflight ---------------------------------------------------------------

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'wsl.exe not found. Install WSL first (wsl --install).'
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$provisionScript = Join-Path $scriptDir 'provision.sh'
if (-not $SkipProvision -and -not (Test-Path $provisionScript)) {
    throw "provision.sh not found next to this script: $provisionScript"
}

# --- destroy ----------------------------------------------------------------

$distroExists = Test-DistroExists $Name

if ($ProvisionOnly) {
    if (-not $distroExists) { throw "Distro '$Name' does not exist; drop -ProvisionOnly to create it." }
    if ($Force -or $DestroyOnly -or $SkipProvision) { throw '-ProvisionOnly cannot be combined with -Force, -DestroyOnly or -SkipProvision.' }
}

if ($distroExists -and -not $ProvisionOnly) {
    if (-not ($Force -or $DestroyOnly)) {
        throw "Distro '$Name' already exists. Pass -Force to destroy and recreate it, -ProvisionOnly to re-run provisioning in place, or -DestroyOnly to just remove it."
    }

    Write-Step "Unregistering existing distro '$Name'"
    Invoke-Wsl '--terminate' $Name | Out-Null
    Invoke-Wsl '--unregister' $Name
    if ($LASTEXITCODE -ne 0) { throw "wsl --unregister $Name failed ($LASTEXITCODE)" }
}
elseif ($DestroyOnly) {
    Write-Host "Distro '$Name' does not exist; nothing to destroy."
}

if ($DestroyOnly) {
    Write-Step 'Done'
    return
}

# --- register ---------------------------------------------------------------

if ($ProvisionOnly) {
    Write-Step "Reusing the existing '$Name' distro"
}
else {
    Write-Step "Registering '$Name' from $BaseDistro at $Location"
    New-Item -ItemType Directory -Force -Path $Location | Out-Null
    Invoke-Wsl '--install' $BaseDistro '--name' $Name '--location' $Location '--no-launch'
    if ($LASTEXITCODE -ne 0) { throw "wsl --install failed ($LASTEXITCODE)" }
}

if ($SkipProvision) {
    Write-Step 'Registered without provisioning'
    Write-Host "Run provisioning later with:"
    Write-Host "  wsl -d $Name -u root -- bash /tmp/provision.sh $DevUser"
    return
}

# --- provision --------------------------------------------------------------

Write-Step 'Copying provision.sh into the distro'
$provisionWslPath = ConvertTo-WslPath $provisionScript

# Strip CR so the script runs even though it came off an NTFS checkout.
Invoke-Wsl '-d' $Name '-u' 'root' '--' 'sh' '-c' "sed 's/\r`$//' '$provisionWslPath' > /tmp/provision.sh"
if ($LASTEXITCODE -ne 0) { throw 'Could not copy provision.sh into the distro.' }

# Literal 'none' rather than '', because an empty argument to a native exe is
# not reliably passed through.
$caWslPath = 'none'
if ($CorporateCA -eq 'Auto') {
    # Exported unconditionally because it is cheap and local. provision.sh
    # probes HTTPS and only installs them if the network actually intercepts.
    Write-Step 'Exporting Windows root CAs (used only if HTTPS is intercepted)'
    $caDir = Join-Path $env:TEMP "$Name-ca"
    Export-WindowsRootCAs $caDir
    $caWslPath = ConvertTo-WslPath $caDir
}
else {
    Write-Step 'Skipping Windows root CA export (-CorporateCA Never)'
}

Write-Step 'Provisioning (this takes a while: apt, then mise tools)'
Invoke-Wsl '-d' $Name '-u' 'root' '--' 'bash' '/tmp/provision.sh' $DevUser $caWslPath
if ($LASTEXITCODE -ne 0) { throw "provision.sh failed ($LASTEXITCODE)" }

# Restart so [user] default from /etc/wsl.conf takes effect.
Write-Step 'Restarting the distro so the default user applies'
Invoke-Wsl '--terminate' $Name | Out-Null

Write-Step 'Ready'
Write-Host "Open a shell:        wsl -d $Name"
Write-Host "Run one command:     wsl -d $Name -- bash -lc 'mise exec -- cmake --version'"
Write-Host "Destroy:             powershell -ExecutionPolicy Bypass -File scripts\wsl\New-MsczDevDistro.ps1 -DestroyOnly"
Write-Host "Rebuild from zero:   powershell -ExecutionPolicy Bypass -File scripts\wsl\New-MsczDevDistro.ps1 -Force"
