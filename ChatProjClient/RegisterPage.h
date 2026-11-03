#pragma once

#include <afxwin.h>
#include <vector>

class CRegisterPage : public CDialogEx
{
public:
    CRegisterPage(CWnd* pParent = nullptr);

protected:
    CEdit m_username;
    CEdit m_email;
    CEdit m_password;
    CEdit m_confirmPassword;

    CButton m_showPassword;
    CButton m_showConfirmPassword;
    CButton m_submit;

    CStatic m_title;
    CStatic m_subtitle;
    CStatic m_usernameLabel;
    CStatic m_emailLabel;
    CStatic m_passwordLabel;
    CStatic m_confirmLabel;
    CStatic m_footer;

    CFont m_titleFont;
    CFont m_subtitleFont;
    CFont m_uiFont;
    CFont m_labelFont;
    CBrush m_backgroundBrush;
    CBrush m_editBrush;

    std::vector<WORD> m_dialogTemplate;

    afx_msg void OnRegisterClicked();
    afx_msg void OnTogglePassword();
    afx_msg void OnToggleConfirmPassword();
    afx_msg void OnPaint();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    void LayoutRegistration();
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;

    DECLARE_MESSAGE_MAP()
};
