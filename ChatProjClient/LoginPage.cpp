#include "pch.h"
#include "framework.h"
#include "LoginPage.h"
#include "RegisterPage.h"
#include "DatabaseService.h"
#include "ChatClientCore.h"
#include "DialogTemplateHelper.h"

#include <string>
#include <vector>
#include <atlconv.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    constexpr int ID_USERNAME = 2001;
    constexpr int ID_PASSWORD = 2002;
    constexpr int ID_LOGIN = 2003;
    constexpr int ID_REGISTER = 2004;
    constexpr int ID_SHOW_PASSWORD = 2005;

    const COLORREF COLOR_PAGE = RGB(224, 239, 255);
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

    void DrawVerticalGradient(CDC& dc, const CRect& rect,
        COLORREF topColor, COLORREF bottomColor)
    {
        const int height = rect.Height();
        if (height <= 0)
            return;

        const int topR = GetRValue(topColor);
        const int topG = GetGValue(topColor);
        const int topB = GetBValue(topColor);
        const int botR = GetRValue(bottomColor);
        const int botG = GetGValue(bottomColor);
        const int botB = GetBValue(bottomColor);

        for (int y = 0; y < height; ++y)
        {
            const double t = static_cast<double>(y) / (height > 1 ? height - 1 : 1);
            const COLORREF color = RGB(
                static_cast<BYTE>(topR + (botR - topR) * t),
                static_cast<BYTE>(topG + (botG - topG) * t),
                static_cast<BYTE>(topB + (botB - topB) * t));

            CPen pen(PS_SOLID, 1, color);
            CPen* oldPen = dc.SelectObject(&pen);
            dc.MoveTo(rect.left, rect.top + y);
            dc.LineTo(rect.right, rect.top + y);
            dc.SelectObject(oldPen);
        }
    }
}

BEGIN_MESSAGE_MAP(CLoginPage, CDialogEx)
    ON_BN_CLICKED(ID_LOGIN, &CLoginPage::OnLoginClicked)
    ON_BN_CLICKED(ID_REGISTER, &CLoginPage::OnRegisterClicked)
    ON_BN_CLICKED(ID_SHOW_PASSWORD, &CLoginPage::OnTogglePassword)
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

CLoginPage::CLoginPage(CWnd* pParent)
    : CDialogEx(),
    m_dialogTemplate(BuildDialogTemplate()),
    m_backgroundBrush(COLOR_PAGE),
    m_whiteBrush(COLOR_WHITE)
{
    InitModalIndirect(
        reinterpret_cast<DLGTEMPLATE*>(m_dialogTemplate.data()),
        pParent);
}

BOOL CLoginPage::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetWindowTextW(L"ChatProj | Sign in");

    // Use a normal, maximized application window instead of a small popup.
    ModifyStyle(DS_MODALFRAME, WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
        SWP_FRAMECHANGED);
    ShowWindow(SW_MAXIMIZE);

    m_titleFont.CreatePointFont(300, L"Segoe UI Semibold");
    m_subtitleFont.CreatePointFont(115, L"Segoe UI");
    m_uiFont.CreatePointFont(125, L"Segoe UI");
    m_labelFont.CreatePointFont(115, L"Segoe UI Semibold");

    m_title.Create(L"Sign in", WS_CHILD | WS_VISIBLE | SS_LEFT,
        CRect(0, 0, 10, 10), this);
    m_title.SetFont(&m_titleFont);

    m_subtitle.Create(L"Welcome back to ChatProj",
        WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this);
    m_subtitle.SetFont(&m_subtitleFont);

    m_usernameLabel.Create(L"Username",
        WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this);
    m_usernameLabel.SetFont(&m_labelFont);

    m_passwordLabel.Create(L"Password",
        WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this);
    m_passwordLabel.SetFont(&m_labelFont);

    m_username.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
        ES_AUTOHSCROLL, CRect(0, 0, 10, 10), this, ID_USERNAME);
    m_username.SetFont(&m_uiFont);
    m_username.SetMargins(14, 6);

    m_password.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
        ES_PASSWORD | ES_AUTOHSCROLL, CRect(0, 0, 10, 10), this, ID_PASSWORD);
    m_password.SetFont(&m_uiFont);
    m_password.SetMargins(14, 6);

    m_showPassword.Create(L"Show password",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        CRect(0, 0, 10, 10), this, ID_SHOW_PASSWORD);
    m_showPassword.SetFont(&m_uiFont);

    m_loginButton.Create(L"Sign in",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        CRect(0, 0, 10, 10), this, ID_LOGIN);
    m_loginButton.SetFont(&m_uiFont);

    m_registerPrompt.Create(L"New to ChatProj? Create an account below.",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        CRect(0, 0, 10, 10), this);
    m_registerPrompt.SetFont(&m_subtitleFont);

    m_registerButton.Create(L"Create account",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(0, 0, 10, 10), this, ID_REGISTER);
    m_registerButton.SetFont(&m_uiFont);

    CRect client;
    GetClientRect(&client);
    LayoutLogin(client.Width(), client.Height());

    m_username.SetFocus();
    return FALSE; // Focus was set manually.
}

void CLoginPage::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

void CLoginPage::LayoutLogin(int cx, int cy)
{
    if (!::IsWindow(m_username.GetSafeHwnd()))
        return;

    // Keep a centered card-like form. All controls resize with the window.
    const int cardWidth = min(760, max(420, cx - 48));
    const int cardLeft = (cx - cardWidth) / 2;
    const int innerLeft = cardLeft + 62;
    const int innerRight = cardLeft + cardWidth - 62;
    const int innerWidth = innerRight - innerLeft;

    const int contentHeight = 500;
    const int top = max(44, (cy - contentHeight) / 2);

    m_title.MoveWindow(innerLeft, top, innerWidth, 62);
    m_subtitle.MoveWindow(innerLeft + 4, top + 62, innerWidth, 32);

    const int labelHeight = 26;
    const int editHeight = 52;
    const int row1 = top + 132;

    m_usernameLabel.MoveWindow(innerLeft + 4, row1, innerWidth - 8, labelHeight);
    m_username.MoveWindow(innerLeft, row1 + 30, innerWidth, editHeight);

    const int row2 = row1 + 112;
    m_passwordLabel.MoveWindow(innerLeft + 4, row2, innerWidth - 8, labelHeight);
    m_password.MoveWindow(innerLeft, row2 + 30, innerWidth, editHeight);

    m_showPassword.MoveWindow(innerLeft + 2, row2 + 88, 190, 30);
    m_loginButton.MoveWindow(innerLeft, row2 + 138, innerWidth, 58);
    m_registerPrompt.MoveWindow(innerLeft, row2 + 218, innerWidth, 32);
    m_registerButton.MoveWindow(innerLeft + innerWidth / 4,
        row2 + 258, innerWidth / 2, 48);
}

void CLoginPage::OnSize(UINT nType, int cx, int cy)
{
    CDialogEx::OnSize(nType, cx, cy);
    LayoutLogin(cx, cy);
    Invalidate(FALSE);
}

void CLoginPage::OnPaint()
{
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);

    DrawVerticalGradient(dc, client, RGB(81, 164, 239), RGB(25, 78, 210));

    // A subtle white rounded card behind the login controls.
    const int cardWidth = min(860, max(500, client.Width() - 36));
    const int cardHeight = 650;
    const int left = (client.Width() - cardWidth) / 2;
    const int top = max(20, (client.Height() - cardHeight) / 2);
    CRect card(left, top, left + cardWidth, min(client.Height() - 20, top + cardHeight));

    CBrush white(COLOR_WHITE);
    CPen border(PS_SOLID, 1, COLOR_BORDER);
    CBrush* oldBrush = dc.SelectObject(&white);
    CPen* oldPen = dc.SelectObject(&border);
    dc.RoundRect(card, CPoint(28, 28));
    dc.SelectObject(oldPen);
    dc.SelectObject(oldBrush);
}

HBRUSH CLoginPage::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    const int id = pWnd ? pWnd->GetDlgCtrlID() : 0;

    if (nCtlColor == CTLCOLOR_STATIC)
    {
        pDC->SetBkMode(TRANSPARENT);
        if (id == m_title.GetDlgCtrlID())
            pDC->SetTextColor(COLOR_BLUE_DARK);
        else if (id == m_subtitle.GetDlgCtrlID() ||
            id == m_registerPrompt.GetDlgCtrlID())
            pDC->SetTextColor(COLOR_MUTED);
        else
            pDC->SetTextColor(COLOR_TEXT);

        return (HBRUSH)m_whiteBrush.GetSafeHandle();
    }

    if (nCtlColor == CTLCOLOR_EDIT)
    {
        pDC->SetTextColor(COLOR_TEXT);
        pDC->SetBkColor(COLOR_WHITE);
        return (HBRUSH)m_whiteBrush.GetSafeHandle();
    }

    if (nCtlColor == CTLCOLOR_BTN)
    {
        pDC->SetTextColor(COLOR_TEXT);
        pDC->SetBkColor(COLOR_WHITE);
        return (HBRUSH)m_whiteBrush.GetSafeHandle();
    }

    return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CLoginPage::OnTogglePassword()
{
    const bool show = m_showPassword.GetCheck() == BST_CHECKED;
    m_password.SetPasswordChar(show ? 0 : L'*');
    m_password.Invalidate();
    m_password.UpdateWindow();
    m_showPassword.SetWindowTextW(show ? L"Hide password" : L"Show password");
}

void CLoginPage::OnLoginClicked()
{
    m_username.GetWindowText(m_usernameValue);
    m_password.GetWindowText(m_passwordValue);

    m_usernameValue.Trim();

    if (m_usernameValue.IsEmpty() || m_passwordValue.IsEmpty())
    {
        AfxMessageBox(L"Enter your username and password.", MB_ICONWARNING);
        return;
    }

    DatabaseService& database = DatabaseService::Instance();

    if (!database.IsConnected())
    {
        AfxMessageBox(
            L"MySQL is not connected. Check the database configuration.",
            MB_ICONERROR);
        return;
    }

    const std::string username = ToUtf8(m_usernameValue);
    const std::string password = ToUtf8(m_passwordValue);

    long long userId = 0;
    std::string displayName;

    // Preserve the existing database authentication flow.
    if (database.AuthenticateForChat(username, password, userId, displayName))
    {
        // Preserve the existing server authentication credentials flow.
        ChatClientCore::SetLoginCredentials(username, password);
        EndDialog(IDOK);
        return;
    }

    const std::string error = database.GetLastError();
    CString message;
    message.Format(L"Login failed.\n\n%S", error.c_str());
    AfxMessageBox(message, MB_ICONWARNING);
}

void CLoginPage::OnRegisterClicked()
{
    CRegisterPage registerDlg(this);

    if (registerDlg.DoModal() == IDOK)
    {
        AfxMessageBox(
            L"Registration completed. You can now sign in.",
            MB_ICONINFORMATION);
    }
}

CString CLoginPage::GetUsername() const
{
    return m_usernameValue;
}

CString CLoginPage::GetPassword() const
{
    return m_passwordValue;
}
