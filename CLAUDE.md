# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

WinSysLive is a native Win32 GUI app (C++23, ATL/WTL, Unicode) at an early stage: a WTL frame window hosting a tabbed view. The app code lives in `WinSysLive/`; the heavy lifting comes from two git submodules by the same author, each with its own `CLAUDE.md` to read before changing it:

- `wtlhelper/`: WTL UI libraries. `WTLHelper` (static lib: dark mode, `Settings`, list-view/tab helpers, vendored WTL headers in `WTL/`), `GraphControl` (Direct2D time-series graph) and `LogView` (`CLogViewCtrl`, the History tab's log, `<LogView/include/LogView.h>`) are projects in `WinSysLive.sln` (in its Controls folder, with project references from the app).
- `winsys/`: the `WinSys` static library (NT native API wrappers: processes, threads, `ProcessManager<>` snapshots, services, network interface and drive statistics, and so on). The app references it, along with WTLHelper, TreeListView, GraphControl and HexControl (`<HexControl/SimpleHexControl.h>`, a static lib in `wtlhelper/HexControl`).

If the submodule directories are empty, run `git submodule update --init`.

## Build

MSBuild only. There is no CMake or lint. Toolset v145 (VS 18).

```powershell
msbuild WinSysLive.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild WinSysLive\WinSysLive.vcxproj /p:Configuration=Debug /p:Platform=x64   # app + WTLHelper (project reference)
x64\Debug\Tests.exe          # the tests; Tests.exe Alerts runs those whose names contain "Alerts", Tests.exe -Live all but those with "Live"; the exit code is the failures
```

Tests: `Tests\Tests.vcxproj` (built with the solution in Debug and Release x64, not in ReleaseSigned or ARM64) is a console program with a runner of its own (`TestFramework.h`: `TEST`, `CHECK`, `CHECK_EQ`; no framework). Its precompiled header is the app's `pch.h`, so app sources it takes compile as in the app (`AlertRules.cpp`, the Alerts rules without the app around them); it links WinSys. The tests cover the parsers of undocumented formats on buffers in their verified layouts (power requests and wake timers, the .NET rundown events, the .NET diagnostics pipe's ProcessInfo answer and the GC events, the heap summary's `RTL_PROCESS_HEAPS` in both layouts), the installed packages' wording and live enumeration (`Packages.h`), the event data parser, the PE parser (real images and broken ones), the Alerts rule text, conditions and event summaries, the column layouts, the Events tab's filters, the Devices tab's rows (`DeviceTree.h`), the Resources by Type order (`DeviceResourceOrder.h`), the command palette's search (`PaletteSearch.h`), the snapshot file format and comparison (`Snapshot.cpp`; `Snapshot::Capture` is apart, in `SnapshotCapture.cpp`, as it needs the app's process snapshot), and some live queries. Code that's worth testing is kept apart from the UI and the queries (`PowerRequests::ParseRequestList`, `DotNetInfo::ApplyRundownEvent`, `AlertRule`) so a test can call it; add a test with a change to such code, and see it fail once before trusting it. Tests that read the live system (power requests, devices, event logs, an app silo, pwsh's .NET runtime) have "Live" in their names: their results depend on the machine.

CI: `.github/workflows/build.yml` builds Debug and Release x64 on GitHub's `windows-2025-vs2026` image (Visual Studio 2026, whose only C++ toolset is v145) on every push and pull request to `master`: the submodules checked out (not recursively: WinSys lists a submodule nothing builds), phnt, WIL, Detours and WTL from the runner's vcpkg in both triplets, with user-wide integration, the built packages kept between runs (vcpkg's binary cache in the runner's temp folder, saved by `actions/cache`, keyed by the image's vcpkg commit); then `Tests.exe -Live` is the gate and `Tests.exe Live` runs after it, informational (`continue-on-error`). The Release build is uploaded. ReleaseSigned isn't built (no certificate on the runner).

ReleaseSigned (x64) is Release with the exe signed after the build, as TotalPE does (`signtool sign /a /n Scorpio /t http://timestamp.digicert.com /fd sha256`, after the DbgHelp copy, so a failed signing fails the build). Build it through the solution (`msbuild WinSysLive.sln /p:Configuration=ReleaseSigned /p:Platform=x64`), which builds the libraries in Release; building the project alone would ask them for a ReleaseSigned they don't have. The output is `x64\ReleaseSigned\`.

Releases: `winget\New-Release.ps1` builds ReleaseSigned, zips the exe with `dbghelp.dll`, `symsrv.dll` and `LICENSE.txt` into `dist\` (ignored) and writes the WinGet manifests (`ScorpioSoftware.WinSysLive`, a portable app from the zip on the GitHub release) for the version in `VS_VERSION_INFO`; `winget\README.md` has the steps.

Dependencies come from the submodules, and from vcpkg (user-wide integration): WTLHelper needs Detours, WIL and WTL, in the static triplet (`x64-windows-static`: it and HexControl set `VcpkgUseStatic`), the rest the default one (`x64-windows`), and the app links Detours from that one (the LNK4217/LNK4286 warnings: its library was built for the DLL runtime); these were NuGet packages once (wtlhelper's `packages.config`, unused, is gone). WinSys needs phnt and WIL from vcpkg and must stay pinned to Windows SDK `10.0.26100.0` (see `winsys/CLAUDE.md` for why). `dbghelp.dll` is delay-loaded and loaded from the exe's folder (see Symbols); the build doesn't put it or `symsrv.dll` there (the Pavel Tools installer ships the Debugging Tools' copies beside the exe), so a dev build without them uses the system's `dbghelp.dll`, which can't download symbols. Copy both from `$(WindowsSdkDir)Debuggers\x64` into the output folder to test the symbol server.

Include paths are `..\wtlhelper\WTLHelper` and `..\wtlhelper\WTL` (WTLHelper headers such as `WTLHelper.h`, `Settings.h` and `NativeCustomTabView.h` are included by bare name), plus `..\wtlhelper` and `..\winsys` so that the other libraries are included with a folder prefix: `<WinSys/ProcessManager.h>`, `<TreeListView/include/TreeListView.h>`. Keep that prefix. WinSys and WTLHelper both have a `SecurityHelper.h`, and WTLHelper has its own, different `TreeListView.h`. The CRT is linked statically (`MultiThreaded[Debug]`).

The include order in `pch.h` matters, and the reasons are commented there:
1. Full `windows.h` first (with `WIN32_NO_STATUS`), because `phnt_windows.h` forces `WIN32_LEAN_AND_MEAN`.
2. `phnt_windows.h`, then `#undef INITGUID`.
3. ATL/WTL.
4. `phnt.h` last, because it `#define`s `TOKEN_INFORMATION_CLASS` values that break `atlcom.h`.

`winsock2.h` comes right before `windows.h`, which would otherwise include `winsock.h`, whose definitions clash with `ws2def.h` (needed by `ws2ipdef.h`, which `iphlpapi.h` needs for its IPv6 tables).

`NOMINMAX` is defined at the top of `pch.h`; phnt would define it, but too late now that `windows.h` comes first.

## Architecture

- **WinSys first:** core Windows functionality (native API queries, process/thread/handle operations, GPU, connections, symbols, dumps) belongs in WinSys, exported as types the app consumes; the app keeps UI state and UI-only types (highlighting, rows, icons, threading for the UI). When a feature needs a new query, add it to WinSys (see `winsys/CLAUDE.md`) rather than calling the API from a view.
- `WinSysLive.cpp`: `_tWinMain` initializes COM and `CAppModule _Module`, loads settings from the registry key `Software\ScorpioSoftware\WinSysLive`, calls `WTLHelper::InitDarkMode(...)` *before* any window is created, then runs the message loop with a single `CMainFrame`.
- `AppSettings.h`: a `Settings` subclass declared with WTLHelper's `BEGIN_SETTINGS`/`SETTING`/`DEF_SETTING` macros. The first constructed instance becomes the singleton (the global `AppSettings _Settings` in `WinSysLive.cpp`) and is reached anywhere through `AppSettings::Get()`. To add a persisted option, add a `SETTING(...)` entry plus a `DEF_SETTING(...)` accessor, and call `Save()` after changing it. At logoff or shutdown no window is destroyed, so the frame's `WM_ENDSESSION` calls `IView::SaveSettings` on every view (a view that saves state in `WM_DESTROY`, such as column layouts, does the same there) and then saves the frame's own.
- `MainFrm.*`: `CMainFrame` (`CFrameWindowImpl` + `CAutoUpdateUI`, and it implements `IMainFrame`) hosts the tabs in `CMainTabView`, a `CNativeCustomTabViewImpl`. Only Processes is always open (its page is `FixedPage`, whose close button `CalcCloseButtonRect` makes empty); the others open and close: the `Tabs` setting has the open ones' keys (`s_Tabs`, which must not change) in tab order (default Processes, Performance, Services), saved as tabs open and close and at exit (dragging reorders them), and `ActiveTab` is an index among them. A View menu command (also a checked toolbar button) opens a closed tab, activates an open one, and closes the active one; `TBVN_TABCLOSEBTN` closes through the tab view, which destroys the view's window (the view deletes itself). Pages are matched to views by their windows (`GetPageView`), never by index, and the active view is kept as a pointer, as removing and dragging tabs shift the indices; `ForgetView` stops the frame talking to a view before its page goes. `ActivateTab` opens a closed tab (Go to Kernel Module, Go to Driver); a view opened after startup gets the theme and font from `OpenTab`. Dark mode is toggled through `WTLHelper::SwitchToMode`, after which every thread window is redrawn and the status bar background is set by hand.
- Views follow TotalPE (`C:\dev\totalpe2\totalpe`):
  - `Interfaces.h` declares `IMainFrame` and `IView`. Every view derives from `CViewBase<T>` (`ViewBase.h`), which is built on WTLHelper's `CFrameView`, so each view is a child frame window with its own client area and rebar.
  - Views are heap-allocated and delete themselves when their window is destroyed.
  - Each tab's page data is the view's `CMessageMap*`. `COMMAND_TABVIEW_HANDLER(m_Tabs, 1)`, which comes first in the frame's map, routes `WM_COMMAND` to the active view's `ALT_MSG_MAP(1)`. That section must end with `CHAIN_MSG_MAP_ALT(CViewBase<T>, 1)`.
- **Refresh model:**
  - The frame owns the single `ProcessManager` (`WinSys::ProcessManager<ProcessInfoEx>`, defined in `ProcessInfoEx.h`) and one timer. The interval is the `UpdateInterval` setting, and updates can be paused.
  - On each tick the frame calls `Update()` once, then `IView::OnRefresh(active)` on **every** view. Inactive views update their data but don't redraw.
  - Views get the snapshot through `IMainFrame::GetProcessManager()` and must not take their own.
  - `ProcessInfo::CPU` is in units of 1/10000 of a percent.
- **Font:** Options > Font... saves a `LOGFONT` in the `Font` setting, its height at 96 DPI (no face name: the default; Options > Default Font clears it). The frame creates the font for its DPI (`ApplyFont`; the default is the icon title font, the list views' own) and hands it to every view through `IView::OnFontChanged`, which views apply to their lists and trees (the Processes view also to the lower pane's pages, through `SetListFont`) and the Performance view to its navigation pane and the text below the graphs (the graphs keep their Direct2D fonts). The frame keeps the font until the next change, so a view may hold its handle (the Performance view does, for drive pages added later). With no font set, the frame doesn't call the views at startup, so the controls keep their own fonts. The property sheets keep theirs.
- **Shared image list:** the frame owns the 16×16 image list, which the tab view also uses. Views get it with `GetImageList()`, look up icons with `GetIconIndex(IDI_*)` (`IIconSource`, which `IMainFrame` derives from), and should create their lists with `LVS_SHAREIMAGELISTS`. Register a new icon in `CMainFrame::InitImageList` (`s_Icons`); the system's stock icons (winuser.h's `IDI_WARNING`, `IDI_ERROR`) go in `s_SystemIcons`, loaded at the list's size, and are looked up with `GetIconIndex(SystemIconId(IDI_WARNING))` (`IconSource.h`). Process icons come from `IMainFrame::GetProcessIcon`, which extracts them from the executables and caches them by path (processes without one get the system's application icon).
- **Painting performance:** WTLHelper installs a thread-wide `WH_CALLWNDPROCRET` hook to theme new windows. While it's installed, every `SendMessage` goes through the kernel, and that roughly doubled the time to repaint the process list. For any list with many rows or callbacks, call `SuspendHookWhilePainting(hList)` from `ViewBase.h`; it removes the hook during `WM_PAINT` (`WTLHelper::SuspendHook` really unhooks). Measured per refresh tick with the Processes tab visible: ~20 ms for the snapshot (on the UI thread) and ~60 ms for painting.
- **Adding a tab** takes four steps:
  1. Add a value to `TabType` (`Interfaces.h`).
  2. Write `XxxView.h/.cpp`.
  3. Add a `case` to `CMainFrame::CreateView` (`ViewFactory.cpp`).
  4. Add an entry to `CMainFrame::s_Tabs` (with a key for the `Tabs` setting), an `ID_VIEW_*` command (View menu item and Ctrl+N accelerator), its toolbar button in `s_ToolBarButtons` (`BTNS_CHECK`) and a `COMMAND_ID_HANDLER(ID_VIEW_*, OnViewTab)` in the frame's message map.
  5. Describe it in `docs/views.md`.

## Detailed documentation

The parts of the app are described in detail in `docs/`. Read the file for the area you're changing first, and keep it up to date as you change the code (new features get an entry there, not here):

- [docs/processes-view.md](docs/processes-view.md): the Processes tab, its lower pane (Handles, Modules, Threads) and the process property sheet.
- [docs/views.md](docs/views.md): the other tabs (Services and Drivers, Users, Network, Kernel Modules, RPC/ALPC, Zombie Processes, Object Types, Object Manager, Pool Tags, Disk, History, Autoruns, Devices, Events, Performance).
- [docs/dialogs.md](docs/dialogs.md): the dialogs and menus that aren't a tab's (Run, Find Handle or DLL, Snapshots, System Information, Firmware and TPM, the Computer, Tools and Memory Lists menus, Pipes and Mailslots, Installed Packages, Network Information, ETW Sessions, Alerts, the command palette, Window Stations and Desktops, Power Requests, Network Shares, Page Files, RAM, Logon Sessions, the window finder, VirusTotal, wait chains, About).
- [docs/app-services.md](docs/app-services.md): Copy and Export, the ACL editor, highlighting, the tray icons and window options, startup, updates and settings files, symbols and DPI.

## Conventions

- Every `.cpp` starts with `#include "pch.h"` (ATL/WTL and STL headers live there).
- Classes use WTL style: `C` prefix, `m_` members, message maps with the commented handler-prototype boilerplate, tabs for indentation, and opening braces on the same line.
- New command IDs go in `resource.h` and the menu in `WinSysLive.rc`. When adding a source file, register it in both `WinSysLive.vcxproj` and `WinSysLive.vcxproj.filters`.
