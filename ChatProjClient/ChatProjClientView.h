#pragma once

#include <afxwin.h>
#include <afxrich.h>
#include <string>
#include <vector>

class CChatProjClientDoc;

class CChatProjClientView : public CView
{
protected:
    CChatProjClientView() noexcept;
    DECLARE_DYNCREATE(CChatProjClientView)

public:
    virtual ~CChatProjClientView();

    virtual void OnDraw(CDC* pDC) override;
    virtual void OnInitialUpdate() override;
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs) override;

    CChatProjClientDoc* GetDocument() const;

protected:
    CEdit m_input;
    CRichEditCtrl m_chatLog;
    CEdit m_search;                 // Recipient user ID / conversation lookup
    CListBox m_conversationList;    // Conversations discovered or opened by ID
    CStatic m_conversationTitle;
    CStatic m_chatTitle;
    CStatic m_chatSubtitle;

    CButton m_openChatButton;
    CButton m_sendButton;
    CButton m_connectButton;
    CButton m_contactsButton;
    CButton m_groupsButton;
    CButton m_historyButton;
    CButton m_profileButton;
    CButton m_settingsButton;

    enum class Page
    {
        Dashboard,
        Chat,
        Contacts,
        Groups,
        History,
        Profile,
        Settings
    };

    Page m_currentPage = Page::Dashboard;

    bool m_connected = false;
    bool m_controlsCreated = false;

    long long m_loggedInUserId = 0;
    CString m_displayName;

    std::vector<CString> m_messages;
    std::vector<CString> m_conversationIds;
    CString m_selectedRecipientId;

    afx_msg void OnSendMessage();
    afx_msg void OnConnect();
    afx_msg void OnContacts();
    afx_msg void OnGroups();
    afx_msg void OnHistory();
    afx_msg void OnProfile();
    afx_msg void OnSettings();
    afx_msg void OnOpenConversation();
    afx_msg void OnConversationSelectionChanged();

    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg LRESULT OnNetworkMessage(WPARAM wParam, LPARAM lParam);

    afx_msg void OnFilePrintPreview();
    afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);

    void ShowPage(Page page);
    void LayoutControls(int cx, int cy);
    void AppendChatText(const CString& text, bool remember = true);
    void AddConversation(const CString& userId, bool selectConversation = false);
    void UpdateChatHeader();

    DECLARE_MESSAGE_MAP()

    virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
    virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
    virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);

#ifdef _DEBUG
public:
    virtual void AssertValid() const override;
    virtual void Dump(CDumpContext& dc) const override;
#endif
};

#ifndef _DEBUG
inline CChatProjClientDoc* CChatProjClientView::GetDocument() const
{
    return reinterpret_cast<CChatProjClientDoc*>(m_pDocument);
}
#endif
