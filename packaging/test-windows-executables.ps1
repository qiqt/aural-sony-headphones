param(
    [Parameter(Mandatory)][string]$DownloadsDir,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$ExpectedAppSha256
)

$ErrorActionPreference = 'Stop'
$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Aural'
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Aural\Aural.lnk'
if ((Test-Path $key) -or (Test-Path $shortcut) -or
    (Test-Path 'HKCU:\Software\Aural\Installer') -or (Get-Process appAural -ErrorAction SilentlyContinue)) {
    throw 'Run this check in a disposable Windows environment without Aural installed or running.'
}
$root = Join-Path $env:TEMP ('Aural packaging test ' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $root | Out-Null
$install = Join-Path $root 'Installed Aural'
$env:AURAL_DEMO = '1'
$env:QT_QUICK_BACKEND = 'software'
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"

function Assert-App([System.Diagnostics.Process]$Process, [string]$Path) {
    Start-Sleep -Seconds 8
    $Process.Refresh()
    if ($Process.HasExited) { throw "Aural exited with code $($Process.ExitCode)" }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $ExpectedAppSha256) {
        throw 'Packaged application differs from the portable ZIP'
    }
    $info = (Get-Item -LiteralPath $Path).VersionInfo
    if ($info.FileVersion -ne $Version -or $info.ProductVersion -ne $Version) {
        throw 'Application version mismatch'
    }
    if (-not ($Process.Modules | Where-Object ModuleName -eq 'Qt6Quick.dll')) {
        throw 'Qt Quick was not loaded'
    }
}

$setup = Join-Path $DownloadsDir "Aural-$Version-Setup.exe"
$portable = Join-Path $DownloadsDir "Aural-$Version-Portable.exe"
foreach ($exe in @($setup, $portable)) {
    $info = (Get-Item -LiteralPath $exe).VersionInfo
    if ($info.ProductVersion -ne $Version) { throw "Wrong version in $exe" }
}
$installer = Start-Process -FilePath $setup -ArgumentList "/S /D=$install" -WindowStyle Hidden -PassThru -Wait
if ($installer.ExitCode -ne 0) { throw "Setup failed: $($installer.ExitCode)" }
$registration = Get-ItemProperty $key
if ($registration.DisplayVersion -ne $Version -or $registration.InstallLocation -ne $install) {
    throw 'Incorrect uninstall registration'
}
if ($registration.UninstallString -ne ('"' + $install + '\Uninstall.exe"')) {
    throw 'Incorrect uninstaller command quoting'
}
if (-not (Test-Path $shortcut)) { throw 'Missing Start menu shortcut' }
$shell = New-Object -ComObject WScript.Shell
# Let Windows normalize both paths, including short names in the runner's TEMP.
$expectedShortcut = $shell.CreateShortcut((Join-Path $root 'expected.lnk'))
$expectedShortcut.TargetPath = "$install\appAural.exe"
$actualTarget = $shell.CreateShortcut($shortcut).TargetPath
if ($actualTarget -ne $expectedShortcut.TargetPath) {
    throw "Incorrect Start menu shortcut target: '$actualTarget'; expected '$($expectedShortcut.TargetPath)'"
}
foreach ($license in @('LGPL-3.0-only.txt', 'GPL-3.0-only.txt', 'NSIS-zlib.txt')) {
    if (-not (Test-Path "$install\licenses\$license")) { throw "Missing $license" }
}
$app = Start-Process -FilePath "$install\appAural.exe" -WindowStyle Hidden -PassThru
try { Assert-App $app "$install\appAural.exe" }
finally { if (-not $app.HasExited) { Stop-Process -Id $app.Id } }

# A user-created file must survive uninstall.
Set-Content -LiteralPath "$install\keep-me.txt" -Value 'Unrelated user file'
$uninstaller = Join-Path $root 'Uninstall.exe'
Copy-Item -LiteralPath "$install\Uninstall.exe" -Destination $uninstaller
$uninstall = Start-Process -FilePath $uninstaller -ArgumentList "/S _?=$install" -WindowStyle Hidden -PassThru -Wait
if ($uninstall.ExitCode -ne 0) { throw "Uninstall failed: $($uninstall.ExitCode)" }
if ((Test-Path "$install\appAural.exe") -or (Test-Path $key) -or (Test-Path $shortcut)) {
    throw 'Uninstall left app files, registration or shortcuts behind'
}
if (-not (Test-Path "$install\keep-me.txt")) { throw 'Uninstall removed an unrelated file' }

$launcher = Start-Process -FilePath $portable -WindowStyle Hidden -PassThru
$child = $null
try {
    $deadline = (Get-Date).AddSeconds(90)
    do {
        Start-Sleep -Milliseconds 500
        $child = Get-CimInstance Win32_Process -Filter "Name='appAural.exe'" |
            Where-Object ParentProcessId -eq $launcher.Id | Select-Object -First 1
        $launcher.Refresh()
        if ($launcher.HasExited -and -not $child) { throw "Portable launcher exited: $($launcher.ExitCode)" }
    } until ($child -or (Get-Date) -gt $deadline)
    if (-not $child) { throw 'Portable application did not start' }
    $temporaryApp = [string]$child.ExecutablePath
    if (-not $temporaryApp.StartsWith($env:TEMP + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Portable app was not extracted inside the temporary directory'
    }
    $app = Get-Process -Id $child.ProcessId
    Assert-App $app $temporaryApp
    if ((Test-Path $key) -or (Test-Path $shortcut)) { throw 'Portable launcher installed the app' }
    Stop-Process -Id $app.Id
    if (-not $launcher.WaitForExit(15000)) { throw 'Portable launcher did not exit with the app' }
    if (Test-Path (Split-Path $temporaryApp)) { throw 'Portable files were not cleaned up' }
} finally {
    if ($child) { Stop-Process -Id $child.ProcessId -ErrorAction SilentlyContinue }
    if (-not $launcher.HasExited) { Stop-Process -Id $launcher.Id }
}

[pscustomobject]@{
    Status = 'Passed'
    Version = $Version
    Checks = @('setup', 'shortcut', 'version', 'app hash', 'licenses', 'uninstall',
               'preserve user files', 'portable launch', 'portable cleanup')
    TestDirectory = $root
}
