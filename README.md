# WinSysLive

A live view of a Windows system, in the spirit of Process Explorer and System Informer: processes as a tree with
their threads, modules, handles, memory, token, windows, jobs and services; performance graphs (CPU, memory, I/O,
network, disks, GPUs, NPUs); services and drivers; users; network connections; kernel modules; RPC/ALPC traffic;
zombie processes; object types and the object manager namespace; kernel pool tags; firmware and TPM; with VirusTotal
checks, symbols, wait chains and dumps.

Some information (other users' processes, network traffic, RPC/ALPC, pool tags...) needs the app to run elevated.

## Install

```
winget install ScorpioSoftware.WinSysLive
```

or download the zip from [Releases](https://github.com/zodiacon/WinSysLive/releases) and run `WinSysLive.exe`
(keep `dbghelp.dll` and `symsrv.dll` beside it to download symbols). Windows 10 or later, x64.

## Build

Visual Studio 2026 (toolset v145) with the Windows SDK 10.0.26100.0, phnt and WIL from vcpkg, and the submodules:

```
git clone --recursive https://github.com/zodiacon/WinSysLive
nuget restore wtlhelper\WTLHelper.sln
msbuild WinSysLive.sln /p:Configuration=Release /p:Platform=x64
```
