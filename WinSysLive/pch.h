#pragma once

#define WINVER			0x0A00
#define _WIN32_WINNT	0x0A00
#define _WIN32_IE		0x0A00
#define _RICHEDIT_VER	0x0500

// The include order below is load-bearing:
// 1. windows.h in full first: phnt_windows.h would force WIN32_LEAN_AND_MEAN, which ATL/WTL can't live with.
//    WIN32_NO_STATUS lets phnt_windows.h pull in ntstatus.h without redefinitions.
// 2. phnt_windows.h defines INITGUID; undo it so later DEFINE_GUIDs are declarations again (WTL defines some IIDs itself).
// 3. ATL/WTL before phnt.h: phnt.h #defines TOKEN_INFORMATION_CLASS values that break atlcom.h.
#define NOMINMAX	// phnt_windows.h would define it, but windows.h is included first (see above)
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#define PHNT_MODE 1
#define PHNT_VERSION PHNT_THRESHOLD

#include <phnt_windows.h>
#undef INITGUID
#include <guiddef.h>

#include <atlbase.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlstr.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlctrlx.h>
#include <atltheme.h>
#include <atlsplit.h>
#include <atlctrlw.h>
#include <atltypes.h>

#define __ImageBase __ImageBase_phnt	// phnt declares it const, WIL doesn't
#include <phnt.h>
#undef __ImageBase

#include <ShellScalingApi.h>
#include <Psapi.h>
#include <VersionHelpers.h>
#include <sddl.h>

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <optional>
#include <algorithm>
#include <format>

#include <wil\resource.h>

#if defined _M_IX86
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#pragma comment(lib, "ntdll")
