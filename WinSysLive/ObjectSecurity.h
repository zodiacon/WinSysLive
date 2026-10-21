#pragma once

#include <aclui.h>
#include <unordered_set>
#include <functional>

namespace WinSys {
	class Service;
}

//
// What the ACL editor (CreateSecurityPage, EditSecurity) needs (ISecurityInformation): an object's security descriptor,
// read and written (WinSys ObjectInspector) through a handle with the access the caller could get, which decides what
// can be changed: without WRITE_DAC the editor is read-only, without WRITE_OWNER the owner is. The rights it lists are
// the type's (WinSys AccessMaskDecoder) and the standard ones; generic rights map through the type's generic mapping.
// Reference counted: created with one reference, which the creator releases.
//
class CObjectSecurityInfo : public ISecurityInformation {
public:
	// hObject: a handle to the object, which this keeps (null: the descriptor can't be read; the last error says why)
	CObjectSecurityInfo(HANDLE hObject, std::wstring type, std::wstring name, uint32_t access, GENERIC_MAPPING const& mapping);
	// the type and its generic mapping from the handle
	static CObjectSecurityInfo* Create(HANDLE hObject, std::wstring name, uint32_t access);
	// opens the object with the most it can of READ_CONTROL | WRITE_DAC | WRITE_OWNER (open: a new handle with the
	// access, or null with the last error); type: the object's, if it can't be opened
	static CObjectSecurityInfo* Open(std::function<HANDLE(DWORD access)> const& open, std::wstring type, std::wstring name);
	virtual ~CObjectSecurityInfo() = default;

	// IUnknown
	STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;
	STDMETHOD_(ULONG, AddRef)() override;
	STDMETHOD_(ULONG, Release)() override;

	// ISecurityInformation
	STDMETHOD(GetObjectInformation)(PSI_OBJECT_INFO pObjectInfo) override;
	STDMETHOD(GetSecurity)(SECURITY_INFORMATION RequestedInformation, PSECURITY_DESCRIPTOR* ppSecurityDescriptor, BOOL fDefault) override;
	STDMETHOD(SetSecurity)(SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR pSecurityDescriptor) override;
	STDMETHOD(GetAccessRights)(const GUID* pguidObjectType, DWORD dwFlags, PSI_ACCESS* ppAccess, ULONG* pcAccesses,
		ULONG* piDefaultAccess) override;
	STDMETHOD(MapGeneric)(const GUID* pguidObjectType, UCHAR* pAceFlags, ACCESS_MASK* pMask) override;
	STDMETHOD(GetInheritTypes)(PSI_INHERIT_TYPE* ppInheritTypes, ULONG* pcInheritTypes) override;
	STDMETHOD(PropertySheetPageCallback)(HWND hwnd, UINT uMsg, SI_PAGE_TYPE uPage) override;

private:
	wil::unique_handle m_hObject;
	std::wstring m_Type, m_Name;
	uint32_t m_Access;
	GENERIC_MAPPING m_Mapping;
	std::vector<std::wstring> m_RightNames;	// SI_ACCESS points into these
	std::vector<SI_ACCESS> m_Rights;
	ULONG m_RefCount{ 1 };
	DWORD m_Error{ 0 };	// why there's no handle
};

//
// A service's: the SCM's objects aren't kernel objects, so the descriptor goes through WinSys Service
// (QueryServiceObjectSecurity), with a service's rights and generic mapping
//
class CServiceSecurityInfo : public CObjectSecurityInfo {
public:
	// opened with the most of READ_CONTROL, WRITE_DAC and WRITE_OWNER it allows
	static CServiceSecurityInfo* Open(std::wstring const& name, std::wstring displayName);
	~CServiceSecurityInfo() override;

	STDMETHOD(GetSecurity)(SECURITY_INFORMATION RequestedInformation, PSECURITY_DESCRIPTOR* ppSecurityDescriptor, BOOL fDefault) override;
	STDMETHOD(SetSecurity)(SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR pSecurityDescriptor) override;

private:
	CServiceSecurityInfo(std::unique_ptr<WinSys::Service> service, std::wstring displayName, uint32_t access, DWORD error);

	std::unique_ptr<WinSys::Service> m_Service;
	DWORD m_OpenError;	// why there's no service
};

//
// The ACL editor's own dialogs (Advanced, a permission entry, Select User or Group, EditSecurity's sheet) aren't created
// through anything their owner sees, and the theming hook leaves their title bars light. While an object of this class
// is installed (a member of the sheet that shows the editor, installed once the sheet exists), a CBT hook on its thread
// applies the current mode (WTLHelper::ApplyMode) to each window the owner owns, directly or through such a dialog,
// when it's first activated: from a zero-delay thread timer, as a dialog builds its contents after it's activated.
// The Advanced dialog is DirectUI: its buttons, tab and edges are drawn with the light visual style whatever theme
// their windows are given. One per thread at a time (the last installed is the one used).
//
class CAclDialogTheming {
public:
	CAclDialogTheming() = default;
	CAclDialogTheming(CAclDialogTheming const&) = delete;
	CAclDialogTheming& operator=(CAclDialogTheming const&) = delete;
	~CAclDialogTheming();

	void Install(HWND owner);
	void Uninstall();

private:
	static LRESULT CALLBACK CbtHook(int code, WPARAM wParam, LPARAM lParam);
	static void CALLBACK OnTimer(HWND, UINT, UINT_PTR id, DWORD);

	inline static thread_local CAclDialogTheming* s_Current{ nullptr };
	CAclDialogTheming* m_Previous{ nullptr };
	HHOOK m_Hook{ nullptr };
	HWND m_Owner{ nullptr };
	std::unordered_set<HWND> m_Themed;
	std::vector<HWND> m_Pending;	// activated, to theme from the timer
};

// the ACL editor's page brings no icon the sheet knows (CIconPropertySheetImpl): the system's shield on its tab, once
// the sheet is initialized
void SetSecurityTabIcon(HWND sheet, int tab);
