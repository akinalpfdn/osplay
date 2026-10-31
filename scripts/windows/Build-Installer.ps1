<#
.SYNOPSIS
    Builds the Windows installer, windows\build\installer\Osplay-Setup.exe.
.DESCRIPTION
    Builds the engine and the helper with the release preset and runs their tests, publishes the
    app self-contained, downloads the pinned Virtual Display Driver and UsbDk releases and checks
    their hashes, stages the Android accessory driver files and the license texts, and compiles
    windows\installer\Osplay.iss with Inno Setup.

    Run it from the repository root in a shell set up by scripts\windows\Enter-DevShell.ps1.
    Needs the .NET SDK from windows\app\global.json and Inno Setup 6 or 7 (winget install
    JRSoftware.InnoSetup). The release workflow runs this same script; see docs/RELEASING.md.
.PARAMETER SkipTests
    Skips the native tests, for a quick local rebuild of the installer only.
#>
[CmdletBinding()]
param(
    [switch] $SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue' # Invoke-WebRequest is many times slower with progress

# The Virtual Display Driver release bundled with Osplay (docs/THIRD_PARTY.md). Change all three
# together, after testing the new release with Osplay.
$VddRelease = '25.7.23'
$VddUrl = "https://github.com/VirtualDrivers/Virtual-Display-Driver/releases/download/$VddRelease/VirtualDisplayDriver-x86.Driver.Only.zip"
$VddSha256 = 'e24210692b442b39af763536330ce78b423f19342b7a7792c26de3944e418b3a'
$VddFiles = @('MttVDD.inf', 'mttvdd.cat', 'MttVDD.dll')

# The UsbDk release bundled with Osplay, which switches Android devices on the USB cable
# (docs/THIRD_PARTY.md). Change all four together; the ProductCode is the MSI's, which the
# uninstaller removes it by.
$UsbDkRelease = '1.0.22'
$UsbDkUrl = 'https://github.com/daynix/UsbDk/releases/download/v1.00-22/UsbDk_1.0.22_x64.msi'
$UsbDkSha256 = '91f6f695e1e13c656024e6d3b55620bf08d8835ef05ee0496935ba6bb62466a5'
$UsbDkProductCode = '{6D4A6ED0-CF41-4615-A4B3-BDA018C3C1CD}'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$windows = Join-Path $repo 'windows'
$nativeBin = Join-Path $windows 'build\release\bin'
$output = Join-Path $windows 'build\installer'
$stage = Join-Path $output 'stage'
$downloads = Join-Path $output 'downloads'

# Native commands do not stop the script on failure in Windows PowerShell 5.1.
function Invoke-Native {
    param([string] $Command, [string[]] $Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Find-InnoSetupCompiler {
    $command = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    # The script works with both; the release workflow pins Inno Setup 6.
    $candidates = foreach ($folder in @('Inno Setup 7', 'Inno Setup 6')) {
        Join-Path ${env:ProgramFiles(x86)} "$folder\ISCC.exe"
        Join-Path $env:ProgramFiles "$folder\ISCC.exe"
        Join-Path $env:LOCALAPPDATA "Programs\$folder\ISCC.exe"
    }
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    throw 'Inno Setup was not found. Install it with: winget install JRSoftware.InnoSetup'
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'The MSVC compiler is not on PATH. Run this first: . .\scripts\windows\Enter-DevShell.ps1'
}
$iscc = Find-InnoSetupCompiler

# One version for the whole release: the Windows app's, which scripts/release_version.py keeps
# equal to the Mac app's and the tag.
$version = ([xml](Get-Content (Join-Path $windows 'app\Directory.Build.props') -Raw)).Project.PropertyGroup.Version
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "windows\app\Directory.Build.props has no X.Y.Z <Version> (found '$version')"
}
Write-Host "==> Osplay $version"

Write-Host '==> Engine and helper (release preset)'
Push-Location $windows
try {
    Invoke-Native cmake @('--preset', 'release')
    Invoke-Native cmake @('--build', '--preset', 'release')
    if (-not $SkipTests) {
        Invoke-Native ctest @('--preset', 'release')
    }
}
finally {
    Pop-Location
}

if (Test-Path $stage) {
    Remove-Item $stage -Recurse -Force
}
$appStage = Join-Path $stage 'app'
$driverStage = Join-Path $stage 'driver'
$licenseStage = Join-Path $stage 'licenses'
$accessoryStage = Join-Path $stage 'accessory'
$usbDkStage = Join-Path $stage 'usbdk'
New-Item -ItemType Directory -Force -Path $appStage, $driverStage, $licenseStage, $accessoryStage,
    $usbDkStage, $downloads | Out-Null

Write-Host '==> App (self-contained, win-x64)'
Invoke-Native dotnet @(
    'publish', (Join-Path $windows 'app\Osplay.App\Osplay.App.csproj'),
    '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true',
    "-p:OsplayNativeBin=$nativeBin", '-o', $appStage
)
# The project copies the engine only when it exists, so a missing one must fail here.
foreach ($file in @('Osplay.exe', 'OsplayEngine.dll')) {
    if (-not (Test-Path (Join-Path $appStage $file))) {
        throw "The published app has no $file"
    }
}
Copy-Item (Join-Path $nativeBin 'OsplayHelper.exe') $appStage

Write-Host "==> Virtual Display Driver $VddRelease"
$zip = Join-Path $downloads "VirtualDisplayDriver-$VddRelease.zip"
if (-not (Test-Path $zip)) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $VddUrl -OutFile $zip -UseBasicParsing
}
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne $VddSha256) {
    Remove-Item $zip -Force
    throw "The Virtual Display Driver download has SHA-256 $hash, expected $VddSha256"
}
$extracted = Join-Path $downloads "VirtualDisplayDriver-$VddRelease"
if (Test-Path $extracted) {
    Remove-Item $extracted -Recurse -Force
}
Expand-Archive -Path $zip -DestinationPath $extracted
foreach ($file in $VddFiles) {
    $found = Get-ChildItem $extracted -Recurse -File -Filter $file | Select-Object -First 1
    if (-not $found) {
        throw "The Virtual Display Driver archive has no $file"
    }
    Copy-Item $found.FullName (Join-Path $driverStage $file)
}

Write-Host "==> UsbDk $UsbDkRelease"
$msi = Join-Path $downloads "UsbDk_$($UsbDkRelease)_x64.msi"
if (-not (Test-Path $msi)) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $UsbDkUrl -OutFile $msi -UseBasicParsing
}
$hash = (Get-FileHash $msi -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne $UsbDkSha256) {
    Remove-Item $msi -Force
    throw "The UsbDk download has SHA-256 $hash, expected $UsbDkSha256"
}
Copy-Item $msi (Join-Path $usbDkStage 'UsbDk.msi')

Write-Host '==> Android accessory driver'
# The catalog is made and signed on each PC at install time (AccessoryDriver.ps1).
Copy-Item (Join-Path $windows 'installer\accessory\*') $accessoryStage

Write-Host '==> Licenses'
$dotnetRoot = Split-Path (Get-Command dotnet).Source
$licenses = [ordered]@{
    'LICENSE.txt'                     = Join-Path $repo 'LICENSE'
    'Monocypher-LICENSE.txt'          = Join-Path $repo 'core\third_party\monocypher\LICENCE.md'
    'Asio-LICENSE.txt'                = Join-Path $repo 'core\third_party\asio\LICENSE_1_0.txt'
    'dotnet-LICENSE.txt'              = Join-Path $dotnetRoot 'LICENSE.txt'
    'dotnet-ThirdPartyNotices.txt'    = Join-Path $dotnetRoot 'ThirdPartyNotices.txt'
}
foreach ($entry in $licenses.GetEnumerator()) {
    if (-not (Test-Path $entry.Value)) {
        throw "License text not found: $($entry.Value)"
    }
    Copy-Item $entry.Value (Join-Path $licenseStage $entry.Key)
}
Copy-Item (Join-Path $windows 'installer\licenses\*.txt') $licenseStage

Write-Host '==> Installer'
Invoke-Native $iscc @(
    '/Qp', "/DAppVersion=$version", "/DStageDir=$stage", "/DOutputDir=$output",
    "/DUsbDkProductCode=$UsbDkProductCode",
    (Join-Path $windows 'installer\Osplay.iss')
)
$installer = Join-Path $output 'Osplay-Setup.exe'
# The installer is not code-signed, so the published hash is how a download can be checked.
$installerHash = (Get-FileHash $installer -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -Path "$installer.sha256" -Value "$installerHash  Osplay-Setup.exe" -Encoding ascii
Write-Host "Built $installer"
Write-Host "SHA-256 $installerHash"
