<#
.SYNOPSIS
Packages WinSysLive for a GitHub release and writes its WinGet manifests.

.DESCRIPTION
Builds the solution as ReleaseSigned (unless -NoBuild), checks the exe's signature, zips the exe with dbghelp.dll and
symsrv.dll (the Debugging Tools' copies, which the symbol engine needs beside the exe to download symbols) and
LICENSE.txt into dist\WinSysLive-<version>-x64.zip, and writes the manifests for that zip to
winget\ScorpioSoftware.WinSysLive\<version>. The version is the exe's ProductVersion (VS_VERSION_INFO in
WinSysLive.rc) without its fourth part; the release tag is v<version>. The zip is the asset to upload as is: its hash is
the manifest's InstallerSha256.
#>
param(
	[switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$out = Join-Path $root 'x64\ReleaseSigned'
$exe = Join-Path $out 'WinSysLive.exe'
$id = 'ScorpioSoftware.WinSysLive'
$repo = 'https://github.com/zodiacon/WinSysLive'

if (-not $NoBuild) {
	$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	$msbuild = & $vswhere -latest -prerelease -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | Select-Object -First 1
	if (-not $msbuild) { throw 'MSBuild not found' }
	# the solution, so the libraries build as Release (see CLAUDE.md)
	& $msbuild (Join-Path $root 'WinSysLive.sln') /t:Rebuild /p:Configuration=ReleaseSigned /p:Platform=x64 /m /v:m /nologo
	if ($LASTEXITCODE) { throw "The build failed ($LASTEXITCODE)" }
}

$signature = Get-AuthenticodeSignature $exe
if ($signature.Status -ne 'Valid') { throw "$exe isn't signed validly: $($signature.Status)" }

$v = [Version](Get-Item $exe).VersionInfo.ProductVersion
$version = '{0}.{1}.{2}' -f $v.Major, $v.Minor, [Math]::Max($v.Build, 0)
$tag = "v$version"
$zipName = "WinSysLive-$version-x64.zip"

# dbghelp.dll and symsrv.dll: the output folder's, else the Debugging Tools'
$debuggers = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Debuggers\x64'
$files = @($exe, (Join-Path $root 'LICENSE.txt'))
foreach ($dll in 'dbghelp.dll', 'symsrv.dll') {
	$path = @((Join-Path $out $dll), (Join-Path $debuggers $dll)) | Where-Object { Test-Path $_ } | Select-Object -First 1
	if (-not $path) { throw "$dll not found in $out or $debuggers (install the Debugging Tools for Windows)" }
	$files += $path
}

$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null
$zip = Join-Path $dist $zipName
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $files -DestinationPath $zip
$sha = (Get-FileHash $zip -Algorithm SHA256).Hash

$dir = Join-Path $PSScriptRoot "$id\$version"
New-Item -ItemType Directory -Force $dir | Out-Null
$date = Get-Date -Format 'yyyy-MM-dd'
$utf8 = New-Object System.Text.UTF8Encoding $false

[IO.File]::WriteAllText((Join-Path $dir "$id.yaml"), @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.version.1.12.0.schema.json

PackageIdentifier: $id
PackageVersion: $version
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.12.0

"@, $utf8)

# portable, from the zip: ArchiveBinariesDependOnPath puts the folder on the PATH instead of a symlink, as the exe
# loads dbghelp.dll from its own folder (a symlink's would be WinGet's Links folder)
[IO.File]::WriteAllText((Join-Path $dir "$id.installer.yaml"), @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.installer.1.12.0.schema.json

PackageIdentifier: $id
PackageVersion: $version
InstallerLocale: en-US
InstallerType: zip
NestedInstallerType: portable
NestedInstallerFiles:
  - RelativeFilePath: WinSysLive.exe
    PortableCommandAlias: winsyslive
ArchiveBinariesDependOnPath: true
UpgradeBehavior: uninstallPrevious
ReleaseDate: $date
Installers:
  - Architecture: x64
    InstallerUrl: $repo/releases/download/$tag/$zipName
    InstallerSha256: $sha
ManifestType: installer
ManifestVersion: 1.12.0

"@, $utf8)

[IO.File]::WriteAllText((Join-Path $dir "$id.locale.en-US.yaml"), @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.defaultLocale.1.12.0.schema.json

PackageIdentifier: $id
PackageVersion: $version
PackageLocale: en-US
Publisher: Scorpio Software
PublisherUrl: https://github.com/zodiacon
PublisherSupportUrl: $repo/issues
Author: Pavel Yosifovich
PackageName: WinSysLive
PackageUrl: $repo
License: MIT
LicenseUrl: $repo/blob/HEAD/LICENSE.txt
Copyright: (c) Pavel Yosifovich
ShortDescription: A live view of a Windows system's processes, performance, services, kernel objects and more.
Description: |-
  WinSysLive is a system explorer in the spirit of Process Explorer and System Informer: processes as a tree
  with details (threads, modules, handles, memory, token, windows, jobs, services), performance graphs (CPU,
  memory, I/O, network, disks, GPUs, NPUs), services and drivers, users, network connections, kernel modules,
  RPC/ALPC traffic, zombie processes, object types, the object manager namespace, kernel pool tags, firmware
  and TPM, with VirusTotal checks, symbols, wait chains and dumps.
ReleaseNotesUrl: $repo/releases/tag/$tag
Moniker: winsyslive
Tags:
  - diagnostics
  - internals
  - performance
  - process
  - sysinternals
  - system
  - task-manager
ManifestType: defaultLocale
ManifestVersion: 1.12.0

"@, $utf8)

Write-Host "Version $version, $zip"
Write-Host "SHA256 $sha"
Write-Host "Manifests in $dir"
Write-Host "Next: publish release $tag with $zipName attached, then: winget validate --manifest `"$dir`""
