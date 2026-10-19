#pragma once

#include "ViewBase.h"

class CServicesView :
	public CViewBase<CServicesView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;

	BEGIN_MSG_MAP(CServicesView)
		CHAIN_MSG_MAP(CViewBase<CServicesView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CServicesView>, 1)
	END_MSG_MAP()
};
