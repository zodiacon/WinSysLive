#include "pch.h"
#include "resource.h"

#include "PerfView.h"
#include "WTLHelper.h"

BOOL CPerfView::PreTranslateMessage(MSG* pMsg) {
	pMsg;
	return FALSE;
}

void CPerfView::OnFinalMessage(HWND /*hWnd*/) {
	delete this;
}

LRESULT CPerfView::OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CPaintDC dc(m_hWnd);

	//TODO: Add your drawing code here

	return 0;
}

LRESULT CPerfView::OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	return 1;
}
