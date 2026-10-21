#include "pch.h"
#include "StackDlg.h"
#include <ClipboardHelper.h>

CStackDlg::CStackDlg(uint32_t pid, int64_t created, uint32_t tid, std::wstring name) :
	m_Pid(pid), m_Tid(tid), m_Created(created), m_Name(std::move(name)) {
}

LRESULT CStackDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetWindowText(std::format(L"Stack: Thread {} ({}, {})", m_Tid, m_Name, m_Pid).c_str());
	m_List.Attach(GetDlgItem(IDC_STACK_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	m_List.InsertColumn(0, L"#", LVCFMT_RIGHT, width(35));
	m_List.InsertColumn(1, L"Address", LVCFMT_RIGHT, width(130));
	m_List.InsertColumn(2, L"Symbol", LVCFMT_LEFT, width(380));
	m_List.InsertColumn(3, L"Source", LVCFMT_LEFT, width(300));
	m_List.InsertColumn(4, L"Module", LVCFMT_LEFT, width(300));
	m_List.InsertColumn(5, L"Symbols", LVCFMT_LEFT, width(300));

	DlgResize_Init(true, true);
	CenterWindow(GetParent());
	Capture();
	return TRUE;
}

void CStackDlg::Capture() {
	GetDlgItem(IDC_STACK_REFRESH).EnableWindow(FALSE);
	SetDlgItemText(IDC_STACK_STATUS, L"Capturing the stack...");
	SymbolEngine::Get().CaptureStack(m_Pid, m_Created, m_Tid, m_hWnd);
}

LRESULT CStackDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// captures posted before the dialog closed: nothing else would free them
	MSG msg;
	while (::PeekMessage(&msg, m_hWnd, WM_STACK_CAPTURED, WM_STACK_CAPTURED, PM_REMOVE))
		delete reinterpret_cast<StackCapture*>(msg.lParam);
	bHandled = FALSE;
	return 0;
}

LRESULT CStackDlg::OnStackCaptured(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	std::unique_ptr<StackCapture> capture(reinterpret_cast<StackCapture*>(lParam));
	GetDlgItem(IDC_STACK_REFRESH).EnableWindow(TRUE);
	m_Frames = std::move(capture->Frames);

	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	for (int i = 0; i < (int)m_Frames.size(); i++) {
		auto& f = m_Frames[i];
		int n = m_List.InsertItem(i, std::to_wstring(i).c_str());
		m_List.SetItemText(n, 1, (f.Is32Bit ? std::format(L"0x{:08X}", f.Address) : FormatAddress(f.Address)).c_str());
		m_List.SetItemText(n, 2, f.Symbol.c_str());
		m_List.SetItemText(n, 3, f.Source.c_str());
		m_List.SetItemText(n, 4, f.Module.c_str());
		m_List.SetItemText(n, 5, GetSymbolsText(f).c_str());
	}
	m_List.SetRedraw(TRUE);

	if (!capture->Error.empty())
		SetDlgItemText(IDC_STACK_STATUS, capture->Error.c_str());
	else {
		auto frames32 = std::ranges::count_if(m_Frames, &StackFrame::Is32Bit);
		auto text = std::format(L"{} frames", m_Frames.size());
		if (frames32)
			text += std::format(L" ({} of them 32-bit, after the native ones)", frames32);
		if (!SymbolEngine::Get().IsEnabled())
			text += L"    Symbols aren't resolved (Options > Resolve Symbols)";
		else {
			// the modules whose frames show exports or offsets only: their PDBs weren't found (or downloaded)
			std::vector<std::wstring> missing;
			for (auto& f : m_Frames) {
				if (f.Module.empty() || (f.Symbols != WinSys::ModuleSymbolsKind::Exports && f.Symbols != WinSys::ModuleSymbolsKind::None))
					continue;
				auto slash = f.Module.rfind(L'\\');
				auto name = slash == std::wstring::npos ? f.Module : f.Module.substr(slash + 1);
				if (std::ranges::find(missing, name) == missing.end())
					missing.push_back(std::move(name));
			}
			if (!missing.empty()) {
				text += L"    No PDB for: ";
				for (size_t i = 0; i < missing.size(); i++)
					text += (i ? L", " : L"") + missing[i];
			}
		}
		SetDlgItemText(IDC_STACK_STATUS, text.c_str());
	}
	GetDlgItem(IDC_STACK_COPY).EnableWindow(!m_Frames.empty());
	return 0;
}

std::wstring CStackDlg::GetSymbolsText(StackFrame const& frame) {
	if (frame.Module.empty())
		return L"";
	using enum WinSys::ModuleSymbolsKind;
	switch (frame.Symbols) {
		case Pdb: case Other: return frame.SymbolFile.empty() ? L"Loaded" : frame.SymbolFile;
		case Exports: return L"Exports only (no PDB)";
		case None: return L"None";
	}
	return L"Not loaded";
}

LRESULT CStackDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto index = reinterpret_cast<NMITEMACTIVATE*>(hdr)->iItem;
	if (index < 0 || index >= (int)m_Frames.size() || m_Frames[index].Module.empty())
		return 0;
	// the module's folder, with the file selected
	::ShellExecute(m_hWnd, L"open", L"explorer.exe", std::format(L"/select,\"{}\"", m_Frames[index].Module).c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CStackDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Capture();
	return 0;
}

LRESULT CStackDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text;
	for (int i = 0; i < (int)m_Frames.size(); i++) {
		auto& f = m_Frames[i];
		text += std::format(L"{}\t{}\t{}\t{}\t{}\t{}\r\n", i, f.Is32Bit ? std::format(L"0x{:08X}", f.Address) : FormatAddress(f.Address),
			f.Symbol, f.Source, f.Module, GetSymbolsText(f));
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CStackDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// a capture still on its way is posted to a window that's gone, and the engine frees it
	EndDialog(wID);
	return 0;
}
