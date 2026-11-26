# Tests Brouk's MSIX package (032) on a CI runner: signs a copy with a
# throwaway certificate for the package's publisher, trusts it, installs the
# package for this user, checks that it asks for no restricted capability
# but runFullTrust, that the Store app starts through its execution alias,
# reports its version and keeps its data in the package's LocalState\Brouk
# (never in %LOCALAPPDATA%\Brouk), that a requested Move import brings the
# direct download's data in (without caches) and deletes it there, and that
# uninstalling removes the package's data. Removes the package and the
# certificate again. Needs an administrator (trusting the certificate), as
# GitHub's runners are.
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
Import-Module Microsoft.PowerShell.Security, PKI, Appx
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
function Get-StoreApp {
  Get-Process -Name $AppExe -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like '*\WindowsApps\*' }
}
function Stop-StoreApp {
  Get-StoreApp | Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep 3
}
function Wait-For([string] $path, [string] $what) {
  $deadline = (Get-Date).AddSeconds(60)
  while (-not (Test-Path $path) -and (Get-Date) -lt $deadline) { Start-Sleep 1 }
  if (-not (Test-Path $path)) { Fail "no $what ($path) after 60 s" }
}
try {
  $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($password))
  & signtool sign /q /fd SHA256 /f $pfx /p $plain $signed
  if ($LASTEXITCODE -ne 0) { Fail "signtool ended with $LASTEXITCODE" }

  # Start without the direct download's data, to see where the Store app
  # writes its own.
  if (Test-Path $data) { Move-Item $data $saved }

  Add-AppxPackage -Path $signed
  $package = Get-AppxPackage -Name $IdentityName
  if (-not $package) { Fail "$IdentityName is not installed" }
  Write-Host "Installed $($package.PackageFullName)"

  # The Store refuses restricted capabilities it has no reason to grant.
  [xml] $manifest = Get-Content (Join-Path $package.InstallLocation 'AppxManifest.xml')
  $restricted = @($manifest.Package.Capabilities.ChildNodes |
    Where-Object { $_.NamespaceURI -like '*restrictedcapabilities' } |
    ForEach-Object { $_.GetAttribute('Name') })
  if ($restricted.Count -ne 1 -or $restricted[0] -ne 'runFullTrust') {
    Fail "restricted capabilities: $($restricted -join ', ')"
  }
  $state = Join-Path $env:LOCALAPPDATA "Packages\$($package.PackageFamilyName)\LocalState"
  $own = Join-Path $state $AppName

  # The execution alias starts the app with its package identity.
  $alias = Join-Path $env:LOCALAPPDATA "Microsoft\WindowsApps\$AppExe.exe"
  if (-not (Test-Path $alias)) { Fail "no execution alias $alias" }
  $reported = (& $alias --version | Out-String).Trim()
  Write-Host "Reports: $reported"
  if (-not $reported.StartsWith("$AppName $Version ")) { Fail "it reports `"$reported`"" }

  # A normal start: Brouk creates its data folder at once.
  Start-Process -FilePath $alias -ArgumentList 'about:blank'
  Wait-For $own 'data folder'
  Start-Sleep 5
  $running = Get-StoreApp
  if (-not $running) { Fail 'the Store app is not running' }
  if (Test-Path $data) { Fail "the Store app wrote the direct download's folder $data" }
  $redirected = Join-Path $env:LOCALAPPDATA "Packages\$($package.PackageFamilyName)\LocalCache\Local\$AppName"
  if (Test-Path $redirected) { Fail "the data went to the redirected folder $redirected" }
  Write-Host "Running from $($running[0].Path); data in $own"
  Stop-StoreApp

  # Move: the direct download's data (here a copy of the Store app's own,
  # with a marker and a cache file) comes in, and its folder goes.
  Copy-Item -Recurse $own $data
  Set-Content (Join-Path $data 'import-marker.txt') 'from the direct download'
  $cache = Join-Path $data 'web\test\Cache'
  New-Item -ItemType Directory $cache -Force | Out-Null
  Set-Content (Join-Path $cache 'data_0') 'cache'
  $request = Join-Path $state 'import-direct-download'
  # As Brouk writes it: plain ASCII (Windows PowerShell's default is not).
  [IO.File]::WriteAllText($request, 'move', [Text.Encoding]::ASCII)
  Start-Process -FilePath $alias -ArgumentList 'about:blank'
  try {
    Wait-For (Join-Path $own 'import-marker.txt') 'imported data'
  } catch {
    # What happened: was the request read, did a copy start, which windows
    # does Brouk show (a message box waits for a click on CI)?
    Write-Host "Request still there: $(Test-Path $request)"
    Write-Host "Direct download's folder still there: $(Test-Path $data)"
    Write-Host "Staging folder there: $(Test-Path "$own.import")"
    Get-ChildItem $state -Force -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "  $($_.Name)" }
    Get-Process -Name $AppExe -ErrorAction SilentlyContinue |
      ForEach-Object { Write-Host "Process $($_.Id) $($_.Path) window '$($_.MainWindowTitle)'" }
    Add-Type -Namespace Win -Name Windows -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, System.IntPtr p);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr w, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr w);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(System.IntPtr w, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern System.IntPtr GetDlgItem(System.IntPtr w, int id);
public delegate bool EnumProc(System.IntPtr w, System.IntPtr p);
'@
    $ids = @(Get-StoreApp | ForEach-Object { [uint32] $_.Id })
    [Win.Windows]::EnumWindows({ param($w, $p)
      $procId = [uint32] 0
      [void] [Win.Windows]::GetWindowThreadProcessId($w, [ref] $procId)
      if ($ids -contains $procId -and [Win.Windows]::IsWindowVisible($w)) {
        $text = New-Object System.Text.StringBuilder 512
        [void] [Win.Windows]::GetWindowText($w, $text, 512)
        $message = New-Object System.Text.StringBuilder 2048
        # A message box's text is its static control 0xFFFF.
        $item = [Win.Windows]::GetDlgItem($w, 0xFFFF)
        if ($item -ne [IntPtr]::Zero) { [void] [Win.Windows]::GetWindowText($item, $message, 2048) }
        Write-Host "Window '$text': $message"
      }
      $true }, [IntPtr]::Zero) | Out-Null
    throw
  }
  Start-Sleep 5
  if (Test-Path $request) { Fail 'the import request is still there' }
  if (Test-Path (Join-Path $own 'web\test\Cache')) { Fail 'the cache was imported' }
  if (Test-Path $data) { Fail "Move left the direct download's folder $data" }
  if (-not (Get-StoreApp)) { Fail 'the Store app is not running after the import' }
  Write-Host 'Move brought the data in and removed the old folder.'
  Stop-StoreApp

  Remove-AppxPackage -Package $package.PackageFullName
  $package = $null
  if (Get-AppxPackage -Name $IdentityName) { Fail 'the package is still installed' }
  if (Test-Path $own) { Fail "uninstalling left the package's data $own" }
  Write-Host "Installed, started, imported and uninstalled $(Split-Path $Msix -Leaf)."
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
