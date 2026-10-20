#pragma once

// the icon (IDI_*, in the frame's image list) for a handle of the object type, with the name WinSys
// HandleNames::QueryName gave it: files keep NT names only for devices and pipes, which get the device icon
UINT GetHandleIcon(std::wstring const& type, std::wstring const& name);
