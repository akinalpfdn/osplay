<#
.SYNOPSIS
    Puts the current PowerShell session into a Visual Studio x64 developer environment.
.DESCRIPTION
    Finds the newest Visual Studio with the C++ x64 toolset through vswhere, so builds work with
    any installed Visual Studio version. Not needed inside "Developer PowerShell for VS".
#>
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio Installer was not found. Install Visual Studio with the "Desktop development with C++" workload.'
}

$installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installPath) {
    throw 'No Visual Studio installation with the "Desktop development with C++" workload was found.'
}

Import-Module (Join-Path $installPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
Write-Host "Visual Studio developer environment: $installPath"
