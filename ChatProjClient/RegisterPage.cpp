#include "pch.h"
#include "framework.h"
#include "RegisterPage.h"
#include "DatabaseService.h"

#include <string>
#include <atlconv.h>
#include "DialogTemplateHelper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    constexpr int ID_REG_USERNAME = 2101;
    constexpr int ID_REG_EMAIL = 2102;
    constexpr int ID_REG_PASSWORD = 2104;
    constexpr int ID_REG_CONFIRM = 2105;
    constexpr int ID_REG_SUBMIT = 2106;
    constexpr int ID_REG_SHOW_PASSWORD = 2107;
    constexpr int ID_REG_SHOW_CONFIRM = 2108;
    constexpr int ID_REG_TITLE = 2110;
    constexpr int ID_REG_SUBTITLE = 2111;
    constexpr int ID_REG_USERNAME_LABEL = 2112;
    constexpr int ID_REG_EMAIL_LABEL = 2113;
    constexpr int ID_REG_PASSWORD_LABEL = 2114;
    constexpr int ID_REG_CONFIRM_LABEL = 2115;
    constexpr int ID_REG_FOOTER = 2116;

    const COLORREF COLOR_PAGE = RGB(235, 244, 255);
    const COLORREF COLOR_BLUE = RGB(37, 99, 235);
    const COLORREF COLOR_BLUE_DARK = RGB(29, 78, 216);
    const COLORREF COLOR_TEXT = RGB(30, 41, 59);
    const COLORREF COLOR_MUTED = RGB(100, 116, 139);
    const COLORREF COLOR_WHITE = RGB(255, 255, 255);
    const COLORREF COLOR_BORDER = RGB(203, 218, 238);

    std::string ToUtf8(const CString& value)
    {
        CT2A converted(value, CP_UTF8);
        return std::string(converted);
    }
}

BEGIN_MESSAGE_MAP(CRegisterPage, CDialogEx)
    ON_BN_CLICKED(ID_REG_SUBMIT, &CRegisterPage::OnRegisterClicked)
    ON_BN_CLICKED(ID_REG_SHOW_PASSWORD, &CRegisterPage::OnTogglePassword)
    ON_BN_CLICKED(ID_REG_SHOW_CONFIRM, &CRegisterPage::OnToggleConfirmPassword)
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

CRegisterPage::CRegisterPage(CWnd* pParent)
    : CDialogEx(),
    m_dialogTemplate(BuildDialogTemplate()),
    m_backgroundBrush(COLOR_PAGE),
    m_editBrush(COLOR_WHITE)
{
    InitModalIndirect(
        reinterpret_cast<DLGTEMPLATE*>(m_dialogTemplate.data()),
        pParent);
}

BOOL CRegisterPage::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetWindowTextW(L"ChatProj | Create Account");

    // Open as a real application window rather than a small floating dialog.
    ModifyStyle(DS_MODALFRAME, WS_THICKFRAME | WS_MAXIMIZEBOX, SWP_FRAMECHANGED);
    ShowWindow(SW_MAXIMIZE);

    m_titleFont.CreatePointFont(260, L"Segoe UI Semibold");
    m_subtitleFont.CreatePointFont(105, L"Segoe UI");
    m_uiFont.CreatePointFont(115, L"Segoe UI");
    m_labelFont.CreatePointFont(105, L"Segoe UI Semibold");

    m_title.Create(L"Create your account", WS_CHILD | WS_VISIBLE | SS_LEFT,
        CRect(0, 0, 10, 10), this, ID_REG_TITLE);
    m_title.SetFont(&m_titleFont);

    m_subtitle.Create(L"Join ChatProj to start chatting with your people.",
        WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this, ID_REG_SUBTITLE);
    m_subtitle.SetFont(&m_subtitleFont);

    auto makeLabel = [this](CStatic& label, const wchar_t* caption, int id)
        {
            label.Create(caption, WS_CHILD | WS_VISIBLE | SS_LEFT,
                CRect(0, 0, 10, 10), this, id);
            label.SetFont(&m_labelFont);
        };
    auto makeEdit = [this](CEdit& edit, int id, DWORD extraStyle = 0)
        {
            edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
                ES_AUTOHSCROLL | extraStyle, CRect(0, 0, 10, 10), this, id);
            edit.SetFont(&m_uiFont);
            edit.SetMargins(12, 5);
        };

    makeLabel(m_usernameLabel, L"Username", ID_REG_USERNAME_LABEL);
    makeEdit(m_username, ID_REG_USERNAME);
    makeLabel(m_emailLabel, L"Email address", ID_REG_EMAIL_LABEL);
    makeEdit(m_email, ID_REG_EMAIL);
    makeLabel(m_passwordLabel, L"Password", ID_REG_PASSWORD_LABEL);
    makeEdit(m_password, ID_REG_PASSWORD, ES_PASSWORD);
    makeLabel(m_confirmLabel, L"Confirm password", ID_REG_CONFIRM_LABEL);
    makeEdit(m_confirmPassword, ID_REG_CONFIRM, ES_PASSWORD);

    m_showPassword.Create(L"Show password", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        CRect(0, 0, 10, 10), this, ID_REG_SHOW_PASSWORD);
    m_showPassword.SetFont(&m_uiFont);
    m_showConfirmPassword.Create(L"Show confirmation", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        CRect(0, 0, 10, 10), this, ID_REG_SHOW_CONFIRM);
    m_showConfirmPassword.SetFont(&m_uiFont);

    m_submit.Create(L"Create account", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        CRect(0, 0, 10, 10), this, ID_REG_SUBMIT);
    m_submit.SetFont(&m_labelFont);
    m_footer.Create(L"Already have an account? Close this window and sign in.",
        WS_CHILD | WS_VISIBLE | SS_CENTER, CRect(0, 0, 10, 10), this, ID_REG_FOOTER);
    m_footer.SetFont(&m_subtitleFont);

    LayoutRegistration();
    return TRUE;
}

void CRegisterPage::LayoutRegistration()
{
    if (!::IsWindow(m_title.GetSafeHwnd())) return;

    CRect client;
    GetClientRect(&client);
    const int w = client.Width();
    const int h = client.Height();

    // A responsive centered card; the card grows with the app window but stays readable.
    const int cardW = min(900, max(560, w - 64));
    const int cardH = min(820, max(620, h - 64));
    const int cardX = max(24, (w - cardW) / 2);
    const int cardY = max(24, (h - cardH) / 2);
    const int innerX = cardX + 70;
    const int innerW = cardW - 140;
    const int titleY = cardY + 48;
    const int labelH = 25;
    const int editH = 48;
    const int gap = 29;
    int y = titleY + 100;

    m_title.MoveWindow(innerX, titleY, innerW, 48);
    m_subtitle.MoveWindow(innerX, titleY + 54, innerW, 28);

    auto placeField = [&](CStatic& label, CEdit& edit)
        {
            label.MoveWindow(innerX, y, innerW, labelH);
            edit.MoveWindow(innerX, y + labelH, innerW, editH);
            y += labelH + editH + gap;
        };
    placeField(m_usernameLabel, m_username);
    placeField(m_emailLabel, m_email);
    placeField(m_passwordLabel, m_password);
    placeField(m_confirmLabel, m_confirmPassword);

    const int checkY = y - 8;
    m_showPassword.MoveWindow(innerX, checkY, 190, 30);
    m_showConfirmPassword.MoveWindow(innerX + 220, checkY, 240, 30);
    const int buttonY = min(cardY + cardH - 120, checkY + 54);
    m_submit.MoveWindow(innerX, buttonY, innerW, 52);
    m_footer.MoveWindow(innerX, buttonY + 62, innerW, 30);
}

void CRegisterPage::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

void CRegisterPage::OnPaint()
{
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);

    // Full-window blue gradient background.
    for (int y = 0; y < client.Height(); ++y)
    {
        const double t = static_cast<double>(y) / max(1, client.Height());
        const BYTE r = static_cast<BYTE>(24 + (70 - 24) * t);
        const BYTE g = static_cast<BYTE>(91 + (155 - 91) * t);
        const BYTE b = static_cast<BYTE>(220 + (245 - 220) * t);
        dc.FillSolidRect(0, y, client.Width(), 1, RGB(r, g, b));
    }

    const int cardW = min(900, max(560, client.Width() - 64));
    const int cardH = min(820, max(620, client.Height() - 64));
    const int x = max(24, (client.Width() - cardW) / 2);
    const int y = max(24, (client.Height() - cardH) / 2);
    CRect card(x, y, x + cardW, y + cardH);
    CBrush whiteBrush(COLOR_WHITE);
    CPen borderPen(PS_SOLID, 1, COLOR_BORDER);
    CPen* oldPen = dc.SelectObject(&borderPen);
    CBrush* oldBrush = dc.SelectObject(&whiteBrush);
    dc.RoundRect(card, CPoint(24, 24));
    if (oldBrush) dc.SelectObject(oldBrush);
    if (oldPen) dc.SelectObject(oldPen);
}

void CRegisterPage::OnSize(UINT nType, int cx, int cy)
{
    CDialogEx::OnSize(nType, cx, cy);
    LayoutRegistration();
    Invalidate(FALSE);
}

HBRUSH CRegisterPage::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    const int id = pWnd ? pWnd->GetDlgCtrlID() : 0;

    if (nCtlColor == CTLCOLOR_STATIC)
    {
        pDC->SetBkMode(TRANSPARENT);
        if (id == ID_REG_TITLE)
            pDC->SetTextColor(COLOR_BLUE_DARK);
        else if (id == ID_REG_SUBTITLE)
            pDC->SetTextColor(COLOR_MUTED);
        else if (id == ID_REG_FOOTER)
            pDC->SetTextColor(COLOR_MUTED);
        else
            pDC->SetTextColor(COLOR_TEXT);
        return (HBRUSH)m_editBrush.GetSafeHandle();
    }

    if (nCtlColor == CTLCOLOR_EDIT)
    {
        pDC->SetTextColor(COLOR_TEXT);
        pDC->SetBkColor(COLOR_WHITE);
        return (HBRUSH)m_editBrush.GetSafeHandle();
    }

    if (nCtlColor == CTLCOLOR_BTN)
    {
        pDC->SetTextColor(COLOR_TEXT);
        pDC->SetBkColor(COLOR_WHITE);
        return (HBRUSH)m_editBrush.GetSafeHandle();
    }

    return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CRegisterPage::OnTogglePassword()
{
    const bool show = m_showPassword.GetCheck() == BST_CHECKED;
    m_password.SetPasswordChar(show ? 0 : L'*');
    m_password.Invalidate();
    m_password.UpdateWindow();
    m_showPassword.SetWindowTextW(show ? L"Hide password" : L"Show password");
}

void CRegisterPage::OnToggleConfirmPassword()
{
    const bool show = m_showConfirmPassword.GetCheck() == BST_CHECKED;
    m_confirmPassword.SetPasswordChar(show ? 0 : L'*');
    m_confirmPassword.Invalidate();
    m_confirmPassword.UpdateWindow();
    m_showConfirmPassword.SetWindowTextW(show ? L"Hide confirmation" : L"Show confirmation");
}

void CRegisterPage::OnRegisterClicked()
{
    CString username, email, password, confirm;
    m_username.GetWindowText(username);
    m_email.GetWindowText(email);
    m_password.GetWindowText(password);
    m_confirmPassword.GetWindowText(confirm);
    username.Trim();
    email.Trim();

    if (username.IsEmpty() || email.IsEmpty() || password.IsEmpty() || confirm.IsEmpty())
    {
        AfxMessageBox(L"Please fill in every field.", MB_ICONWARNING);
        return;
    }
    if (username.GetLength() > 50)
    {
        AfxMessageBox(L"Username must be 50 characters or fewer.", MB_ICONWARNING);
        return;
    }
    if (email.GetLength() > 150)
    {
        AfxMessageBox(L"Email must be 150 characters or fewer.", MB_ICONWARNING);
        return;
    }
    const int at = email.Find(L'@');
    if (at <= 0 || email.Find(L'.', at + 1) < 0)
    {
        AfxMessageBox(L"Please enter a valid email address.", MB_ICONWARNING);
        return;
    }
    if (password.GetLength() < 8)
    {
        AfxMessageBox(L"Password must contain at least 8 characters.", MB_ICONWARNING);
        return;
    }
    if (password.GetLength() > 1024)
    {
        AfxMessageBox(L"Password is too long.", MB_ICONWARNING);
        return;
    }
    if (password != confirm)
    {
        AfxMessageBox(L"Passwords do not match.", MB_ICONWARNING);
        return;
    }

    DatabaseService& database = DatabaseService::Instance();
    if (!database.IsConnected())
    {
        AfxMessageBox(L"MySQL is not connected. Please start the database and try again.", MB_ICONERROR);
        return;
    }

    const std::string usernameUtf8 = ToUtf8(username);
    const std::string emailUtf8 = ToUtf8(email);
    const std::string passwordUtf8 = ToUtf8(password);
    // Keep the existing database API/schema; username is used as display name.
    const std::string displayNameUtf8 = usernameUtf8;

    if (database.RegisterUser(usernameUtf8, emailUtf8, passwordUtf8, displayNameUtf8))
    {
        AfxMessageBox(L"Account created successfully!\nYou can now log in.", MB_ICONINFORMATION);
        EndDialog(IDOK);
        return;
    }

    const std::string error = database.GetLastError();
    CString message;
    message.Format(L"Registration failed.\n\nDatabase error: %S", error.c_str());
    AfxMessageBox(message, MB_ICONERROR);
}
