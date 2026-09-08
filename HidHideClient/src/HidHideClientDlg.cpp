// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// HidHideClientDlg.cpp
#include "stdafx.h"
#include "HidHideClient.h"
#include "HidHideClientDlg.h"
#include "FilterDriverProxy.h"
#include "Utils.h"
#include "Logging.h"
#include <winver.h>
#pragma comment(lib, "version.lib")

UINT const WM_HIDHIDE_SHOW_MANAGER{ ::RegisterWindowMessageW(L"HidHide.AppProfiles.ShowManager") };
UINT const WM_TASKBAR_CREATED{ ::RegisterWindowMessageW(L"TaskbarCreated") };

namespace
{
    constexpr UINT_PTR PROFILE_TIMER_ID{ 42 };
    constexpr UINT PROFILE_TIMER_INTERVAL_MS{ 100 };
    constexpr UINT WM_TRAY_ICON{ WM_APP + 1 };
    constexpr UINT WM_HIDE_AFTER_START{ WM_APP + 2 };
    constexpr UINT WM_DEVICES_CHANGED{ WM_APP + 3 };
    DWORD CALLBACK DevicesChanged(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA, DWORD)
    {
        // The main window unregisters synchronously in OnDestroy, before HWND reuse.
        // Never access MFC objects or the driver on Configuration Manager's thread.
        if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL || action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL)
            ::PostMessageW(static_cast<HWND>(context), WM_DEVICES_CHANGED, 0, 0);
        return ERROR_SUCCESS;
    }
    constexpr UINT TRAY_COMMAND_SHOW{ 1 };
    constexpr UINT TRAY_COMMAND_EXIT{ 2 };
    constexpr UINT TRAY_COMMAND_ACCEPT_CURRENT{ 3 };
    constexpr UINT TRAY_COMMAND_RESUME{ 4 };
    constexpr UINT TRAY_COMMAND_PAUSE{ 5 };
    constexpr UINT ABOUT_COMMAND{ 0x0010 };

    std::wstring InstalledDriverVersion()
    {
        wchar_t system[MAX_PATH]{};
        auto length = ::GetSystemDirectoryW(system, MAX_PATH);
        if (!length || length >= MAX_PATH) return L"Unavailable";
        auto path = std::wstring(system) + L"\\drivers\\HidHide.sys";
        DWORD unused{};
        auto size = ::GetFileVersionInfoSizeW(path.c_str(), &unused);
        if (!size || size > 1024 * 1024) return L"Not installed or version unavailable";
        std::vector<BYTE> bytes(size);
        if (!::GetFileVersionInfoW(path.c_str(), 0, size, bytes.data())) return L"Unavailable";
        VS_FIXEDFILEINFO* info{}; UINT count{};
        if (!::VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info), &count) ||
            count < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != 0xfeef04bd) return L"Unavailable";
        std::wostringstream version;
        version << HIWORD(info->dwFileVersionMS) << L'.' << LOWORD(info->dwFileVersionMS)
            << L'.' << HIWORD(info->dwFileVersionLS) << L'.' << LOWORD(info->dwFileVersionLS);
        return version.str();
    }
}

#pragma warning(push)
#pragma warning(disable: 26454 28213) // Warnings caused by Microsoft MFC macros
BEGIN_MESSAGE_MAP(CHidHideClientDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_NOTIFY(TCN_SELCHANGE, IDC_TAB_APPLICATION, &CHidHideClientDlg::OnTcnSelchangeTabApplication)
    ON_WM_SHOWWINDOW()
    ON_WM_TIMER()
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_WM_SYSCOMMAND()
    ON_MESSAGE(WM_DEVICES_CHANGED, &CHidHideClientDlg::OnDevicesChanged)
    ON_MESSAGE(WM_TRAY_ICON, &CHidHideClientDlg::OnTrayIcon)
    ON_MESSAGE(WM_HIDE_AFTER_START, &CHidHideClientDlg::OnHideAfterStart)
    ON_REGISTERED_MESSAGE(WM_HIDHIDE_SHOW_MANAGER, &CHidHideClientDlg::OnShowManager)
    ON_REGISTERED_MESSAGE(WM_TASKBAR_CREATED, &CHidHideClientDlg::OnTaskbarCreated)
END_MESSAGE_MAP()
#pragma warning(pop)

_Use_decl_annotations_
CHidHideClientDlg::CHidHideClientDlg(CWnd* pParent, bool startHidden)
    : CDialogEx(IDD_DIALOG_APPLICATION, pParent)
    , m_FilterDriverProxy{}
    , m_ProfileManager{}
    , m_DropTarget{}
    , m_hIcon{}
    , m_TabApplication{}
    , m_BlacklistDlg(*this, nullptr)
    , m_WhitelistDlg(*this, nullptr)
    , m_AppProfilesDlg(*this, nullptr)
    , m_StartHidden(startHidden)
{
    TRACE_ALWAYS(L"");
    m_hIcon = ::AfxGetApp()->LoadIcon(IDR_DIALOG_APPLICATION);
}

HidHide::FilterDriverProxy& CHidHideClientDlg::FilterDriverProxy() noexcept
{
    return (*m_FilterDriverProxy.get());
}

bool CHidHideClientDlg::EffectiveHidingEnabled() const
{
    return HidHide::FilterDriverProxy::ReadDriverConfiguration().active;
}

_Use_decl_annotations_
bool CHidHideClientDlg::ProfileIsActive(HidHide::FullImageName const& profile) const noexcept
{
    return m_ProfileManager && m_ProfileManager->ProfileIsActive(profile);
}

_Use_decl_annotations_
void CHidHideClientDlg::DoDataExchange(CDataExchange* pDX)
{
    TRACE_ALWAYS(L"");
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_TAB_APPLICATION, m_TabApplication);
}

BOOL CHidHideClientDlg::OnInitDialog()
{
    TRACE_ALWAYS(L"");
    CDialogEx::OnInitDialog();
    if (auto menu = GetSystemMenu(FALSE))
    {
        menu->AppendMenuW(MF_SEPARATOR);
        menu->AppendMenuW(MF_STRING, ABOUT_COMMAND, L"About HidHide...");
    }

    m_FilterDriverProxy = std::make_unique<HidHide::FilterDriverProxy>(true, true);

    // Register this window as a drop target
    m_DropTarget.Register(this);

    // Set the dialog title and include the version number, as defined via a define from the build environment
    std::wostringstream title;
    title << HidHide::StringTable(IDS_DIALOG_APPLICATION) << L" v" << _L(BldProductVersion);
    SetWindowTextW(title.str().c_str());

    // Add tabs to tab-control so the height of the client rectangle is defined
    TCITEM tcItem;
    tcItem.mask = TCIF_TEXT;
    auto tabApplicationHeader0{ HidHide::StringTable(IDS_TAB_APPLICATION_HEADER_0) };
    tcItem.pszText = tabApplicationHeader0.data();
    m_TabApplication.InsertItem(0, &tcItem);
    auto tabApplicationHeader1{ HidHide::StringTable(IDS_TAB_APPLICATION_HEADER_1) };
    tcItem.pszText = tabApplicationHeader1.data();
    m_TabApplication.InsertItem(1, &tcItem);
    tcItem.pszText = const_cast<LPWSTR>(L"App Profiles");
    m_TabApplication.InsertItem(2, &tcItem);

    // Determine the proper offset for the tab dialogs
    CRect clientRect;
    CRect windowRect;
    m_TabApplication.GetClientRect(&clientRect);
    m_TabApplication.AdjustRect(FALSE, &clientRect);
    m_TabApplication.GetWindowRect(&windowRect);
    ScreenToClient(windowRect);
    clientRect.OffsetRect(windowRect.left, windowRect.top);

    // Create the dialogs (invisible per default)
    m_BlacklistDlg.Create(IDD_DIALOG_BLACKLIST, m_TabApplication.GetWindow(IDD_DIALOG_BLACKLIST));
    m_BlacklistDlg.MoveWindow(clientRect);
    m_WhitelistDlg.Create(IDD_DIALOG_WHITELIST, m_TabApplication.GetWindow(IDD_DIALOG_WHITELIST));
    m_WhitelistDlg.MoveWindow(clientRect);
    m_AppProfilesDlg.Create(IDD_DIALOG_APP_PROFILES, m_TabApplication.GetWindow(IDD_DIALOG_APP_PROFILES));
    m_AppProfilesDlg.MoveWindow(clientRect);

    m_ProfileManager = std::make_unique<CProfileManager>(*m_FilterDriverProxy);
    m_ProfileManager->Recover();
    m_FilterDriverProxy->SetMaintenanceHandler([this]
    {
        // A modal child runs a nested message loop. Ending the owner dialog from
        // its timer cannot unwind that loop, so ownership would remain held.
        // Refuse before changing baseline or marking this manager prepared.
        if (!IsWindowEnabled())
            throw std::runtime_error("Close open HidHide dialogs before running setup");
        auto confirmed = m_ProfileManager->PrepareMaintenance();
        m_MaintenancePrepared = true;
        EnableWindow(FALSE);
        return confirmed;
    });
    m_ConfigurationServer = std::make_unique<HidHide::Channel::Server>();
    m_ProfileManager->Tick();
    AddTrayIcon();
    SetTimer(PROFILE_TIMER_ID, PROFILE_TIMER_INTERVAL_MS, nullptr);
    if (m_StartHidden) PostMessageW(WM_HIDE_AFTER_START);
    CM_NOTIFY_FILTER filter{ sizeof(CM_NOTIFY_FILTER), CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES, CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE, 0, 0 };
    if (auto const result = ::CM_Register_Notification(&filter, m_hWnd, &DevicesChanged, &m_DeviceNotification); result != CR_SUCCESS)
        THROW_CONFIGRET(result);

    return (TRUE);
}

void CHidHideClientDlg::AddTrayIcon()
{
    m_LastTrayProfileCount = static_cast<size_t>(-1);
    m_NotifyIcon = {};
    m_NotifyIcon.cbSize = sizeof(m_NotifyIcon);
    m_NotifyIcon.hWnd = m_hWnd;
    m_NotifyIcon.uID = 1;
    m_NotifyIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_NotifyIcon.uCallbackMessage = WM_TRAY_ICON;
    m_NotifyIcon.hIcon = m_hIcon;
    wcscpy_s(m_NotifyIcon.szTip, L"HidHide App Profiles");
    ::Shell_NotifyIconW(NIM_ADD, &m_NotifyIcon);
}

void CHidHideClientDlg::RemoveTrayIcon() noexcept
{
    if (nullptr != m_NotifyIcon.hWnd) ::Shell_NotifyIconW(NIM_DELETE, &m_NotifyIcon);
    m_NotifyIcon.hWnd = nullptr;
}

void CHidHideClientDlg::HideToTray()
{
    ShowWindow(SW_HIDE);
    if (!m_HideNoticeShown)
    {
        m_HideNoticeShown = true;
        m_NotifyIcon.uFlags = NIF_INFO;
        wcscpy_s(m_NotifyIcon.szInfoTitle, L"HidHide App Profiles");
        wcscpy_s(m_NotifyIcon.szInfo, L"Profile monitoring is still running. Use the tray icon to reopen or exit.");
        m_NotifyIcon.dwInfoFlags = NIIF_INFO;
        ::Shell_NotifyIconW(NIM_MODIFY, &m_NotifyIcon);
    }
}

void CHidHideClientDlg::ShowFromTray()
{
    ShowWindow(SW_RESTORE);
    SetForegroundWindow();
}

void CHidHideClientDlg::UpdateTrayTooltip()
{
    if (!m_ProfileManager) return;
    auto const activeProfileCount{ m_ProfileManager->ActiveProfileCount() };
    auto const status = m_ProfileManager->Status();
    if (m_LastTrayProfileCount == activeProfileCount && m_LastStatus == status) return;

    std::wostringstream text;
    text << L"HidHide (mikeev261 fork) v" << _L(BldProductVersion);
    if (0 != activeProfileCount) text << L" - " << activeProfileCount << L" detected";
    text << L" - " << status;
    SetWindowTextW(text.str().c_str());
    m_NotifyIcon.uFlags = NIF_TIP;
    wcsncpy_s(m_NotifyIcon.szTip, text.str().c_str(), _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &m_NotifyIcon);
    m_LastTrayProfileCount = activeProfileCount;
    m_LastStatus = status;
}

void CHidHideClientDlg::OnPaint()
{
    TRACE_ALWAYS(L"");
    if (IsIconic())
    {
        CPaintDC dc(this); // device context for painting

        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        // Center icon in client rectangle
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

        // Draw the icon
        dc.DrawIcon(x, y, m_hIcon);
    }
    else
    {
        CDialogEx::OnPaint();
    }
}

HCURSOR CHidHideClientDlg::OnQueryDragIcon()
{
    TRACE_ALWAYS(L"");
    return static_cast<HCURSOR>(m_hIcon);
}

void CHidHideClientDlg::ResyncTabDialogVisibilityState()
{
    TRACE_ALWAYS(L"");
    switch (m_TabApplication.GetCurSel())
    {
    case 0: // Applications
        m_BlacklistDlg.ShowWindow(SW_HIDE);
        m_AppProfilesDlg.ShowWindow(SW_HIDE);
        m_WhitelistDlg.ShowWindow(SW_SHOW);
        m_DropTarget.SetRedirectionTarget(m_WhitelistDlg);
        break;
    case 1: // Devices
        m_WhitelistDlg.ShowWindow(SW_HIDE);
        m_AppProfilesDlg.ShowWindow(SW_HIDE);
        m_BlacklistDlg.ShowWindow(SW_SHOW);
        m_DropTarget.SetRedirectionTarget(m_BlacklistDlg);
        break;
    case 2: // App Profiles
        m_WhitelistDlg.ShowWindow(SW_HIDE);
        m_BlacklistDlg.ShowWindow(SW_HIDE);
        m_AppProfilesDlg.ShowWindow(SW_SHOW);
        m_DropTarget.SetRedirectionTarget(m_AppProfilesDlg);
        break;
    }
}

_Use_decl_annotations_
void CHidHideClientDlg::OnTcnSelchangeTabApplication(NMHDR* pNMHDR, LRESULT* pResult)
{
    TRACE_ALWAYS(L"");
    UNREFERENCED_PARAMETER(pNMHDR);
    ResyncTabDialogVisibilityState();
    *pResult = 0;
}

_Use_decl_annotations_
void CHidHideClientDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
    TRACE_ALWAYS(L"");
    CDialogEx::OnShowWindow(bShow, nStatus);
    m_TabApplication.SetCurSel(0);
    ResyncTabDialogVisibilityState();
}

_Use_decl_annotations_
_Use_decl_annotations_
void CHidHideClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    if ((PROFILE_TIMER_ID == nIDEvent) && m_ProfileManager)
    {
        try
        {
            auto const before = m_FilterDriverProxy->CachedConfiguration();
            if (m_ConfigurationServer) m_ConfigurationServer->Pump([this](auto const& request) { return m_FilterDriverProxy->HandleRequest(request); });
            if (m_MaintenancePrepared)
            {
                // Stop reconciling immediately. Allow the authenticated client to
                // consume its confirmation and close before destroying the pipe.
                if (!m_ConfigurationServer->Connected()) { m_Exiting = true; CDialogEx::OnCancel(); }
                return;
            }
            m_ProfileManager->Tick();
            if (HidHide::Maintenance::Active()) { UpdateTrayTooltip(); return; }
            if (before != m_FilterDriverProxy->CachedConfiguration())
            {
                m_BlacklistDlg.Refresh(true);
                m_WhitelistDlg.RefreshConfiguration();
                m_AppProfilesDlg.DevicesChanged();
            }
            UpdateTrayTooltip();
        }
        catch (std::exception const& error)
        {
            m_ProfileManager->ReportFailure(error.what());
            UpdateTrayTooltip();
        }
        catch (...)
        {
            LOGEXC_AND_CONTINUE;
        }
    }
    if (PROFILE_TIMER_ID == nIDEvent)
    {
        try { m_BlacklistDlg.SynchronizeActiveState(); }
        catch (...) { LOGEXC_AND_CONTINUE; }
        m_BlacklistDlg.RetryPendingRefresh();
    }
    CDialogEx::OnTimer(nIDEvent);
}

void CHidHideClientDlg::OnClose()
{
    HideToTray();
}

void CHidHideClientDlg::OnSysCommand(UINT id, LPARAM parameter)
{
    if ((id & 0xfff0) == ABOUT_COMMAND)
    {
        std::wstring message = L"HidHide (mikeev261 fork)\nFork version: " + std::wstring(_L(BldProductVersion))
            + L"\nInstalled driver file version: " + InstalledDriverVersion()
            + L"\n\nEnhanced configuration and App Profiles by mikeev261."
              L"\nBased on HidHide by Nefarius Software Solutions and Eric Korff de Gidts."
              L"\nThe Microsoft-signed upstream driver is distributed unchanged."
              L"\nSee the installed Driver\\LICENSE.rtf for upstream license terms.";
        MessageBoxW(message.c_str(), L"About HidHide", MB_OK | MB_ICONINFORMATION);
        return;
    }
    CDialogEx::OnSysCommand(id, parameter);
}

void CHidHideClientDlg::OnCancel()
{
    if (!m_Exiting)
    {
        HideToTray();
        return;
    }
    CDialogEx::OnCancel();
}

void CHidHideClientDlg::OnOK()
{
    HideToTray();
}

void CHidHideClientDlg::OnDestroy()
{
    if (m_DeviceNotification)
    {
        ::CM_Unregister_Notification(m_DeviceNotification);
        m_DeviceNotification = nullptr;
    }
    KillTimer(PROFILE_TIMER_ID);
    m_ConfigurationServer.reset();
    if (m_ProfileManager) m_ProfileManager->Stop();
    RemoveTrayIcon();
    CDialogEx::OnDestroy();
}

_Use_decl_annotations_
LRESULT CHidHideClientDlg::OnTrayIcon(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    auto const message{ static_cast<UINT>(lParam) };
    if ((WM_LBUTTONDBLCLK == message) || (WM_LBUTTONUP == message))
    {
        ShowFromTray();
    }
    else if ((WM_RBUTTONUP == message) || (WM_CONTEXTMENU == message))
    {
        CMenu menu;
        menu.CreatePopupMenu();
        menu.AppendMenuW(MF_STRING, TRAY_COMMAND_SHOW, L"Open HidHide App Profiles");
        menu.AppendMenuW(MF_STRING, TRAY_COMMAND_PAUSE, L"Pause automatic profiles and restore baseline");
        if (m_ProfileManager->HasConflict())
            menu.AppendMenuW(MF_STRING, TRAY_COMMAND_ACCEPT_CURRENT, L"Resolve conflict: accept current driver settings");
        menu.AppendMenuW(MF_STRING, TRAY_COMMAND_RESUME, L"Resume automatic profiles");
        menu.AppendMenuW(MF_SEPARATOR);
        menu.AppendMenuW(MF_STRING, TRAY_COMMAND_EXIT, L"Exit and restore device settings");

        CPoint point;
        ::GetCursorPos(&point);
        SetForegroundWindow();
        auto const command{ menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, this) };
        if (TRAY_COMMAND_SHOW == command) ShowFromTray();
        try
        {
            if (TRAY_COMMAND_ACCEPT_CURRENT == command) m_ProfileManager->AdoptExternalState();
            if (TRAY_COMMAND_RESUME == command) m_ProfileManager->Resume();
            if (TRAY_COMMAND_PAUSE == command) m_ProfileManager->Pause();
            UpdateTrayTooltip();
        }
        catch (std::exception const& error) { ::MessageBoxA(m_hWnd, error.what(), "HidHide configuration", MB_OK | MB_ICONERROR); }
        if (TRAY_COMMAND_EXIT == command)
        {
            try { m_ProfileManager->ExitSafely(); m_Exiting = true; CDialogEx::OnCancel(); }
            catch (std::exception const& error) { ::MessageBoxA(m_hWnd, error.what(), "HidHide restoration was not confirmed", MB_OK | MB_ICONERROR); }
        }
    }
    return 0;
}

_Use_decl_annotations_
LRESULT CHidHideClientDlg::OnHideAfterStart(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    ShowWindow(SW_HIDE);
    return 0;
}

_Use_decl_annotations_
LRESULT CHidHideClientDlg::OnShowManager(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    ShowFromTray();
    return 0;
}

_Use_decl_annotations_
LRESULT CHidHideClientDlg::OnTaskbarCreated(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    AddTrayIcon();
    UpdateTrayTooltip();
    return 0;
}


bool CHidHideClientDlg::ProfileIsUnresolved(HidHide::FullImageName const& profile) const noexcept
{ return m_ProfileManager && m_ProfileManager->ProfileIsUnresolved(profile); }

HidHide::DeviceInstancePaths CHidHideClientDlg::Baseline()
{ return m_FilterDriverProxy->GetBlacklist(); }

void CHidHideClientDlg::EditBaseline(HidHide::DeviceInstancePaths const& displayed, HidHide::DeviceInstancePaths const& requested)
{ m_FilterDriverProxy->SetBlacklist(displayed, requested); }

void CHidHideClientDlg::SetEnabled(bool displayed, bool requested)
{
    if (m_ProfileManager) m_ProfileManager->SetEnabled(displayed, requested);
    else m_FilterDriverProxy->SetActive(displayed, requested);
}

LRESULT CHidHideClientDlg::OnDevicesChanged(WPARAM, LPARAM)
{
    m_BlacklistDlg.Refresh(true);
    m_AppProfilesDlg.DevicesChanged();
    return 0;
}
