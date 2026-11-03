
#pragma once
#include <afxwin.h>
#include <vector>

inline std::vector<WORD> BuildDialogTemplate()
{
    std::vector<WORD> data;

    auto addDword = [&data](DWORD value) {
        data.push_back(static_cast<WORD>(value & 0xFFFF));
        data.push_back(static_cast<WORD>(value >> 16));
        };

    auto addString = [&data](const wchar_t* value) {
        while (*value)
            data.push_back(static_cast<WORD>(*value++));
        data.push_back(0);
        };

    addDword(WS_POPUP | WS_CAPTION | WS_SYSMENU |
        DS_MODALFRAME | DS_SETFONT);
    addDword(0);       // Extended style
    data.push_back(0); // Number of template controls
    data.push_back(10); // X
    data.push_back(10); // Y
    data.push_back(230); // Width
    data.push_back(165); // Height
    data.push_back(0); // No menu
    data.push_back(0); // Default dialog class
    addString(L"ChatProj");
    data.push_back(9); // Font size
    addString(L"Segoe UI");

    return data;
}
