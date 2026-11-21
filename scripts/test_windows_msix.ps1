# Tests Brouk's MSIX package (032) on a CI runner: signs a copy with a
# throwaway certificate for the package's publisher, trusts it, installs the
# package for this user, checks that the Store app starts through its
# execution alias, reports its version, keeps its data in %LOCALAPPDATA%\Brouk
# (not in the package's private folder) and that the data stays after
# uninstalling. Removes the package and the certificate again. Needs an
# administrator (trusting the certificate), as GitHub's runners are.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test_windows_msix.ps1 `
#     -Msix build\dist\Brouk-0.5.0-x64.msix -Publisher "CN=..." -AppName Brouk `
#     -AppExe brouk -Version 0.5.0 -IdentityName Brouk.Placeholder
param(
  [Parameter(Mandatory)] [string] $Msix,
  [Parameter(Mandatory)] [string] $Publisher,
  [Parameter(Mandatory)] [string] $AppName,
  [Parameter(Mandatory)] [string] $AppExe,
  [Parameter(Mandatory)] [string] $Version,
  [Parameter(Mandatory)] [string] $IdentityName
)
$ErrorActionPreference = 'Stop'
function Fail([string] $message) { throw "MSIX test failed: $message" }

$work = Join-Path $env:RUNNER_TEMP 'msix-test'
if (-not $env:RUNNER_TEMP) { $work = Join-Path $env:TEMP 'msix-test' }
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory $work | Out-Null
$signed = Join-Path $work (Split-Path $Msix -Leaf)
Copy-Item $Msix $signed

# A code-signing certificate whose subject is the manifest's publisher.
$cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher `
  -KeyUsage DigitalSignature -FriendlyName 'Brouk MSIX test' `
  -CertStoreLocation 'Cert:\CurrentUser\My' `
  -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
$password = ConvertTo-SecureString -String ([guid]::NewGuid().ToString()) -Force -AsPlainText
$pfx = Join-Path $work 'test.pfx'
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $password | Out-Null
$cer = Join-Path $work 'test.cer'
Export-Certificate -Cert $cert -FilePath $cer | Out-Null
Import-Certificate -FilePath $cer -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' | Out-Null

$data = Join-Path $env:LOCALAPPDATA $AppName
$saved = "$data.before-msix-test"
$package = $null
try {
  $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($password))
  & signtool sign /q /fd SHA256 /f $pfx /p $plain $signed
  if ($LASTEXITCODE -ne 0) { Fail "signtool ended with $LASTEXITCODE" }

  # Start without data, to see where the Store app writes it.
  if (Test-Path $data) { Move-Item $data $saved }

  Add-AppxPackage -Path $signed
  $package = Get-AppxPackage -Name $IdentityName
  if (-not $package) { Fail "$IdentityName is not installed" }
  Write-Host "Installed $($package.PackageFullName)"

  # The execution alias starts the app with its package identity.
  $alias = Join-Path $env:LOCALAPPDATA "Microsoft\WindowsApps\$AppExe.exe"
  if (-not (Test-Path $alias)) { Fail "no execution alias $alias" }
  $reported = (& $alias --version | Out-String).Trim()
  Write-Host "Reports: $reported"
  if (-not $reported.StartsWith("$AppName $Version ")) { Fail "it reports `"$reported`"" }

  # A normal start: Brouk creates its data folder at once.
  Start-Process -FilePath $alias -ArgumentList 'about:blank'
  $deadline = (Get-Date).AddSeconds(60)
  while (-not (Test-Path $data) -and (Get-Date) -lt $deadline) { Start-Sleep 1 }
  $private = Join-Path $env:LOCALAPPDATA "Packages\$($package.PackageFamilyName)\LocalCache\Local\$AppName"
  if (Test-Path $private) { Fail "the data went to the package's private folder $private" }
  if (-not (Test-Path $data)) { Fail "no data folder $data after 60 s" }
  $running = Get-Process -Name $AppExe -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like '*\WindowsApps\*' }
  if (-not $running) { Fail 'the Store app is not running' }
  Write-Host "Running from $($running[0].Path); data in $data"
  Start-Sleep 5
  $running | Stop-Process -Force
  Start-Sleep 3

  Remove-AppxPackage -Package $package.PackageFullName
  $package = $null
  if (Get-AppxPackage -Name $IdentityName) { Fail 'the package is still installed' }
  if (-not (Test-Path $data)) { Fail 'uninstalling removed the data folder' }
  Write-Host "Installed, started and uninstalled $(Split-Path $Msix -Leaf)."
}
finally {
  Get-Process -Name $AppExe -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like '*\WindowsApps\*' } | Stop-Process -Force -ErrorAction SilentlyContinue
  if ($package) { Remove-AppxPackage -Package $package.PackageFullName -ErrorAction SilentlyContinue }
  if (Test-Path $saved) {
    Remove-Item -Recurse -Force $data -ErrorAction SilentlyContinue
    Move-Item $saved $data
  }
  Get-ChildItem 'Cert:\LocalMachine\TrustedPeople', 'Cert:\CurrentUser\My' |
    Where-Object { $_.Thumbprint -eq $cert.Thumbprint } | Remove-Item -ErrorAction SilentlyContinue
  Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
