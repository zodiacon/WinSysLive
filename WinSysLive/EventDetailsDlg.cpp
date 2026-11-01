#include "pch.h"
#include "EventDetailsDlg.h"
#include "FormatHelper.h"
#include <IconHelper.h>
#include <ClipboardHelper.h>
#include <WinSys/Helpers.h>

using namespace WinSys;

namespace {
	// the field controls with their names, for Copy
	const struct {
		int Id;
		PCWSTR Name;
	} s_Fields[] = {
		{ IDC_ED_LOG, L"Log" }, { IDC_ED_SOURCE, L"Source" }, { IDC_ED_LEVEL, L"Level" }, { IDC_ED_EVENTID, L"Event ID" },
		{ IDC_ED_TIME, L"Time" }, { IDC_ED_RECORDID, L"Record ID" }, { IDC_ED_PROCESS, L"Process" }, { IDC_ED_THREAD, L"Thread" },
		{ IDC_ED_COMPUTER, L"Computer" }, { IDC_ED_KEYWORDS, L"Keywords" },
	};
}

LRESULT CEventDetailsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_EVENT, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_EVENT, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);
	// the title: the dialog's font, bold and larger
	LOGFONT lf;
	CFontHandle(GetFont()).GetLogFont(&lf);
	lf.lfWeight = FW_SEMIBOLD;
	lf.lfHeight = lf.lfHeight * 5 / 4;
	m_TitleFont.CreateFontIndirect(&lf);
	GetDlgItem(IDC_ED_TITLE).SetFont(m_TitleFont);

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	ShowEntry();
	// focused, but not selected (a read-only edit selects all its text when focused)
	GotoDlgCtrl(GetDlgItem(IDC_ED_MESSAGE));
	SendDlgItemMessage(IDC_ED_MESSAGE, EM_SETSEL, 0, 0);
	return FALSE;
}

LRESULT CEventDetailsDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	bHandled = FALSE;
	return 0;
}

void CEventDetailsDlg::ShowEntry() {
	auto& e = m_Entry;
	m_Xml.reset();
	auto process = m_Callbacks.ProcessName ? m_Callbacks.ProcessName(e) : std::wstring();
	SetDlgItemText(IDC_ED_TITLE, std::format(L"{} {}", e.Provider, e.EventId).c_str());
	SetDlgItemText(IDC_ED_LOG, e.Channel.c_str());
	SetDlgItemText(IDC_ED_SOURCE, e.Provider.c_str());
	SetDlgItemText(IDC_ED_LEVEL, EventLog::LevelText(e));
	SetDlgItemText(IDC_ED_EVENTID, std::to_wstring(e.EventId).c_str());
	SetDlgItemText(IDC_ED_TIME, FormatHelper::FormatDateTime(e.Time).c_str());
	SetDlgItemText(IDC_ED_RECORDID, std::to_wstring(e.RecordId).c_str());
	SetDlgItemText(IDC_ED_PROCESS, (e.ProcessId ? std::to_wstring(e.ProcessId) + (process.empty() ? L"" : L" (" + process + L")") : std::wstring()).c_str());
	SetDlgItemText(IDC_ED_THREAD, e.ThreadId ? std::to_wstring(e.ThreadId).c_str() : L"");
	SetDlgItemText(IDC_ED_COMPUTER, e.Computer.c_str());
	SetDlgItemText(IDC_ED_KEYWORDS, std::format(L"0x{:X}", e.Keywords).c_str());

	// the level's icon: the system's stock ones (IconId), the app's key for the Security log's audits (as the list)
	IconId icon = e.IsAuditFailure() || e.IsAuditSuccess() ? IconId(IDI_KEY)
		: e.Level == 1 || e.Level == 2 ? IconId(IDI_ERROR) : e.Level == 3 ? IconId(IDI_WARNING) : IconId(IDI_INFORMATION);
	CRect rc;
	GetDlgItem(IDC_ED_ICON).GetClientRect(&rc);
	if (m_Icon)
		m_Icon.DestroyIcon();
	m_Icon.Attach(IconHelper::Load(icon, rc.Width()));
	CStatic(GetDlgItem(IDC_ED_ICON)).SetIcon(m_Icon);

	GetDlgItem(IDC_ED_GOTOPROCESS).EnableWindow(!process.empty());
	ShowText();
}

void CEventDetailsDlg::ShowText() {
	if (IsDlgButtonChecked(IDC_ED_XML) == BST_CHECKED)
		SetDlgItemText(IDC_ED_MESSAGE, WithCrLf(Xml()).c_str());
	else {
		auto& message = m_Entry.Message;
		SetDlgItemText(IDC_ED_MESSAGE, message.empty() ? L"(no message: the source's resources have none for this event; Show XML has its data)"
			: WithCrLf(message).c_str());
	}
	// from the top, nothing selected
	SendDlgItemMessage(IDC_ED_MESSAGE, EM_SETSEL, 0, 0);
	SendDlgItemMessage(IDC_ED_MESSAGE, EM_SCROLLCARET);
}

std::wstring const& CEventDetailsDlg::Xml() {
	if (!m_Xml) {
		CWaitCursor wait;
		auto xml = EventLog::GetXml(m_Entry.Channel, m_Entry.RecordId);
		m_Xml = xml ? Indent(*xml) : L"(the event can't be read: " + Helpers::GetErrorText(::GetLastError()) + L")";
	}
	return *m_Xml;
}

std::wstring CEventDetailsDlg::WithCrLf(std::wstring const& text) {
	std::wstring result;
	result.reserve(text.size() + text.size() / 16);
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r'))
			result += L'\r';
		result += text[i];
	}
	return result;
}

std::wstring CEventDetailsDlg::Indent(std::wstring const& xml) {
	// a line per element, indented by its depth; an element's text stays on its line (<Data Name='x'>value</Data>)
	std::wstring result;
	int depth = 0;
	size_t i = 0;
	while (i < xml.size()) {
		if (xml[i] != L'<') {
			// text: up to the next tag
			auto next = xml.find(L'<', i);
			result.append(xml, i, next == std::wstring::npos ? std::wstring::npos : next - i);
			i = next == std::wstring::npos ? xml.size() : next;
			continue;
		}
		auto end = xml.find(L'>', i);
		if (end == std::wstring::npos) {
			result.append(xml, i);
			break;
		}
		auto tag = xml.substr(i, end - i + 1);
		bool closing = tag.starts_with(L"</");
		bool selfClosing = tag.ends_with(L"/>") || tag.starts_with(L"<?") || tag.starts_with(L"<!");
		// a closing tag right after its text stays on the text's line
		bool afterText = !result.empty() && result.back() != L'>' && result.back() != L'\n';
		if (closing)
			depth--;
		if (!(closing && afterText)) {
			if (!result.empty())
				result += L'\n';
			result.append((size_t)(std::max)(depth, 0) * 2, L' ');
		}
		result += tag;
		if (!closing && !selfClosing)
			depth++;
		i = end + 1;
	}
	return result;
}

std::wstring CEventDetailsDlg::FieldsText() const {
	std::wstring text;
	for (auto& f : s_Fields) {
		CString value;
		GetDlgItemText(f.Id, value);
		text += std::format(L"{}: {}\r\n", f.Name, (PCWSTR)value);
	}
	return text;
}

LRESULT CEventDetailsDlg::OnShowXml(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	ShowText();
	return 0;
}

LRESULT CEventDetailsDlg::OnMove(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!m_Callbacks.Move)
		return 0;
	if (auto entry = m_Callbacks.Move(m_Entry, id == IDC_ED_NEXT ? 1 : -1)) {
		m_Entry = std::move(*entry);
		ShowEntry();
	}
	else
		::MessageBeep(MB_OK);
	return 0;
}

LRESULT CEventDetailsDlg::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!m_Callbacks.GoToProcess || !m_Entry.ProcessId)
		return 0;
	auto pid = m_Entry.ProcessId;
	EndDialog(IDOK);
	m_Callbacks.GoToProcess(pid);
	return 0;
}

LRESULT CEventDetailsDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto text = IsDlgButtonChecked(IDC_ED_XML) == BST_CHECKED ? WithCrLf(Xml()) : FieldsText() + L"\r\n" + WithCrLf(m_Entry.Message);
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CEventDetailsDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
