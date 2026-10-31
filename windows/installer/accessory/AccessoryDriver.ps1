<#
.SYNOPSIS
    Installs or removes the WinUSB driver for Android devices in Osplay's accessory mode.
.DESCRIPTION
    The installer runs this as administrator from the install folder. Installing makes a code
    signing certificate on this PC, signs OsplayAccessory.inf's catalog with it, trusts the
    certificate for drivers, deletes its private key and adds the package to the driver store, so
    Windows binds WinUSB to USB\VID_18D1&PID_2D00 (the method Zadig uses; docs/THIRD_PARTY.md,
    "Android accessory driver"). No other driver can be signed with that certificate afterwards.
    An earlier package and certificate are removed first, so upgrades keep one of each.

    The exit code is pnputil's when installing (0, 259 with no device plugged in, 3010 when a
    restart finishes it) and 1 on any other failure. The log is
    %ProgramData%\Osplay\logs\accessory-driver.log.
.PARAMETER Remove
    Removes the package and the certificate instead.
#>
[CmdletBinding()]
param(
    [switch] $Remove
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Subject = 'CN=Osplay Android USB driver (this computer only)'
$InfName = 'OsplayAccessory.inf'
$CatalogName = 'OsplayAccessory.cat'
$PnpUtil = Join-Path $env:SystemRoot 'System32\pnputil.exe'

function Remove-AccessoryDriver {
    # The driver store renames packages to oemNN.inf and keeps the original name.
    $packages = Get-WindowsDriver -Online | Where-Object {
        (Split-Path $_.OriginalFileName -Leaf) -eq $InfName
    }
    foreach ($package in $packages) {
        & $PnpUtil /delete-driver $package.Driver /uninstall | Out-Host
        Write-Host "Deleted $($package.Driver): pnputil exit code $LASTEXITCODE"
    }
    foreach ($store in 'Root', 'TrustedPublisher') {
        Get-ChildItem "Cert:\LocalMachine\$store" | Where-Object Subject -eq $Subject | Remove-Item
    }
    # A run that failed before deleting its private key. -DeleteKey needs an explicit certificate
    # path: from the pipeline PowerShell cannot bind it.
    foreach ($certificate in Get-ChildItem Cert:\LocalMachine\My | Where-Object Subject -eq $Subject) {
        Remove-Item -Path $certificate.PSPath -DeleteKey
    }
}

function Add-TrustedCertificate {
    param([System.Security.Cryptography.X509Certificates.X509Certificate2] $Certificate)
    # The public part only: Root makes the signature valid, TrustedPublisher installs the driver
    # without asking.
    $public = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $Certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    foreach ($name in 'Root', 'TrustedPublisher') {
        $store = [System.Security.Cryptography.X509Certificates.X509Store]::new($name, 'LocalMachine')
        try {
            $store.Open('ReadWrite')
            $store.Add($public)
        }
        finally {
            $store.Close()
        }
    }
}

function Install-AccessoryDriver {
    Remove-AccessoryDriver
    $inf = Join-Path $PSScriptRoot $InfName
    $catalog = Join-Path $PSScriptRoot $CatalogName
    Remove-Item $catalog -Force -ErrorAction SilentlyContinue
    New-FileCatalog -Path $inf -CatalogFilePath $catalog -CatalogVersion 2 | Out-Null

    $certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject $Subject `
        -CertStoreLocation Cert:\LocalMachine\My -KeyExportPolicy NonExportable `
        -NotAfter (Get-Date).AddYears(30)
    try {
        # The status is UnknownError here because the certificate is not trusted yet.
        $signature = Set-AuthenticodeSignature -FilePath $catalog -Certificate $certificate `
            -HashAlgorithm SHA256
        if ($null -eq $signature.SignerCertificate) {
            throw "Signing the catalog failed: $($signature.StatusMessage)"
        }
        Add-TrustedCertificate $certificate
    }
    finally {
        Remove-Item "Cert:\LocalMachine\My\$($certificate.Thumbprint)" -DeleteKey
    }
    $check = Get-AuthenticodeSignature $catalog
    if ($check.Status -ne 'Valid') {
        throw "The signed catalog does not verify: $($check.StatusMessage)"
    }

    & $PnpUtil /add-driver $inf /install | Out-Host
    return $LASTEXITCODE
}

$log = Join-Path $env:ProgramData 'Osplay\logs\accessory-driver.log'
New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null
Start-Transcript -Path $log -Append | Out-Null
$code = 0
try {
    if ($Remove) {
        Remove-AccessoryDriver
    }
    else {
        $code = Install-AccessoryDriver
        Write-Host "pnputil exit code: $code"
    }
}
catch {
    Write-Host "Failed: $_"
    $code = 1
}
finally {
    Stop-Transcript | Out-Null
}
exit $code
