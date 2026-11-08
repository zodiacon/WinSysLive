#include "pch.h"
#include "ComPermissionInfo.h"
#include <aclui.h>

#pragma comment(lib, "aclui")

namespace {
	//
	// A COM permission (an app ID's LaunchPermission or AccessPermission, or the machine's default) for the ACL editor,
	// read-only: the rights as dcomcnfg names them, each with COM_RIGHTS_EXECUTE, as COM writes them
	//
	class CComPermissionInfo : public ISecurityInformation {
	public:
		CComPermissionInfo(std::vector<uint8_t> sd, std::wstring name, bool launch)
			: m_Sd(std::move(sd)), m_Name(std::move(name)), m_Launch(launch) {
		}
		virtual ~CComPermissionInfo() = default;

		STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
			if (riid == IID_IUnknown || riid == IID_ISecurityInformation) {
				*ppv = static_cast<ISecurityInformation*>(this);
				AddRef();
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		STDMETHOD_(ULONG, AddRef)() override {
			return ++m_RefCount;
		}
		STDMETHOD_(ULONG, Release)() override {
			auto count = --m_RefCount;
			if (count == 0)
				delete this;
			return count;
		}

		STDMETHOD(GetObjectInformation)(PSI_OBJECT_INFO info) override {
			*info = {};
			info->dwFlags = SI_READONLY | SI_ADVANCED | SI_NO_ACL_PROTECT | SI_NO_TREE_APPLY;
			info->hInstance = _Module.GetResourceInstance();
			info->pszObjectName = m_Name.data();
			return S_OK;
		}
		STDMETHOD(GetSecurity)(SECURITY_INFORMATION, PSECURITY_DESCRIPTOR* ppSD, BOOL) override {
			// the editor frees it with LocalFree
			auto copy = ::LocalAlloc(LPTR, m_Sd.size());
			if (!copy)
				return E_OUTOFMEMORY;
			memcpy(copy, m_Sd.data(), m_Sd.size());
			*ppSD = copy;
			return S_OK;
		}
		STDMETHOD(SetSecurity)(SECURITY_INFORMATION, PSECURITY_DESCRIPTOR) override {
			return E_ACCESSDENIED;
		}
		STDMETHOD(GetAccessRights)(const GUID*, DWORD, PSI_ACCESS* access, ULONG* count, ULONG* defaultAccess) override {
			static SI_ACCESS launch[] = {
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_LOCAL, L"Local Launch", SI_ACCESS_GENERAL },
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_REMOTE, L"Remote Launch", SI_ACCESS_GENERAL },
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_ACTIVATE_LOCAL, L"Local Activation", SI_ACCESS_GENERAL },
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_ACTIVATE_REMOTE, L"Remote Activation", SI_ACCESS_GENERAL },
			};
			static SI_ACCESS accessRights[] = {
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_LOCAL, L"Local Access", SI_ACCESS_GENERAL },
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_REMOTE, L"Remote Access", SI_ACCESS_GENERAL },
			};
			*access = m_Launch ? launch : accessRights;
			*count = m_Launch ? _countof(launch) : _countof(accessRights);
			*defaultAccess = 0;
			return S_OK;
		}
		STDMETHOD(MapGeneric)(const GUID*, UCHAR*, ACCESS_MASK*) override {
			return S_OK;
		}
		STDMETHOD(GetInheritTypes)(PSI_INHERIT_TYPE*, ULONG*) override {
			return E_NOTIMPL;
		}
		STDMETHOD(PropertySheetPageCallback)(HWND, UINT, SI_PAGE_TYPE) override {
			return S_OK;
		}

	private:
		std::vector<uint8_t> m_Sd;
		std::wstring m_Name;
		bool m_Launch;
		ULONG m_RefCount{ 1 };
	};
}

void ShowComPermissions(HWND owner, std::vector<uint8_t> sd, std::wstring const& name, bool launch) {
	auto info = new CComPermissionInfo(std::move(sd), name, launch);
	::EditSecurity(owner, info);
	info->Release();
}
