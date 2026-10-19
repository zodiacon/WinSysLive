# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

WinSysLive is a native Win32 GUI app (C++23, ATL/WTL, Unicode) at an early stage: a WTL frame window hosting a tabbed view. The app code lives in `WinSysLive/`; the heavy lifting comes from two git submodules by the same author, each with its own `CLAUDE.md` to read before changing it:

- `wtlhelper/`: WTL UI libraries. `WTLHelper` (static lib: dark mode, `Settings`, list-view/tab helpers, vendored WTL headers in `WTL/`) and `GraphControl` (Direct2D time-series graph) are both projects in `WinSysLive.sln`.
- `winsys/`: the `WinSys` static library (NT native API wrappers: processes, threads, `ProcessManager<>` snapshots, services, network interface statistics, and so on). The app references it, along with WTLHelper, TreeListView and GraphControl.

If the submodule directories are empty, run `git submodule update --init`.

## Build

MSBuild only. There is no CMake, lint, or test setup for the app itself. Toolset v145 (VS 18).

```powershell
msbuild WinSysLive.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild WinSysLive\WinSysLive.vcxproj /p:Configuration=Debug /p:Platform=x64   # app + WTLHelper (project reference)
```

Dependencies come from the submodules. WTLHelper needs its NuGet packages (Detours, WIL, wtl), restored with `nuget restore wtlhelper\WTLHelper.sln`. WinSys needs phnt and WIL from vcpkg (user-wide integration) and must stay pinned to Windows SDK `10.0.26100.0` (see `winsys/CLAUDE.md` for why).

Include paths are `..\wtlhelper\WTLHelper` and `..\wtlhelper\WTL` (WTLHelper headers such as `WTLHelper.h`, `Settings.h` and `NativeCustomTabView.h` are included by bare name), plus `..\wtlhelper` and `..\winsys` so that the other libraries are included with a folder prefix: `<WinSys/ProcessManager.h>`, `<TreeListView/include/TreeListView.h>`. Keep that prefix. WinSys and WTLHelper both have a `SecurityHelper.h`, and WTLHelper has its own, different `TreeListView.h`. The CRT is linked statically (`MultiThreaded[Debug]`).

The include order in `pch.h` matters, and the reasons are commented there:
1. Full `windows.h` first (with `WIN32_NO_STATUS`), because `phnt_windows.h` forces `WIN32_LEAN_AND_MEAN`.
2. `phnt_windows.h`, then `#undef INITGUID`.
3. ATL/WTL.
4. `phnt.h` last, because it `#define`s `TOKEN_INFORMATION_CLASS` values that break `atlcom.h`.

`NOMINMAX` is defined at the top of `pch.h`; phnt would define it, but too late now that `windows.h` comes first.

## Architecture

- `WinSysLive.cpp`: `_tWinMain` initializes COM and `CAppModule _Module`, loads settings from the registry key `Software\ScorpioSoftware\WinSysLive`, calls `WTLHelper::InitDarkMode(...)` *before* any window is created, then runs the message loop with a single `CMainFrame`.
- `AppSettings.h`: a `Settings` subclass declared with WTLHelper's `BEGIN_SETTINGS`/`SETTING`/`DEF_SETTING` macros. The first constructed instance becomes the singleton (the global `AppSettings _Settings` in `WinSysLive.cpp`) and is reached anywhere through `AppSettings::Get()`. To add a persisted option, add a `SETTING(...)` entry plus a `DEF_SETTING(...)` accessor, and call `Save()` after changing it.
- `MainFrm.*`: `CMainFrame` (`CFrameWindowImpl` + `CAutoUpdateUI`, and it implements `IMainFrame`) hosts fixed tabs in `CMainTabView`, a `CNativeCustomTabViewImpl` with the close button turned off. Dark mode is toggled through `WTLHelper::SwitchToMode`, after which every thread window is redrawn and the status bar background is set by hand.
- Views follow TotalPE (`C:\dev\totalpe2\totalpe`):
  - `Interfaces.h` declares `IMainFrame` and `IView`. Every view derives from `CViewBase<T>` (`ViewBase.h`), which is built on WTLHelper's `CFrameView`, so each view is a child frame window with its own client area and rebar.
  - Views are heap-allocated and delete themselves when their window is destroyed.
  - Each tab's page data is the view's `CMessageMap*`. `COMMAND_TABVIEW_HANDLER(m_Tabs, 1)`, which comes first in the frame's map, routes `WM_COMMAND` to the active view's `ALT_MSG_MAP(1)`. That section must end with `CHAIN_MSG_MAP_ALT(CViewBase<T>, 1)`.
- **Refresh model:**
  - The frame owns the single `ProcessManager` (`WinSys::ProcessManager<ProcessInfoEx>`, defined in `ProcessInfoEx.h`) and one timer. The interval is the `UpdateInterval` setting, and updates can be paused.
  - On each tick the frame calls `Update()` once, then `IView::OnRefresh(active)` on **every** view. Inactive views update their data but don't redraw.
  - Views get the snapshot through `IMainFrame::GetProcessManager()` and must not take their own.
  - `ProcessInfo::CPU` is in units of 1/10000 of a percent.
- **Shared image list:** the frame owns the 16×16 image list, which the tab view also uses. Views get it with `GetImageList()`, look up icons with `GetIconIndex(IDI_*)`, and should create their lists with `LVS_SHAREIMAGELISTS`. Register a new icon in `CMainFrame::InitImageList`.
- **Processes view** (`ProcessesView.*`) uses the standalone TreeListView (`CTreeListViewCtrl`) in callback mode (`TLVN_GETDISPINFO`). Item data is a `ProcessItem*` stored in an `unordered_map`, whose element pointers stay valid. Points to watch:
  - **Removing an item:** `DeleteItem` removes the whole subtree, so `RemoveItem` re-inserts the children first.
  - **Exited processes** are shown for exactly one refresh, then removed. Their `NativeInfo` points into the previous snapshot buffer, and the next `Update()` overwrites it.
  - **Selection:** the inner list view tracks the selection by row index, so after any insert, delete or sort, restore it by item with `SelectItem(h, false)`.
  - **Flat mode** rebuilds the list with every item at the top level, because TreeListView's own flat mode sorts only within each parent.
  - **Image list:** the inner list view has `LVS_SHAREIMAGELISTS` set, since the image list belongs to the frame.
- **Services view** (`ServicesView.*`) is a plain owner-data list view built on `CVirtualListView` and `ColumnManager`, the TotalPE pattern. On each refresh it enumerates services again and merges the result by name. Each service's configuration and description cost a call to the Service Control Manager, so they're loaded on first display and cached (`ServiceItem::GetConfig`). Selection is restored by service name: `GetSaveColumnRange` returns -1 to turn off `CVirtualListView`'s own restore. Restarting a service polls with a timer rather than blocking. Double-clicking a service, or "Go to Process", calls `IMainFrame::GoToProcess`, which switches tabs and calls `IView::ShowProcess`.
- **Performance view** (`PerfView.*`, `PerfNav.*`, `PerfPages.*`): a list of pages on the left (`CPerfNav`, a custom-drawn window with a small history graph for each resource) and the selected page on the right. The selected page is saved in the `PerfPage` setting.
  - `CPerfView` takes one `PerfSample` per refresh tick from WinSys `SystemInfo` (per-processor times queried for each processor group, plus performance, basic and memory list information) and hands it to **every** page, also hidden ones, so their graphs have history when shown.
  - Pages implement `IPerfPage` through the `CPerfPage<T>` base (background, text lines below the graphs, layout): Summary, CPU, Memory and Network. To add one (Disk, GPU...), add the data to `PerfSample`, write the page class, and add a nav item and a `m_Pages` entry in the same order (`NavItem`).
  - Graphs are GraphControl's `CGraphControl`, `CGraphGrid` and `CCompositionBar` from wtlhelper, included as `<GraphControl/include/...>`. Each graph keeps its own history, so a graph shown on two pages is two controls fed the same sample.
  - Network data comes from WinSys `NetworkTracker` (`<WinSys/Network.h>`, `GetIfTable2`, about 4 ms per tick), owned by the view; the sample points to its interface list. The Network page graphs only adapters that are up, and creates or destroys their graphs as that set changes.
  - All pages are kept sized, not just the visible one, because laying out the per-processor grid is slow. The CPU page sets the grid's layout from its aspect ratio, because the grid's automatic layout is square.
- **Painting performance:** WTLHelper installs a thread-wide `WH_CALLWNDPROCRET` hook to theme new windows. While it's installed, every `SendMessage` goes through the kernel, and that roughly doubled the time to repaint the process list. For any list with many rows or callbacks, call `SuspendHookWhilePainting(hList)` from `ViewBase.h`; it removes the hook during `WM_PAINT` (`WTLHelper::SuspendHook` really unhooks). Measured per refresh tick with the Processes tab visible: ~20 ms for the snapshot (on the UI thread) and ~60 ms for painting.
- **Adding a tab** takes four steps:
  1. Add a value to `TabType` (`Interfaces.h`).
  2. Write `XxxView.h/.cpp`.
  3. Add a `case` to `CMainFrame::CreateView` (`ViewFactory.cpp`).
  4. Add an entry to `CMainFrame::s_Tabs`, an `ID_VIEW_*` command (View menu item and Ctrl+N accelerator) and a `COMMAND_ID_HANDLER(ID_VIEW_*, OnViewTab)` in the frame's message map.

## Conventions

- Every `.cpp` starts with `#include "pch.h"` (ATL/WTL and STL headers live there).
- Classes use WTL style: `C` prefix, `m_` members, message maps with the commented handler-prototype boilerplate, tabs for indentation, and opening braces on the same line.
- New command IDs go in `resource.h` and the menu in `WinSysLive.rc`. When adding a source file, register it in both `WinSysLive.vcxproj` and `WinSysLive.vcxproj.filters`.
