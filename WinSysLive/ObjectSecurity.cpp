#include "pch.h"
#include <DpiHelper.h>
#include "ObjectSecurity.h"
#include <WinSys/ObjectDetails.h>
#include <WinSys/AccessMask.h>
#include <WTLHelper.h>

#pragma comment(lib, "aclui")

using namespace WinSys;

CObjectSecurityInfo::CObjectSecurityInfo(HANDLE hObject, std::wstring type, std::wstring name, uint32_t access,
	GENERIC_MAPPING const& mapping) :
	m_hObject(hObject), m_Type(std::move(type)), m_Name(std::move(name)), m_Access(access), m_Mapping(mapping) {
	if (!hObject)
		m_Error = ::GetLastError();

	// full control and the generic rights as the type maps them (the basic page's), then each right (the advanced page's)
	auto specific = AccessMaskDecoder::GetSpecificRights(m_Type);
	auto standard = AccessMaskDecoder::GetStandardRights();
	// SI_ACCESS points into the names: no reallocation
	m_RightNames.reserve(4 + specific.size() + standard.size());
	auto add = [&](uint32_t mask, std::wstring name, DWORD flags) {
		if (mask == 0)
			return;
		m_RightNames.push_back(std::move(name));
		m_Rights.push_back({ &GUID_NULL, mask, m_RightNames.back().c_str(), flags });
	};
	add(m_Mapping.GenericAll, L"Full Control", SI_ACCESS_GENERAL | SI_ACCESS_SPECIFIC);
	add(m_Mapping.GenericRead, L"Read", SI_ACCESS_GENERAL);
	add(m_Mapping.GenericWrite, L"Write", SI_ACCESS_GENERAL);
	add(m_Mapping.GenericExecute, L"Execute", SI_ACCESS_GENERAL);
	for (auto& r : specific)
		add(r.Mask, r.Name, SI_ACCESS_SPECIFIC);
	for (auto& r : standard)
		add(r.Mask, r.Name, SI_ACCESS_SPECIFIC);
}

STDMETHODIMP CObjectSecurityInfo::QueryInterface(REFIID riid, void** ppv) {
	if (!ppv)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_ISecurityInformation) {
		*ppv = static_cast<ISecurityInformation*>(this);
		AddRef();
		return S_OK;
	}
	*ppv = nullptr;
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CObjectSecurityInfo::AddRef() {
	return ++m_RefCount;
}

STDMETHODIMP_(ULONG) CObjectSecurityInfo::Release() {
	auto count = --m_RefCount;
	if (count == 0)
		delete this;
	return count;
}

STDMETHODIMP CObjectSecurityInfo::GetObjectInformation(PSI_OBJECT_INFO pObjectInfo) {
	pObjectInfo->dwFlags = SI_ADVANCED | SI_EDIT_PERMS | SI_EDIT_OWNER | SI_PAGE_TITLE;
	// what the handle's access allows
	if (!(m_Access & WRITE_DAC))
		pObjectInfo->dwFlags |= SI_READONLY;
	if (!(m_Access & WRITE_OWNER))
		pObjectInfo->dwFlags |= SI_OWNER_READONLY;
	pObjectInfo->hInstance = nullptr;
	pObjectInfo->pszServerName = nullptr;
	pObjectInfo->pszObjectName = const_cast<PWSTR>(m_Name.c_str());
	pObjectInfo->pszPageTitle = const_cast<PWSTR>(L"Security");
	return S_OK;
}

STDMETHODIMP CObjectSecurityInfo::GetSecurity(SECURITY_INFORMATION RequestedInformation, PSECURITY_DESCRIPTOR* ppSecurityDescriptor,
	BOOL fDefault) {
	if (fDefault)
		return E_NOTIMPL;
	if (!m_hObject)
		return HRESULT_FROM_WIN32(m_Error);
	// a SACL needs ACCESS_SYSTEM_SECURITY, which a handle hardly ever has; the label is read with READ_CONTROL
	auto sd = ObjectInspector::GetSecurity(m_hObject.get(), RequestedInformation & (OWNER_SECURITY_INFORMATION
		| GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION));
	if (sd.empty())
		return HRESULT_FROM_WIN32(::GetLastError());
	// the editor frees it with LocalFree
	auto copy = ::LocalAlloc(LPTR, sd.size());
	if (!copy)
		return E_OUTOFMEMORY;
	memcpy(copy, sd.data(), sd.size());
	*ppSecurityDescriptor = copy;
	return S_OK;
}

STDMETHODIMP CObjectSecurityInfo::SetSecurity(SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR pSecurityDescriptor) {
	if (!m_hObject)
		return HRESULT_FROM_WIN32(m_Error);
	if (!ObjectInspector::SetSecurity(m_hObject.get(), SecurityInformation, pSecurityDescriptor))
		return HRESULT_FROM_WIN32(::GetLastError());
	return S_OK;
}

STDMETHODIMP CObjectSecurityInfo::GetAccessRights(const GUID* /*pguidObjectType*/, DWORD /*dwFlags*/, PSI_ACCESS* ppAccess,
	ULONG* pcAccesses, ULONG* piDefaultAccess) {
	*ppAccess = m_Rights.data();
	*pcAccesses = (ULONG)m_Rights.size();
	// Read, for a new entry
	*piDefaultAccess = m_Rights.size() > 1 ? 1 : 0;
	return S_OK;
}

STDMETHODIMP CObjectSecurityInfo::MapGeneric(const GUID* /*pguidObjectType*/, UCHAR* /*pAceFlags*/, ACCESS_MASK* pMask) {
	::MapGenericMask(pMask, &m_Mapping);
	return S_OK;
}

STDMETHODIMP CObjectSecurityInfo::GetInheritTypes(PSI_INHERIT_TYPE* ppInheritTypes, ULONG* pcInheritTypes) {
	// not a container: nothing to inherit
	*ppInheritTypes = nullptr;
	*pcInheritTypes = 0;
	return S_OK;
}

STDMETHODIMP CObjectSecurityInfo::PropertySheetPageCallback(HWND /*hwnd*/, UINT /*uMsg*/, SI_PAGE_TYPE /*uPage*/) {
	return S_OK;
}

CObjectSecurityInfo* CObjectSecurityInfo::Create(HANDLE hObject, std::wstring name, uint32_t access) {
	std::wstring type;
	auto stats = hObject ? ObjectInspector::QueryTypeStatistics(hObject, &type) : std::nullopt;
	return new CObjectSecurityInfo(hObject, std::move(type), std::move(name), access, stats ? stats->GenericMapping : GENERIC_MAPPING{});
}

CObjectSecurityInfo* CObjectSecurityInfo::Open(std::function<HANDLE(DWORD access)> const& open, std::wstring type, std::wstring name) {
	for (DWORD access : { READ_CONTROL | WRITE_DAC | WRITE_OWNER, READ_CONTROL | WRITE_DAC, READ_CONTROL }) {
		if (auto h = open(access))
			return Create(h, std::move(name), access);
	}
	// the editor shows why
	return new CObjectSecurityInfo(nullptr, std::move(type), std::move(name), 0, GENERIC_MAPPING{});
}

CAclDialogTheming::~CAclDialogTheming() {
	Uninstall();
}

void CAclDialogTheming::Install(HWND owner) {
	if (m_Hook)
		return;
	m_Owner = owner;
	m_Hook = ::SetWindowsHookEx(WH_CBT, CbtHook, nullptr, ::GetCurrentThreadId());
	if (m_Hook) {
		m_Previous = s_Current;
		s_Current = this;
	}
}

void CAclDialogTheming::Uninstall() {
	if (!m_Hook)
		return;
	::UnhookWindowsHookEx(m_Hook);
	m_Hook = nullptr;
	if (s_Current == this)
		s_Current = m_Previous;
	m_Pending.clear();
}

LRESULT CALLBACK CAclDialogTheming::CbtHook(int code, WPARAM wParam, LPARAM lParam) {
	// activated: not yet built (its title isn't even final)
	if (auto current = s_Current; code == HCBT_ACTIVATE && current) {
		auto hWnd = (HWND)wParam;
		if (hWnd != current->m_Owner && !current->m_Themed.contains(hWnd)) {
			// owned by the owner, or by a dialog it owns
			for (auto owner = ::GetWindow(hWnd, GW_OWNER); owner; owner = ::GetWindow(owner, GW_OWNER)) {
				if (owner == current->m_Owner) {
					current->m_Themed.insert(hWnd);
					current->m_Pending.push_back(hWnd);
					::SetTimer(nullptr, 0, 0, OnTimer);
					break;
				}
			}
		}
	}
	return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

void CALLBACK CAclDialogTheming::OnTimer(HWND, UINT, UINT_PTR id, DWORD) {
	::KillTimer(nullptr, id);
	if (auto current = s_Current) {
		auto pending = std::move(current->m_Pending);
		current->m_Pending.clear();
		for (auto hWnd : pending)
			if (::IsWindow(hWnd))
				WTLHelper::ApplyMode(hWnd);
	}
}

void SetSecurityTabIcon(HWND sheet, int tab) {
	auto size = DpiHelper::SmallIconSize(sheet);
	CIcon shield;
	if (FAILED(::LoadIconWithScaleDown(nullptr, IDI_SHIELD, size, size, &shield.m_hIcon)))
		return;
	CTabCtrl tabs(CPropertySheetWindow(sheet).GetTabControl());
	CImageList images(tabs.GetImageList());
	TCITEM item{ TCIF_IMAGE };
	item.iImage = images ? images.AddIcon(shield) : -1;
	tabs.SetItem(tab, &item);
}
