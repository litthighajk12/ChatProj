// ChatProjClient.cpp : Defines the class behaviors for the application.

#include "pch.h"
#include "framework.h"
#include "afxwinappex.h"
#include "afxdialogex.h"

#include "ChatProjClient.h"
#include "MainFrm.h"
#include "ChildFrm.h"
#include "ChatProjClientDoc.h"
#include "ChatProjClientView.h"

#include "DatabaseService.h"
#include "DatabaseConfig.h"
#include "LoginPage.h"
#include "RegisterPage.h"

#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CChatProjClientApp, CWinAppEx)
    ON_COMMAND(ID_APP_ABOUT, &CChatProjClientApp::OnAppAbout)
    ON_COMMAND(ID_FILE_NEW, &CWinAppEx::OnFileNew)
    ON_COMMAND(ID_FILE_OPEN, &CWinAppEx::OnFileOpen)
    ON_COMMAND(ID_FILE_PRINT_SETUP, &CWinAppEx::OnFilePrintSetup)
END_MESSAGE_MAP()

CChatProjClientApp::CChatProjClientApp() noexcept
{
    m_bHiColorIcons = TRUE;
    m_nAppLook = 0;

    m_dwRestartManagerSupportFlags =
        AFX_RESTART_MANAGER_SUPPORT_ALL_ASPECTS;

#ifdef _MANAGED
    System::Windows::Forms::Application::SetUnhandledExceptionMode(
        System::Windows::Forms::UnhandledExceptionMode::ThrowException);
#endif

    SetAppID(_T("ChatProjClient.AppID.NoVersion"));
}

CChatProjClientApp theApp;

BOOL CChatProjClientApp::InitInstance()
{
    INITCOMMONCONTROLSEX InitCtrls{};
    InitCtrls.dwSize = sizeof(InitCtrls);
    InitCtrls.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&InitCtrls);

    CWinAppEx::InitInstance();
    AfxEnableControlContainer();

    // Connect to MySQL.
    char* dbPassword = nullptr;
    size_t passwordLength = 0;

    errno_t envResult = _dupenv_s(
        &dbPassword,
        &passwordLength,
        "CHATPROJ_DB_PASSWORD");

    if (envResult != 0 || dbPassword == nullptr)
    {
        if (dbPassword != nullptr)
            free(dbPassword);

        AfxMessageBox(
            L"CHATPROJ_DB_PASSWORD is missing.\n"
            L"Set the environment variable and restart Visual Studio.",
            MB_ICONERROR);

        return FALSE;
    }

    bool connected = DatabaseService::Instance().Connect(
        DatabaseConfig::HOST,
        DatabaseConfig::USERNAME,
        dbPassword,
        DatabaseConfig::DATABASE,
        DatabaseConfig::PORT);

    SecureZeroMemory(dbPassword, strlen(dbPassword));
    free(dbPassword);

    if (!connected)
    {
        std::string error =
            DatabaseService::Instance().GetLastError();

        CString message;
        message.Format(
            L"Cannot connect to MySQL.\n\n%S",
            error.c_str());

        AfxMessageBox(message, MB_ICONERROR);
        return FALSE;
    }

    EnableTaskbarInteraction();

    SetRegistryKey(_T("ChatProjClient"));
    LoadStdProfileSettings(4);

    InitContextMenuManager();
    InitKeyboardManager();
    InitTooltipManager();

    CMFCToolTipInfo ttParams;
    ttParams.m_bVislManagerTheme = TRUE;

    GetTooltipManager()->SetTooltipParams(
        AFX_TOOLTIP_TYPE_ALL,
        RUNTIME_CLASS(CMFCToolTipCtrl),
        &ttParams);

    // Register the document template.
    CMultiDocTemplate* pDocTemplate = new CMultiDocTemplate(
        IDR_ChatProjClientTYPE,
        RUNTIME_CLASS(CChatProjClientDoc),
        RUNTIME_CLASS(CChildFrame),
        RUNTIME_CLASS(CChatProjClientView));

    if (pDocTemplate == nullptr)
        return FALSE;

    AddDocTemplate(pDocTemplate);

    // Create the main MDI frame.
    CMainFrame* pMainFrame = new CMainFrame;

    if (pMainFrame == nullptr ||
        !pMainFrame->LoadFrame(IDR_MAINFRAME))
    {
        delete pMainFrame;
        return FALSE;
    }

    m_pMainWnd = pMainFrame;

    // Initialize MFC command-line processing.
    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);

    if (!ProcessShellCommand(cmdInfo))
        return FALSE;

    pMainFrame->ShowWindow(SW_HIDE);

    // Authentication loop: register or log in before opening the app.
    bool authenticated = false;

    while (!authenticated)
    {
        CLoginPage loginDlg;

        INT_PTR result = loginDlg.DoModal();

        if (result != IDOK)
        {
            pMainFrame->DestroyWindow();
            return FALSE;
        }

        authenticated = true;
    }

    // Open the main application after login.
    if (!pMainFrame->IsWindowVisible())
        pMainFrame->ShowWindow(m_nCmdShow);

    pMainFrame->UpdateWindow();

    return TRUE;
}

int CChatProjClientApp::ExitInstance()
{
    DatabaseService::Instance().Disconnect();

    AfxOleTerm(FALSE);

    return CWinAppEx::ExitInstance();
}

class CAboutDlg : public CDialogEx
{
public:
    CAboutDlg() noexcept;

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_ABOUTBOX };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);

    DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() noexcept
    : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

void CChatProjClientApp::OnAppAbout()
{
    CAboutDlg aboutDlg;
    aboutDlg.DoModal();
}

void CChatProjClientApp::PreLoadState()
{
    BOOL bNameValid;
    CString strName;

    bNameValid = strName.LoadString(IDS_EDIT_MENU);
    ASSERT(bNameValid);
    GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EDIT);

    bNameValid = strName.LoadString(IDS_EXPLORER);
    ASSERT(bNameValid);
    GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EXPLORER);
}

void CChatProjClientApp::LoadCustomState()
{
}

void CChatProjClientApp::SaveCustomState()
{
}