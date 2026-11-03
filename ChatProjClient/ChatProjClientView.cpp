#include "pch.h"
#include "framework.h"
#include "ChatProjClient.h"
#include "ChatProjClientDoc.h"
#include "ChatProjClientView.h"
#include "ChatClientCore.h"
#include "DatabaseService.h"
#include <afxrich.h>

#include <memory>
#include <string>

IMPLEMENT_DYNCREATE(CChatProjClientView, CView)

BEGIN_MESSAGE_MAP(CChatProjClientView, CView)
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
    ON_BN_CLICKED(1001, &CChatProjClientView::OnSendMessage)
    ON_BN_CLICKED(1002, &CChatProjClientView::OnConnect)
    ON_BN_CLICKED(1006, &CChatProjClientView::OnContacts)
    ON_BN_CLICKED(1007, &CChatProjClientView::OnGroups)
    ON_BN_CLICKED(1008, &CChatProjClientView::OnHistory)
    ON_BN_CLICKED(1009, &CChatProjClientView::OnProfile)
    ON_BN_CLICKED(1010, &CChatProjClientView::OnSettings)
    ON_BN_CLICKED(1011, &CChatProjClientView::OnOpenConversation)
    ON_LBN_SELCHANGE(1012, &CChatProjClientView::OnConversationSelectionChanged)
    ON_MESSAGE(WM_APP + 1, &CChatProjClientView::OnNetworkMessage)
END_MESSAGE_MAP()

namespace
{
    // ChatProj light-blue / white / blue design palette.
    const COLORREF BG = RGB(238, 246, 255);
    const COLORREF PANEL = RGB(255, 255, 255);
    const COLORREF BLUE = RGB(37, 99, 235);
    const COLORREF DARK_BLUE = RGB(30, 64, 175);
    const COLORREF WHITE = RGB(255, 255, 255);
    const COLORREF TEXT = RGB(30, 41, 59);
    const COLORREF MUTED = RGB(100, 116, 139);
    const COLORREF BORDER = RGB(219, 231, 247);
    const COLORREF PALE_BLUE = RGB(224, 239, 255);

    CString Utf8ToCString(const std::string& value)
    {
        if (value.empty())
            return CString();

        int n = MultiByteToWideChar(
            CP_UTF8, 0, value.data(),
            static_cast<int>(value.size()), nullptr, 0);

        if (n <= 0)
            return CString();

        CString result;
        wchar_t* buffer = result.GetBuffer(n);

        MultiByteToWideChar(
            CP_UTF8, 0, value.data(),
            static_cast<int>(value.size()), buffer, n);

        result.ReleaseBuffer(n);
        return result;
    }

    std::string CStringToUtf8(const CString& value)
    {
        if (value.IsEmpty())
            return {};

        int n = WideCharToMultiByte(
            CP_UTF8, 0, value.GetString(), value.GetLength(),
            nullptr, 0, nullptr, nullptr);

        if (n <= 0)
            return {};

        std::string result(static_cast<size_t>(n), '\0');

        WideCharToMultiByte(
            CP_UTF8, 0, value.GetString(), value.GetLength(),
            &result[0], n, nullptr, nullptr);

        return result;
    }
}

CChatProjClientView::CChatProjClientView() noexcept
{
}

CChatProjClientView::~CChatProjClientView()
{
    ChatClientCore::SetMessageCallback(nullptr);
    ChatClientCore::Disconnect();
}

BOOL CChatProjClientView::PreCreateWindow(CREATESTRUCT& cs)
{
    return CView::PreCreateWindow(cs);
}

BOOL CChatProjClientView::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

void CChatProjClientView::OnInitialUpdate()
{
    CView::OnInitialUpdate();

    CRect r;
    GetClientRect(&r);

    AfxInitRichEdit2();

    m_conversationTitle.Create(
        L"CONVERSATIONS", WS_CHILD | WS_VISIBLE,
        CRect(225, 105, 475, 130), this);

    m_search.Create(
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        CRect(225, 138, 395, 170), this, 1005);

    m_openChatButton.Create(
        L"Open", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(402, 138, 470, 170), this, 1011);

    m_conversationList.Create(
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        CRect(225, 181, 475, 600), this, 1012);

    m_chatTitle.Create(
        L"Choose a conversation", WS_CHILD | WS_VISIBLE,
        CRect(500, 103, 1000, 132), this);

    m_chatSubtitle.Create(
        L"Enter another user's database ID to start a private chat",
        WS_CHILD | WS_VISIBLE,
        CRect(500, 132, 1100, 154), this);

    m_chatLog.Create(
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL,
        CRect(500, 166, max(620, r.Width() - 28), max(250, r.Height() - 110)),
        this, 1003);
    m_chatLog.SetBackgroundColor(FALSE, RGB(255, 255, 255));

    m_input.Create(
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        CRect(500, max(180, r.Height() - 70), max(620, r.Width() - 130),
            max(220, r.Height() - 28)),
        this, 1004);

    m_sendButton.Create(
        L"Send", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(max(620, r.Width() - 118), max(180, r.Height() - 70),
            max(700, r.Width() - 25), max(220, r.Height() - 28)),
        this, 1001);

    m_connectButton.Create(
        L"Connect Server", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(18, 440, 205, 480), this, 1002);

    m_contactsButton.Create(
        L"Private Messages", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(12, 218, 238, 256), this, 1006);

    m_groupsButton.Create(
        L"Groups", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(12, 263, 238, 301), this, 1007);

    m_historyButton.Create(
        L"Dashboard", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(12, 308, 238, 346), this, 1008);

    m_profileButton.Create(
        L"Profile", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(12, 353, 238, 391), this, 1009);

    m_settingsButton.Create(
        L"Settings", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(12, 398, 238, 436), this, 1010);

    CFont* stockFont = CFont::FromHandle(
        static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));

    m_chatLog.SetFont(stockFont);
    m_input.SetFont(stockFont);
    m_search.SetFont(stockFont);
    m_conversationList.SetFont(stockFont);
    m_conversationTitle.SetFont(stockFont);
    m_chatTitle.SetFont(stockFont);
    m_chatSubtitle.SetFont(stockFont);
    m_sendButton.SetFont(stockFont);
    m_connectButton.SetFont(stockFont);
    m_openChatButton.SetFont(stockFont);
    m_contactsButton.SetFont(stockFont);
    m_groupsButton.SetFont(stockFont);
    m_historyButton.SetFont(stockFont);
    m_profileButton.SetFont(stockFont);
    m_settingsButton.SetFont(stockFont);

    LayoutControls(r.Width(), r.Height());
    ShowPage(Page::Dashboard);
}

void CChatProjClientView::OnDraw(CDC* pDC)
{
    CRect r;
    GetClientRect(&r);

    pDC->FillSolidRect(r, BG);
    pDC->FillSolidRect(0, 0, 220, r.Height(), WHITE);
    pDC->FillSolidRect(220, 0, r.Width() - 220, 78, WHITE);

    CPen borderPen(PS_SOLID, 1, BORDER);
    CPen* oldPen = pDC->SelectObject(&borderPen);
    CBrush* oldBrush = (CBrush*)pDC->SelectStockObject(NULL_BRUSH);
    pDC->MoveTo(219, 0);
    pDC->LineTo(219, r.Height());
    pDC->MoveTo(220, 77);
    pDC->LineTo(r.Width(), 77);

    CFont logoFont, headingFont, bodyFont, smallFont, sectionFont;
    logoFont.CreatePointFont(230, L"Segoe UI Semibold");
    headingFont.CreatePointFont(160, L"Segoe UI Semibold");
    bodyFont.CreatePointFont(105, L"Segoe UI");
    smallFont.CreatePointFont(90, L"Segoe UI");
    sectionFont.CreatePointFont(95, L"Segoe UI Semibold");

    pDC->SetBkMode(TRANSPARENT);
    CFont* oldFont = pDC->SelectObject(&logoFont);
    pDC->SetTextColor(BLUE);
    pDC->TextOutW(22, 20, L"ChatProj");

    pDC->SelectObject(&smallFont);
    pDC->SetTextColor(MUTED);
    pDC->TextOutW(24, 51, L"YOUR SPACE TO CONNECT");
    pDC->TextOutW(24, 105, L"WORKSPACE");

    pDC->SelectObject(&bodyFont);
    pDC->SetTextColor(TEXT);
    pDC->TextOutW(24, 135, L"Navigate");

    CRect activeRect(12, 308, 208, 346);
    if (m_currentPage == Page::Chat || m_currentPage == Page::Contacts)
        activeRect.SetRect(12, 218, 208, 256);
    else if (m_currentPage == Page::Groups)
        activeRect.SetRect(12, 263, 208, 301);
    else if (m_currentPage == Page::Profile)
        activeRect.SetRect(12, 353, 208, 391);

    CBrush activeBrush(PALE_BLUE);
    pDC->FillRect(&activeRect, &activeBrush);
    pDC->FillSolidRect(activeRect.left, activeRect.top, 4, activeRect.Height(), BLUE);

    pDC->SelectObject(&headingFont);
    pDC->SetTextColor(DARK_BLUE);
    switch (m_currentPage)
    {
    case Page::Dashboard: pDC->TextOutW(245, 15, L"Dashboard"); break;
    case Page::Chat:
    case Page::Contacts: pDC->TextOutW(245, 15, L"Private Messages"); break;
    case Page::Groups: pDC->TextOutW(245, 15, L"Groups"); break;
    case Page::History: pDC->TextOutW(245, 15, L"Chat History"); break;
    case Page::Profile: pDC->TextOutW(245, 15, L"Profile"); break;
    case Page::Settings: pDC->TextOutW(245, 15, L"Settings"); break;
    }

    pDC->SelectObject(&smallFont);
    pDC->SetTextColor(MUTED);
    if (m_currentPage == Page::Dashboard)
        pDC->TextOutW(246, 47, L"Welcome to your workspace");
    else if (m_currentPage == Page::Chat || m_currentPage == Page::Contacts)
        pDC->TextOutW(246, 47, L"Private conversations");
    else if (m_currentPage == Page::Groups)
        pDC->TextOutW(246, 47, L"Broadcast to connected, authenticated clients");
    else if (m_currentPage == Page::Profile)
        pDC->TextOutW(246, 47, L"Your account information");
    else
        pDC->TextOutW(246, 47, L"ChatProj workspace");

    if (m_currentPage == Page::Chat || m_currentPage == Page::Contacts)
    {
        CRect listPanel(220, 78, 485, r.Height());
        pDC->FillSolidRect(listPanel, RGB(247, 250, 255));
        pDC->MoveTo(484, 78);
        pDC->LineTo(484, r.Height());
    }

    CRect contentCard(
        (m_currentPage == Page::Chat || m_currentPage == Page::Contacts) ? 500 : 245,
        92, max(260, r.Width() - 18), max(180, r.Height() - 18));
    CBrush cardBrush(WHITE);
    pDC->SelectObject(&borderPen);
    pDC->SelectObject(&cardBrush);
    pDC->RoundRect(contentCard, CPoint(12, 12));

    pDC->SelectObject(&smallFont);
    pDC->SetTextColor(MUTED);
    pDC->TextOutW(24, max(420, r.Height() - 116), L"SERVER CONNECTION");

    if (oldFont)
        pDC->SelectObject(oldFont);
    pDC->SelectObject(oldPen);
    pDC->SelectObject(oldBrush);
}

void CChatProjClientView::LayoutControls(int cx, int cy)
{
    if (!::IsWindow(m_chatLog.GetSafeHwnd()))
        return;

    const bool privatePage = (m_currentPage == Page::Chat || m_currentPage == Page::Contacts);
    const int left = privatePage ? 500 : 245;
    const int rightMargin = 28;
    const int inputHeight = 42;
    const int sendWidth = 92;
    const int sendX = max(left + 110, cx - rightMargin - sendWidth);
    const int inputWidth = max(100, sendX - left - 14);
    const int inputY = max(170, cy - 24 - inputHeight);
    const int logTop = privatePage ? 166 : 118;
    const int logHeight = max(100, inputY - logTop - 14);

    m_chatLog.MoveWindow(left + 12, logTop, max(100, cx - left - rightMargin - 12), logHeight);
    m_input.MoveWindow(left + 12, inputY, max(100, inputWidth - 8), inputHeight);
    m_sendButton.MoveWindow(sendX, inputY, sendWidth, inputHeight);

    // The conversation lookup panel is shown only for private messages.
    m_conversationTitle.MoveWindow(238, 100, 230, 25);
    m_search.MoveWindow(238, 132, 155, 32);
    m_openChatButton.MoveWindow(400, 132, 68, 32);
    m_conversationList.MoveWindow(238, 176, 230, max(100, cy - 205));
    m_chatTitle.MoveWindow(left + 18, 99, max(left + 200, cx - 30), 30);
    m_chatSubtitle.MoveWindow(left + 18, 128, max(left + 240, cx - 30), 24);

    m_historyButton.MoveWindow(12, 308, 196, 38);
    m_contactsButton.MoveWindow(12, 218, 196, 38);
    m_groupsButton.MoveWindow(12, 263, 196, 38);
    m_profileButton.MoveWindow(12, 353, 196, 38);
    m_settingsButton.ShowWindow(SW_HIDE);
    m_connectButton.MoveWindow(18, max(475, cy - 66), 184, 40);
}

void CChatProjClientView::ShowPage(Page page)
{
    m_currentPage = page;
    const bool dashboardPage = (page == Page::Dashboard);
    const bool chatPage = (page == Page::Chat || page == Page::Contacts);
    const bool broadcastPage = (page == Page::Groups);
    const bool composerPage = chatPage || broadcastPage;

    m_chatLog.ShowWindow(SW_SHOW);
    m_input.ShowWindow(composerPage ? SW_SHOW : SW_HIDE);
    m_sendButton.ShowWindow(composerPage ? SW_SHOW : SW_HIDE);
    m_connectButton.ShowWindow(SW_SHOW);
    m_search.ShowWindow(chatPage ? SW_SHOW : SW_HIDE);
    m_openChatButton.ShowWindow(chatPage ? SW_SHOW : SW_HIDE);
    m_conversationList.ShowWindow(chatPage ? SW_SHOW : SW_HIDE);
    m_conversationTitle.ShowWindow(chatPage ? SW_SHOW : SW_HIDE);
    m_chatTitle.ShowWindow(chatPage ? SW_SHOW : SW_HIDE);
    m_chatSubtitle.ShowWindow(chatPage ? SW_SHOW : SW_HIDE);

    m_historyButton.SetWindowTextW(L"Dashboard");
    m_contactsButton.SetWindowTextW(L"Private Messages");

    if (dashboardPage)
    {
        CString welcome;
        welcome.Format(
            L"Welcome, %s!\r\n\r\nYour User ID is: %lld\r\n\r\n"
            L"Use Private Messages to start a one-to-one conversation, or Groups to broadcast.\r\n\r\n"
            L"Connect to the server to start live messaging.",
            m_displayName.IsEmpty() ? L"User" : m_displayName.GetString(),
            m_loggedInUserId);
        m_chatLog.SetWindowTextW(welcome);
    }
    else if (chatPage)
    {
        m_chatLog.SetWindowTextW(
            L"Your private conversation will appear here.\r\n\r\n"
            L"Choose a user ID from the conversation list or enter an ID above and select Open.");
        UpdateChatHeader();
    }
    else
    {
        CString pageText;
        switch (page)
        {
        case Page::Groups:
            pageText = L"GROUP BROADCAST\r\n\r\nMessages sent here are broadcast to connected, authenticated clients.\r\n\r\nYour existing server remains responsible for message delivery and persistence.";
            break;
        case Page::History:
            pageText = L"CHAT HISTORY\r\n\r\nMessages received in this client session are shown here.";
            break;
        case Page::Profile:
            pageText.Format(
                L"PROFILE\r\n\r\nUsername: %s\r\nUser ID: %lld\r\n\r\nDatabase connection: %s",
                m_displayName.IsEmpty() ? L"Not received yet" : m_displayName.GetString(),
                m_loggedInUserId,
                DatabaseService::Instance().IsConnected() ? L"Connected" : L"Disconnected");
            break;
        case Page::Settings:
            pageText = L"SETTINGS\r\n\r\nServer address: 127.0.0.1\r\nTCP port: 5000\r\nPresence port: 5001";
            break;
        default:
            pageText = L"Welcome to ChatProj.";
            break;
        }
        m_chatLog.SetWindowTextW(pageText);
    }

    if (page == Page::Chat || page == Page::Groups || page == Page::History)
    {
        CString heading = page == Page::Chat
            ? L"PRIVATE MESSAGES\r\n\r\n"
            : (page == Page::Groups ? L"GROUP BROADCAST\r\n\r\n" : L"SESSION MESSAGE HISTORY\r\n\r\n");
        m_chatLog.SetWindowTextW(heading);
        for (const CString& savedMessage : m_messages)
            AppendChatText(savedMessage, false);
    }

    LayoutControls([&]() { CRect r; GetClientRect(&r); return r.Width(); }(),
        [&]() { CRect r; GetClientRect(&r); return r.Height(); }());
    Invalidate(FALSE);
}

void CChatProjClientView::AddConversation(const CString& userId, bool selectConversation)
{
    CString id(userId);
    id.Trim();
    if (id.IsEmpty() || id == L"0")
        return;

    for (const CString& existing : m_conversationIds)
    {
        if (existing == id)
        {
            if (selectConversation)
            {
                m_selectedRecipientId = id;
                m_search.SetWindowTextW(id);
                CString label;
                label.Format(L"User ID %s", id.GetString());
                const int index = m_conversationList.FindStringExact(-1, label);
                if (index != LB_ERR)
                    m_conversationList.SetCurSel(index);
                UpdateChatHeader();
            }
            return;
        }
    }

    m_conversationIds.push_back(id);
    CString label;
    label.Format(L"User ID %s", id.GetString());
    m_conversationList.AddString(label);

    if (selectConversation)
    {
        m_selectedRecipientId = id;
        m_search.SetWindowTextW(id);
        m_conversationList.SetCurSel(m_conversationList.GetCount() - 1);
        UpdateChatHeader();
    }
}

void CChatProjClientView::UpdateChatHeader()
{
    if (!::IsWindow(m_chatTitle.GetSafeHwnd()))
        return;

    if (m_selectedRecipientId.IsEmpty())
    {
        m_chatTitle.SetWindowTextW(L"Choose a conversation");
        m_chatSubtitle.SetWindowTextW(L"Enter another user's database ID to start a private chat");
    }
    else
    {
        CString title;
        title.Format(L"Private conversation · User ID %s", m_selectedRecipientId.GetString());
        m_chatTitle.SetWindowTextW(title);
        m_chatSubtitle.SetWindowTextW(m_connected
            ? L"Connected · messages use your existing TCP server"
            : L"Not connected · use Connect Server to begin messaging");
    }
}

void CChatProjClientView::OnContacts()
{
    ShowPage(Page::Chat);
}

void CChatProjClientView::OnGroups()
{
    ShowPage(Page::Groups);
}

void CChatProjClientView::OnHistory()
{
    // The former History navigation slot now returns to the Dashboard.
    ShowPage(Page::Dashboard);
}

void CChatProjClientView::OnProfile()
{
    ShowPage(Page::Profile);
}

void CChatProjClientView::OnSettings()
{
    ShowPage(Page::Settings);
}

void CChatProjClientView::OnOpenConversation()
{
    CString id;
    m_search.GetWindowText(id);
    id.Trim();
    if (id.IsEmpty())
    {
        AfxMessageBox(L"Enter the recipient's user ID first.");
        return;
    }
    for (int i = 0; i < id.GetLength(); ++i)
    {
        if (id[i] < L'0' || id[i] > L'9')
        {
            AfxMessageBox(L"For now, enter the numeric user ID. Username search needs a user-directory response from the server.");
            return;
        }
    }
    const long long recipientId = _ttoi64(id);
    if (recipientId <= 0 || recipientId == ChatClientCore::GetUserId())
    {
        AfxMessageBox(L"Enter a valid ID belonging to another user.");
        return;
    }
    AddConversation(id, true);
    m_chatLog.SetWindowTextW(L"Conversation opened. Messages from this user will appear here when delivered by the server.\r\n\r\n");
    for (const CString& savedMessage : m_messages)
        AppendChatText(savedMessage, false);
    Invalidate(FALSE);
}

void CChatProjClientView::OnConversationSelectionChanged()
{
    const int index = m_conversationList.GetCurSel();
    if (index == LB_ERR || index < 0 || index >= static_cast<int>(m_conversationIds.size()))
        return;

    AddConversation(m_conversationIds[static_cast<size_t>(index)], true);
    ShowPage(Page::Chat);
}

void CChatProjClientView::OnConnect()
{
    if (m_connected)
        return;

    ChatClientCore::SetMessageCallback(
        [this](const std::string& received)
        {
            std::string* copy = new std::string(received);

            if (!::PostMessage(
                GetSafeHwnd(), WM_APP + 1, 0,
                reinterpret_cast<LPARAM>(copy)))
            {
                delete copy;
            }
        });

    if (ChatClientCore::Connect("127.0.0.1", 5000))
    {
        m_connected = true;
        m_connectButton.EnableWindow(FALSE);

        AppendChatText(
            L"Connected to server. Waiting for server welcome message.");
        UpdateChatHeader();
    }
    else
    {
        ChatClientCore::SetMessageCallback(nullptr);

        AfxMessageBox(
            L"Cannot connect to 127.0.0.1:5000.\n"
            L"Start Chatserver.exe first.",
            MB_ICONWARNING);
    }
}

void CChatProjClientView::AppendChatText(const CString& text, bool remember)
{
    CString line(text);

    // Some incoming payloads contain escaped newline sequences as literal text.
    // Convert them to spaces so one message always occupies one display line.
    line.Replace(L"\\r\\n", L" ");
    line.Replace(L"\\n", L" ");
    line.Replace(L"\\r", L" ");
    line.Replace(L"\r\n", L" ");
    line.Replace(L"\n", L" ");
    line.Replace(L"\r", L" ");
    line.Trim();

    if (line.IsEmpty())
        return;

    if (remember)
        m_messages.push_back(line);

    const bool isPrivate = line.Left(12) == L"Private Msg:";
    const bool isBroadcast = line.Left(14) == L"Broadcast Msg:";

    LONG end = m_chatLog.GetTextLength();
    m_chatLog.SetSel(end, end);

    // Prefix is colored; the message body stays dark for easy reading.
    if (isPrivate || isBroadcast)
    {
        const int colon = line.Find(L':');
        CString label = (colon >= 0) ? line.Left(colon + 1) : line;
        CString body = (colon >= 0) ? line.Mid(colon + 1) : CString();

        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR | CFM_BOLD;
        format.dwEffects = CFE_BOLD;
        format.crTextColor = isPrivate ? RGB(37, 99, 235) : RGB(14, 116, 200);
        m_chatLog.SetSelectionCharFormat(format);
        m_chatLog.ReplaceSel(label, FALSE);

        format.dwEffects = 0;
        format.crTextColor = RGB(31, 41, 55);
        m_chatLog.SetSelectionCharFormat(format);
        m_chatLog.ReplaceSel(body + L"\r\n", FALSE);
    }
    else
    {
        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR | CFM_BOLD;
        format.dwEffects = 0;
        format.crTextColor = RGB(31, 41, 55);
        m_chatLog.SetSelectionCharFormat(format);
        m_chatLog.ReplaceSel(line + L"\r\n", FALSE);
    }

    m_chatLog.SendMessage(EM_SCROLLCARET, 0, 0);
}

LRESULT CChatProjClientView::OnNetworkMessage(WPARAM, LPARAM lp)
{
    std::unique_ptr<std::string> received(
        reinterpret_cast<std::string*>(lp));

    if (!received)
        return 0;

    CString line = Utf8ToCString(*received);

    if (line.Left(8) == L"WELCOME|")
    {
        CString payload = line.Mid(8);
        int first = payload.Find(L'|');
        int second = (first >= 0) ? payload.Find(L'|', first + 1) : -1;

        if (first >= 0 && second >= 0)
        {
            m_loggedInUserId = _ttoi64(payload.Mid(first + 1, second - first - 1));
            m_displayName = payload.Mid(second + 1);
            if (m_currentPage == Page::Dashboard)
                ShowPage(Page::Dashboard);
        }
        AppendChatText(L"Connected and authenticated.");
        return 0;
    }

    if (line.Left(2) == L"B|" || line.Left(2) == L"P|")
    {
        const bool isBroadcast = (line.Left(2) == L"B|");
        CString payload = line.Mid(2);
        int separator = payload.Find(L'|');

        if (separator >= 0)
        {
            CString senderId = payload.Left(separator);
            CString messageText = payload.Mid(separator + 1);
            messageText.Replace(L"\\r\\n", L" ");
            messageText.Replace(L"\\n", L" ");
            messageText.Replace(L"\\r", L" ");
            messageText.Replace(L"\r\n", L" ");
            messageText.Replace(L"\n", L" ");
            messageText.Replace(L"\r", L" ");

            CString formatted;
            formatted.Format(
                isBroadcast ? L"Broadcast Msg: From user ID %s - %s"
                : L"Private Msg: From user ID %s - %s",
                senderId.GetString(), messageText.GetString());

            if (!isBroadcast)
                AddConversation(senderId, false);
            AppendChatText(formatted);
            return 0;
        }
    }

    if (line.Left(6) == L"ERROR|" || line.Left(11) == L"AUTH_ERROR|")
    {
        AppendChatText(line);
        return 0;
    }

    AppendChatText(line);
    return 0;
}

void CChatProjClientView::OnSendMessage()
{
    const bool privateMode = (m_currentPage == Page::Chat);
    const bool broadcastMode = (m_currentPage == Page::Groups);

    if (!privateMode && !broadcastMode)
        return;

    if (!m_connected)
    {
        AfxMessageBox(L"Connect to the server first.");
        return;
    }

    CString recipient;
    std::string outgoing;

    if (privateMode)
    {
        recipient = m_selectedRecipientId;
        if (recipient.IsEmpty())
            m_search.GetWindowText(recipient);
        recipient.Trim();

        if (recipient.IsEmpty())
        {
            AfxMessageBox(L"Enter the recipient's User ID in the left box.");
            return;
        }

        // Validate that the recipient ID contains digits only.
        for (int i = 0; i < recipient.GetLength(); ++i)
        {
            if (recipient[i] < L'0' || recipient[i] > L'9')
            {
                AfxMessageBox(L"User ID must contain digits only.");
                return;
            }
        }

        const long long recipientUserId = _ttoi64(recipient);
        if (recipientUserId <= 0 ||
            recipientUserId == ChatClientCore::GetUserId())
        {
            AfxMessageBox(L"Enter a valid ID belonging to another user.");
            return;
        }
    }

    CString input;
    m_input.GetWindowText(input);
    input.Trim();

    if (input.IsEmpty())
        return;

    const std::string message = CStringToUtf8(input);

    if (privateMode)
    {
        outgoing = "P|" + CStringToUtf8(recipient) + "|" + message;
        AddConversation(recipient, true);
    }
    else
        outgoing = "B|" + message;

    if (!ChatClientCore::SendChatMessage(outgoing))
    {
        AfxMessageBox(broadcastMode
            ? L"Broadcast failed. Check the server connection."
            : L"Private message failed. Check the connection.");
        return;
    }

    m_input.SetWindowTextW(L"");
}


void CChatProjClientView::OnSize(UINT nType, int cx, int cy)
{
    CView::OnSize(nType, cx, cy);
    LayoutControls(cx, cy);
}

BOOL CChatProjClientView::OnPreparePrinting(CPrintInfo* pInfo)
{
    return DoPreparePrinting(pInfo);
}

void CChatProjClientView::OnBeginPrinting(CDC*, CPrintInfo*)
{
}

void CChatProjClientView::OnEndPrinting(CDC*, CPrintInfo*)
{
}

void CChatProjClientView::OnFilePrintPreview()
{
    AFXPrintPreview(this);
}

void CChatProjClientView::OnRButtonUp(UINT, CPoint point)
{
    ClientToScreen(&point);
    OnContextMenu(this, point);
}

void CChatProjClientView::OnContextMenu(CWnd*, CPoint point)
{
    theApp.GetContextMenuManager()->ShowPopupMenu(
        IDR_POPUP_EDIT, point.x, point.y, this, TRUE);
}

#ifdef _DEBUG

void CChatProjClientView::AssertValid() const
{
    CView::AssertValid();
}

void CChatProjClientView::Dump(CDumpContext& dc) const
{
    CView::Dump(dc);
}

CChatProjClientDoc* CChatProjClientView::GetDocument() const
{
    ASSERT(m_pDocument->IsKindOf(
        RUNTIME_CLASS(CChatProjClientDoc)));

    return (CChatProjClientDoc*)m_pDocument;
}

#endif