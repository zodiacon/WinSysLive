#pragma once

//
// A COM permission (an app ID's LaunchPermission or AccessPermission, or a machine-wide default or limit) in the
// system's ACL editor, read-only: the rights as dcomcnfg names them (Local and Remote Launch and Activation, Local and
// Remote Access), each with COM_RIGHTS_EXECUTE, as COM writes them. The COM Registration and DCOM dialogs use it.
//
// sd: a self-relative security descriptor; name: the editor's title is "Permissions for <name>"; launch: launch and
// activation rights, else access rights
void ShowComPermissions(HWND owner, std::vector<uint8_t> sd, std::wstring const& name, bool launch);
