#pragma once

#include <afxwin.h>
#include <vector>

class CLoginPage : public CDialogEx
{
public:
    explicit CLoginPage(CWnd* pParent = nullptr);

    CString GetUsername() const;
    CString GetPassword() const;

protected:
    CEdit m_username;
    CEdit m_password;
    CButton m_showPassword;
    CButton m_loginButton;
    CButton m_registerButton;

    CStatic m_title;
    CStatic m_subtitle;
    CStatic m_usernameLabel;
    CStatic m_passwordLabel;
    CStatic m_registerPrompt;

    CFont m_titleFont;
    CFont m_subtitleFont;
    CFont m_uiFont;
    CFont m_labelFont;
    CBrush m_backgroundBrush;
    CBrush m_whiteBrush;

    std::vector<WORD> m_dialogTemplate;
    CString m_usernameValue;
    CString m_passwordValue;

    afx_msg void OnLoginClicked();
    afx_msg void OnRegisterClicked();
    afx_msg void OnTogglePassword();
    afx_msg void OnPaint();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

    void LayoutLogin(int cx, int cy);

    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;

    DECLARE_MESSAGE_MAP()
};
