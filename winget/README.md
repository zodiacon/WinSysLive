# WinGet manifests

Community-repo manifests for publishing WinSysLive to the Windows Package Manager
(https://github.com/microsoft/winget-pkgs). The folder layout mirrors winget-pkgs, so a version directory can be
copied straight into `manifests/s/ScorpioSoftware/WinSysLive/<version>/`.

Identifier: `ScorpioSoftware.WinSysLive`, the version resource's `CompanyName` (Scorpio Software), as
`ScorpioSoftware.PavelTools`.

## The package

A zip on the GitHub release, installed by WinGet as a portable app (`NestedInstallerType: portable`, per user, no
Start menu entry; the command is `winsyslive`): `WinSysLive.exe` (ReleaseSigned, signed), `dbghelp.dll` and
`symsrv.dll` (the Debugging Tools' copies, which the symbol engine loads from the exe's folder to download symbols)
and `LICENSE.txt`. `ArchiveBinariesDependOnPath: true` makes WinGet put the install folder on the PATH rather than
link the exe from its Links folder, since an exe started through a symlink would look for `dbghelp.dll` beside the
link.

## Per release

1. Bump `FILEVERSION`, `PRODUCTVERSION`, `FileVersion` and `ProductVersion` in `VS_VERSION_INFO`
   (`WinSysLive\WinSysLive.rc`). The package version is `ProductVersion` without its fourth part (`1.2.0.0` is
   `1.2.0`); the release tag is `v1.2.0`.
2. `.\winget\New-Release.ps1` (`-NoBuild` to package the existing build): rebuilds the solution as ReleaseSigned,
   checks the signature, writes `dist\WinSysLive-<version>-x64.zip` and the manifests in
   `winget\ScorpioSoftware.WinSysLive\<version>`, with the zip's SHA-256 and today as `ReleaseDate`.
3. Create the GitHub release with the tag `v<version>` and attach that zip, unchanged: the manifest has its hash.
   If the release is published on another day, fix `ReleaseDate`.
4. Validate, then submit:

```powershell
winget validate --manifest winget\ScorpioSoftware.WinSysLive\<version>
komac submit winget\ScorpioSoftware.WinSysLive\<version>
```

The first submission is a new package; for later ones `komac update ScorpioSoftware.WinSysLive --version <version>
--urls <zip url>` works too (it reads the rest from the previous version).

To smoke-test the manifest before submitting (needs `winget settings --enable LocalManifestFiles`, elevated):
`winget install --manifest winget\ScorpioSoftware.WinSysLive\<version>`.
