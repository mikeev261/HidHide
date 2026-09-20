// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// HidHideClientDlg.cpp
#include "stdafx.h"
#include "HidHideClient.h"
#include "HidHideClientDlg.h"
#include "FilterDriverProxy.h"
#include "Utils.h"
#include "Logging.h"
#include "ManagerActivation.h"
#include <winver.h>
#pragma comment(lib, "version.lib")

UINT const WM_HIDHIDE_SHOW_MANAGER{ ::RegisterWindowMessageW(L"HidHide.AppProfiles.ShowManager") };
UINT const WM_TASKBAR_CREATED{ ::RegisterWindowMessageW(L"TaskbarCreated") };

namespace
{
    constexpr UINT WM_TRAY_ICON{ WM_APP + 1 };
    constexpr UINT WM_HIDE_AFTER_START{ WM_APP + 2 };
    constexpr UINT WM_DEVICES_CHANGED{ WM_APP + 3 };
    constexpr UINT WM_COALESCED_DEVICE_REFRESH{ WM_APP + 5 };
    constexpr UINT WM_CHANNEL_STATE_CHANGED{ WM_APP + 7 };
    constexpr UINT WM_CHANNEL_REQUEST{ WM_APP + 8 };
    constexpr UINT WM_DEVICE_REFRESH_COMPLETE{ WM_APP + 9 };
    struct ChannelDispatch
    {
        explicit ChannelDispatch(std::vector<std::uint8_t> value) : request(std::move(value)), completed(::CreateEventW(nullptr, TRUE, FALSE, nullptr))
        { if (!completed) throw std::system_error(::GetLastError(), std::system_category(), "Create channel dispatch event"); }
        ~ChannelDispatch() { ::CloseHandle(completed); }
        void Release() { if (references.fetch_sub(1) == 1) delete this; }
        bool editor{};
        std::vector<std::uint8_t> request, response; std::exception_ptr failure; HANDLE completed{};
        std::atomic_uint references{ 2 }; std::atomic_bool cancelled{};
    };
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
    ON_WM_WINDOWPOSCHANGING()
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_WM_SYSCOMMAND()
    ON_MESSAGE(WM_DEVICES_CHANGED, &CHidHideClientDlg::OnDevicesChanged)
    ON_MESSAGE(WM_COALESCED_DEVICE_REFRESH, &CHidHideClientDlg::OnCoalescedDeviceRefresh)
    ON_MESSAGE(WM_PROFILE_CHANGED, &CHidHideClientDlg::OnProfileChanged)
    ON_MESSAGE(WM_CHANNEL_STATE_CHANGED, &CHidHideClientDlg::OnChannelStateChanged)
    ON_MESSAGE(WM_CHANNEL_REQUEST, &CHidHideClientDlg::OnChannelRequest)
    ON_MESSAGE(WM_DEVICE_REFRESH_COMPLETE, &CHidHideClientDlg::OnCoalescedDeviceRefresh)
    ON_MESSAGE(WM_DPICHANGED, &CHidHideClientDlg::OnDpiChanged)
    ON_MESSAGE(ProfilesView::ThemeChangedMessage, &CHidHideClientDlg::OnThemeChanged)
    ON_MESSAGE(WM_THEMECHANGED, &CHidHideClientDlg::OnThemeChanged)
    ON_WM_SETTINGCHANGE()
    ON_WM_SYSCOLORCHANGE()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
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
    , m_ProfilesEnforcement{}
    , m_ProfileApplication{}
    , m_ProfilesCoordinator{}
    , m_ProfileDevices{}
    , m_ProfilesPage{}
    , m_DropTarget{}
    , m_hIcon{}
    , m_TabApplication{}
    , m_BlacklistDlg(*this, nullptr)
    , m_WhitelistDlg(*this, nullptr)
    , m_StartHidden(startHidden)
{
    TRACE_ALWAYS(L"");
    m_hIcon = ::AfxGetApp()->LoadIcon(IDR_DIALOG_APPLICATION);
}

_Use_decl_annotations_
CHidHideClientDlg::CHidHideClientDlg(CWnd* pParent, ProfilesAcceptanceContext& acceptance)
    : CHidHideClientDlg(pParent, false)
{
    m_Acceptance = &acceptance;
}

bool CHidHideClientDlg::AcceptanceStageProfile() { return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceStageProfile(); }
bool CHidHideClientDlg::AcceptanceApply() { return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceApply(); }
bool CHidHideClientDlg::AcceptanceLoaded() { return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceLoaded(); }
bool CHidHideClientDlg::AcceptanceMaintenanceFailure() const
{
    return m_Acceptance && m_ProfilesCoordinator && !m_ProfilesCoordinator->EffectiveSelectionVerified()
        && m_ProfilesCoordinator->Status().find(L"maintenance state could not be verified") != std::wstring::npos;
}
bool CHidHideClientDlg::AcceptanceRepositoryBlocked() const { return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceRepositoryBlocked(); }
bool CHidHideClientDlg::AcceptanceBlockedCommandsSafe() { return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceBlockedCommandsSafe(); }
bool CHidHideClientDlg::AcceptanceExerciseZeroWriteEvents()
{
    if (!m_Acceptance || !m_ProfilesPage) return false;
    ShowWindow(SW_MINIMIZE); OnDevicesChanged(0, 0); OnCoalescedDeviceRefresh(0, 0); ShowWindow(SW_RESTORE);
    OnDevicesChanged(0, 0); OnCoalescedDeviceRefresh(0, 0);
    (void)m_ProfilesCoordinator->AcceptanceScanNow();
    return m_ProfilesPage->AcceptanceExerciseDraftOnlyEvents();
}
bool CHidHideClientDlg::AcceptanceRestoreBackup(std::filesystem::path const& source)
{ return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceRestoreBackup(source); }
bool CHidHideClientDlg::AcceptanceDirtyPromptSemantics()
{ return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceDirtyPromptSemantics(); }
bool CHidHideClientDlg::AcceptanceObservationKnown(bool expectedKnown, bool expectedVerified)
{ return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceObservationKnown(expectedKnown, expectedVerified); }
bool CHidHideClientDlg::AcceptanceStartupFailure()
{ return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceStartupFailure(); }
bool CHidHideClientDlg::AcceptanceSavedEnforcementFailure()
{ return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceSavedEnforcementFailure(); }
void CHidHideClientDlg::AcceptanceNotifyDeviceChange() { if (m_Acceptance) PostMessageW(WM_DEVICES_CHANGED); }
bool CHidHideClientDlg::AcceptanceHiddenPresentationRefresh()
{
    if (!m_Acceptance || !m_ProfileApplication || !m_ProfilesCoordinator || !m_ProfilesPage) return false;
    ShowWindow(SW_SHOW); ShowWindow(SW_MINIMIZE); auto snapshot = m_ProfilesCoordinator->Snapshot(); auto id = snapshot.settings.selectedGlobalId;
    auto changed = snapshot.profiles.at(id); changed.name = L"Changed while minimized";
    auto saved = m_ProfileApplication->Apply(changed, m_ProfileApplication->Version(id));
    OnProfileChanged(1, 0); if (m_ProfilesPage->AcceptanceEditorShows(changed.name)) return false;
    ShowWindow(SW_RESTORE); if (!m_ProfilesPage->AcceptanceEditorShows(changed.name)) return false;
    // A suppressed status-only coordinator event must refresh status without
    // inventing an external-file conflict or replacing the detached draft.
    if (!m_ProfilesPage->AcceptanceStageDetachedStatusDraft()) return false;
    ShowWindow(SW_MINIMIZE); OnProfileChanged(0, 0); ShowWindow(SW_RESTORE);
    return m_ProfilesPage->AcceptanceDetachedDraftHasNoRepositoryConflict();
}
bool CHidHideClientDlg::AcceptanceDeviceBurstCoalesced()
{
    if (!m_Acceptance || !m_Acceptance->devicePipeline) return false; auto before = m_Acceptance->devices.RefreshCount();
    for (int event{}; event < 100; ++event) PostMessageW(WM_DEVICES_CHANGED); MSG message{};
    auto deadline = ::GetTickCount64() + 1000;
    while (::GetTickCount64() < deadline) { while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); } ::Sleep(10); }
    auto refreshes = m_Acceptance->devices.RefreshCount() - before; return refreshes >= 1 && refreshes <= 2;
}
bool CHidHideClientDlg::AcceptanceRetryActivation() { return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceRetryActivation(); }
bool CHidHideClientDlg::AcceptanceSearchSelectsOtherProfile() { return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceSearchSelectsOtherProfile(); }
bool CHidHideClientDlg::AcceptanceVerificationInvalidation()
{
    if (!m_Acceptance || !m_Acceptance->failProcessScans || !m_ProfilesPage || !m_ProfilesCoordinator
        || !m_ProfilesCoordinator->AcceptanceScanNow()
        || !m_ProfilesPage->AcceptanceVerificationInvalidation([this](bool fail) { *m_Acceptance->failProcessScans = fail; })) return false;
    UpdateTrayTooltip(); auto tooltip = std::wstring(m_NotifyIcon.szTip);
    return tooltip.find(L"saved selection; current unconfirmed") != std::wstring::npos
        && tooltip.find(L"last verified") == std::wstring::npos;
}
bool CHidHideClientDlg::AcceptanceMainDriverConflictAction(bool expectedAvailable, bool expectAllowedAppsDraft)
{ return m_Acceptance && m_ProfilesPage && m_ProfilesPage->AcceptanceMainDriverConflictAction(expectedAvailable, expectAllowedAppsDraft); }
bool CHidHideClientDlg::AcceptanceEnterTrayMode() { if (!m_Acceptance) return false; AddTrayIcon(); HideToTray(); return m_NotifyIcon.hWnd == m_hWnd && !IsWindowVisible(); }
bool CHidHideClientDlg::AcceptanceRepositoryDiagnosticsDoNotAdoptDriver()
{
    if (!m_Acceptance || !m_ProfilesCoordinator || !m_ProfilesPage) throw std::runtime_error("repository-domain fixture unavailable");
    auto verified = m_ProfilesCoordinator->RetryActivation();
    if (!verified.observedKnown || !m_ProfilesCoordinator->EffectiveSelectionVerified()) throw std::runtime_error("repository-domain verification scan failed");
    if (!m_ProfilesPage->AcceptanceDirtyRepositoryInvalidation()) throw std::runtime_error("dirty repository invalidation did not preserve/block/re-enable the draft");
    auto settings = m_Acceptance->repositoryRoot / L"settings.json"; std::ofstream corrupt(settings, std::ios::binary | std::ios::trunc); corrupt << "{"; corrupt.close();
    if (!m_ProfilesCoordinator->ReloadRepositoryIfChanged() || !m_ProfilesCoordinator->HasRepositoryDiagnostics() || m_ProfilesCoordinator->HasDriverConflict()) throw std::runtime_error("repository-only invalidity was not isolated");
    if (!AcceptanceMainDriverConflictAction(false)) throw std::runtime_error("main recovery UI exposed driver adoption for repository-only diagnostics");
    for (int repeat{}; repeat < 100; ++repeat) if (!m_ProfilesCoordinator->ReloadRepositoryIfChanged() || m_ProfilesCoordinator->Issues().size() > 1) throw std::runtime_error("repository diagnostics were not bounded");
    try { (void)m_ProfilesCoordinator->AdoptExternalState(); throw std::runtime_error("repository-only diagnostics allowed driver adoption"); } catch (std::logic_error const&) {}
    if (!m_Acceptance->driverConflictMode || !m_Acceptance->adoptionCount || !m_Acceptance->recoveryEvidence) throw std::runtime_error("combined conflict acceptance seam unavailable");
    auto adoptions = m_Acceptance->adoptionCount(); auto recoveryEvidence = m_Acceptance->recoveryEvidence();
    m_Acceptance->driverConflictMode(true); (void)m_ProfilesCoordinator->ObserveEnforcement();
    if (!m_ProfilesCoordinator->HasDriverConflict() || !m_ProfilesCoordinator->HasRepositoryDiagnostics()) throw std::runtime_error("combined repository and driver conflict was not represented");
    if (!AcceptanceMainDriverConflictAction(false)) throw std::runtime_error("combined conflict exposed driver adoption");
    try { (void)m_ProfilesCoordinator->AdoptExternalState(); throw std::runtime_error("combined conflict allowed driver adoption"); } catch (std::logic_error const&) {}
    if (m_Acceptance->adoptionCount() != adoptions || m_Acceptance->recoveryEvidence() != recoveryEvidence)
        throw std::runtime_error("combined conflict crossed the enforcement adoption boundary or changed recovery evidence");
    m_Acceptance->driverConflictMode(false);
    m_ProfilesCoordinator->ExitSafely();
    auto observed = m_Acceptance->enforcement.Observe();
    if (!observed.success || !observed.observedKnown || observed.observed.hidingEnabled) throw std::runtime_error("repository diagnostics did not preserve/restore the original driver baseline");
    return true;
}

bool CHidHideClientDlg::AcceptanceLiveProcessStatus()
{
    if (!m_Acceptance || !m_Acceptance->liveProcessMode || !m_ProfilesCoordinator || !m_ProfilesPage) return false;
    ShowWindow(SW_SHOW); UpdateWindow();
    if (!m_ProfilesPage->AcceptanceBeginLiveStatusDraft()) return false;
    auto writes = m_ProfilesCoordinator->RepositoryWriteCount();
    auto waitFor = [&](int mode, std::wstring const& high, std::wstring const& low, std::wstring const& effective)
    {
        m_Acceptance->liveProcessMode->store(mode); auto deadline = ::GetTickCount64() + 2500; MSG message{};
        while (::GetTickCount64() < deadline)
        {
            while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
            if (m_ProfilesPage->AcceptanceLiveStatus(high, low, effective)) return true;
            ::Sleep(20);
        }
        return false;
    };
    return waitFor(1, L"Active", L"Waiting", L"Status high")
        && waitFor(2, L"Active", L"Running (lower priority)", L"Status high")
        && waitFor(3, L"Waiting", L"Active", L"Status low")
        && waitFor(0, L"Waiting", L"Waiting", L"Default")
        && writes == m_ProfilesCoordinator->RepositoryWriteCount();
}

bool CHidHideClientDlg::AcceptanceAdoptionRepositoryRace()
{
    if (!m_Acceptance || !m_ProfilesCoordinator || !m_ProfilesPage || !m_Acceptance->driverConflictMode
        || !m_Acceptance->adoptionCount || !m_Acceptance->recoveryEvidence) return false;
    if (!m_ProfilesCoordinator->AcceptanceScanNow()) return false;
    m_Acceptance->driverConflictMode(true); (void)m_ProfilesCoordinator->ObserveEnforcement();
    auto adoptions = m_Acceptance->adoptionCount(); auto evidence = m_Acceptance->recoveryEvidence();
    auto settings = m_Acceptance->repositoryRoot / L"settings.json";
    auto rejected = m_ProfilesPage->AcceptanceMainDriverConflictAction(true, false, [settings]
    {
        std::ofstream corrupt(settings, std::ios::binary | std::ios::trunc); corrupt << "{";
    }, false);
    return rejected && m_Acceptance->adoptionCount() == adoptions && m_Acceptance->recoveryEvidence() == evidence
        && HidHide::Profiles::ReadBytes(settings) == "{" && m_ProfilesCoordinator->HasRepositoryDiagnostics()
        && !DriverAdoptionAvailable();
}

bool CHidHideClientDlg::AcceptanceAdoptionRetryAfterFailure()
{
    if (!m_Acceptance || !m_ProfilesCoordinator || !m_ProfilesPage || !m_Acceptance->driverConflictMode || !m_Acceptance->failReconcile) return false;
    if (!m_ProfilesCoordinator->AcceptanceScanNow()) return false;
    m_Acceptance->driverConflictMode(true); (void)m_ProfilesCoordinator->ObserveEnforcement(); m_Acceptance->failReconcile(true);
    auto writes = m_ProfilesCoordinator->RepositoryWriteCount();
    auto failed = m_ProfilesPage->AcceptanceMainDriverConflictAction(true, false, {}, false);
    if (!failed) throw std::runtime_error("failed adoption command did not return the expected non-applied state");
    if (!m_ProfilesPage->AcceptanceAdoptionFailureOffersRetry()) throw std::runtime_error("failed adoption did not leave a clean enabled Retry action");
    if (writes != m_ProfilesCoordinator->RepositoryWriteCount()) throw std::runtime_error("failed adoption wrote JSON");
    m_Acceptance->failReconcile(false); m_Acceptance->driverConflictMode(false);
    if (!m_ProfilesPage->AcceptanceRetryCurrentPolicy()) throw std::runtime_error("post-adoption retry did not verify the saved policy");
    if (writes != m_ProfilesCoordinator->RepositoryWriteCount()) throw std::runtime_error("post-adoption retry wrote JSON");
    return true;
}

bool CHidHideClientDlg::AcceptanceGlobalStatusSemantics()
{
    if (!m_Acceptance || !m_ProfilesCoordinator || !m_ProfilesPage || !m_ProfilesCoordinator->AcceptanceScanNow()) return false;
    m_ProfilesPage->RefreshCoordinatorPresentation(); UpdateTrayTooltip();
    if (!m_ProfilesPage->AcceptanceGlobalPresentation(L"Active Automatic fallback", L"is active", L"Automatic Global fallback")
        || std::wstring(m_NotifyIcon.szTip).find(L"Automatic fallback") == std::wstring::npos) throw std::runtime_error("Automatic fallback presentation was not distinct");
    if (!m_ProfilesPage->AcceptanceSetGlobalMode(true, false)) throw std::runtime_error("Use Global settings Apply did not verify"); UpdateTrayTooltip();
    if (!m_ProfilesPage->AcceptanceGlobalPresentation(L"Active manual Global", L"is active", L"Manual Global policy")
        || std::wstring(m_NotifyIcon.szTip).find(L"manual Use Global") == std::wstring::npos) throw std::runtime_error("manual Use Global presentation was not distinct");
    if (!m_ProfilesPage->AcceptanceSetGlobalMode(true, true)) throw std::runtime_error("Pause settings Apply did not verify"); UpdateTrayTooltip();
    if (!m_ProfilesPage->AcceptanceGlobalPresentation(L"Selected; hiding paused", L"hiding paused", L"Hiding paused")
        || std::wstring(m_NotifyIcon.szTip).find(L"paused; hiding off") == std::wstring::npos) throw std::runtime_error("paused Global presentation was not distinct");
    return true;
}

bool CHidHideClientDlg::AcceptanceChangedAdoptionDraftLifecycle()
{
    if (!m_Acceptance || !m_ProfilesCoordinator || !m_ProfilesPage || !m_Acceptance->driverConflictMode
        || !m_Acceptance->setAllowedApplications || !m_Acceptance->failReconcile) return false;
    auto catalogWrites = m_ProfilesCoordinator->RepositoryWriteCount();
    auto stage = [&](unsigned index)
    {
        ShowWindow(SW_SHOW); auto feeder = HidHide::Profiles::NormalizeExecutable(m_Acceptance->repositoryRoot / (L"AdoptedFeeder" + std::to_wstring(index) + L".exe"));
        { std::ofstream file(feeder, std::ios::binary); file << "adoption fixture"; }
        m_Acceptance->setAllowedApplications({ feeder }); m_Acceptance->driverConflictMode(true); (void)m_ProfilesCoordinator->ObserveEnforcement();
        return m_ProfilesPage->AcceptanceMainDriverConflictAction(true, true);
    };
    for (unsigned action{}; action < 3; ++action)
    {
        if (!stage(action)) throw std::runtime_error("changed Allowed apps were not staged for abandonment coverage");
        m_Acceptance->driverConflictMode(false);
        if (!m_ProfilesPage->AcceptanceAbandonAdoptionDraft(action)) throw std::runtime_error("Discard/navigation/close No did not resolve the adoption draft");
        if (catalogWrites != m_ProfilesCoordinator->RepositoryWriteCount()) throw std::runtime_error("abandoning an adoption draft wrote JSON");
    }
    if (!stage(3)) throw std::runtime_error("changed Allowed apps were not staged for saved-failure coverage");
    m_Acceptance->driverConflictMode(false); m_Acceptance->failReconcile(true);
    if (!m_ProfilesPage->AcceptanceApplyAdoptedSettingsFailure()) throw std::runtime_error("saved adopted settings failure was not clean and retryable");
    auto savedWrites = m_ProfilesCoordinator->RepositoryWriteCount(); m_Acceptance->failReconcile(false);
    if (!m_ProfilesPage->AcceptanceRetryCurrentPolicy() || m_ProfilesCoordinator->RepositoryWriteCount() != savedWrites)
        throw std::runtime_error("saved adopted settings could not retry without another JSON write");
    return true;
}

bool CHidHideClientDlg::AcceptanceObservationPropagation(bool throughSelection)
{
    if (!m_Acceptance || !m_Acceptance->observationMode || !m_ProfilesCoordinator || !m_ProfilesPage
        || !m_ProfilesCoordinator->AcceptanceScanNow()) return false;
    ShowWindow(SW_SHOW); m_ProfilesPage->RefreshCoordinatorPresentation(); UpdateTrayTooltip();
    auto writes = m_ProfilesCoordinator->RepositoryWriteCount(); m_Acceptance->observationMode(throughSelection ? 2 : 1);
    if (throughSelection) m_ProfilesPage->AcceptanceReloadSelectedProfile(); else PostMessageW(WM_DEVICES_CHANGED);
    MSG message{}; auto deadline = ::GetTickCount64() + 1500;
    while (::GetTickCount64() < deadline)
    {
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
        if (m_ProfilesPage->AcceptanceObservedPresentationAlreadyUpdated(!throughSelection))
        {
            auto tooltip = std::wstring(m_NotifyIcon.szTip);
            return tooltip.find(L"saved selection; current unconfirmed") != std::wstring::npos
                && m_ProfilesCoordinator->RepositoryWriteCount() == writes;
        }
        ::Sleep(20);
    }
    return false;
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
void CHidHideClientDlg::DoDataExchange(CDataExchange* pDX)
{
    TRACE_ALWAYS(L"");
    CDialogEx::DoDataExchange(pDX);
}

BOOL CHidHideClientDlg::OnInitDialog()
{
    TRACE_ALWAYS(L"");
    CDialogEx::OnInitDialog();
    ProfilesView::RefreshTheme();ProfilesView::ApplyWindowTheme(m_hWnd);
    m_ThemeObserver=std::make_unique<ProfilesView::ThemeObserver>(m_hWnd);
    SetWindowTextW(L"HidHide Profiles");
    auto dpi=::GetDpiForWindow(m_hWnd); CRect initialBounds(0,0,MulDiv(1280,static_cast<int>(dpi),96),MulDiv(800,static_cast<int>(dpi),96));
    ::AdjustWindowRectExForDpi(&initialBounds,static_cast<DWORD>(GetStyle()),FALSE,static_cast<DWORD>(GetExStyle()),dpi);
    SetWindowPos(nullptr,0,0,initialBounds.Width(),initialBounds.Height(),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    if (auto menu = GetSystemMenu(FALSE))
    {
        menu->AppendMenuW(MF_SEPARATOR);
        menu->AppendMenuW(MF_STRING, ABOUT_COMMAND, L"About HidHide Profiles...");
    }

    if (m_Acceptance)
    {
        m_ProfileApplication = std::make_unique<HidHide::Profiles::ProfileApplicationService>(m_Acceptance->repositoryRoot);
        auto initial = m_Acceptance->enforcement.Observe();
        m_ProfileApplication->OpenOrCreateObserved(initial.success && initial.observedKnown && !initial.conflict
            ? std::optional<std::set<std::filesystem::path>>(initial.observed.allowedApplications) : std::nullopt);
        auto testSystemIntegration = static_cast<bool>(m_Acceptance->startupIntegration);
        m_ProfilesCoordinator = std::make_unique<CProfilesCoordinator>(m_Acceptance->enforcement, m_Acceptance->repositoryRoot,
            testSystemIntegration, m_Acceptance->processes, m_Acceptance->startupIntegration,
            m_Acceptance->maintenanceSource ? m_Acceptance->maintenanceSource : CProfilesCoordinator::MaintenanceSource([] { return false; }), initial);
        m_ProfilesCoordinator->SetNotificationWindow(m_hWnd, WM_PROFILE_CHANGED);
        m_ProfilesPage = std::make_unique<CProfilesPage>(*m_ProfileApplication, *m_ProfilesCoordinator, m_Acceptance->devices, this);
        if (!m_ProfilesPage->Create(IDD_DIALOG_PROFILES_FIRST, this)) THROW_WIN32_LAST_ERROR;
        CRect clientRect; GetClientRect(clientRect); m_ProfilesPage->MoveWindow(clientRect); m_ProfilesPage->ShowWindow(SW_SHOW);
        if (m_Acceptance->devicePipeline)
        {
            m_DeviceStop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr); m_DeviceWake = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!m_DeviceStop || !m_DeviceWake) THROW_WIN32_LAST_ERROR; m_DeviceWorker = std::thread(&CHidHideClientDlg::DeviceWorkerMain, this);
        }
        m_ProfilesCoordinator->Tick();
        return TRUE;
    }

    m_FilterDriverProxy = std::make_unique<HidHide::FilterDriverProxy>(true, true);

    // Register this window as a drop target
    m_DropTarget.Register(this);

    // Set the dialog title and include the version number, as defined via a define from the build environment
    std::wostringstream title;
    title << HidHide::StringTable(IDS_DIALOG_APPLICATION) << L" v" << _L(BldProductVersion);
    SetWindowTextW(title.str().c_str());

    m_ProfilesEnforcement = std::make_unique<CProfilesEnforcementAdapter>(*m_FilterDriverProxy);
    auto repositoryRoot = HidHide::Profiles::DefaultRepositoryRoot(); m_ProfileApplication = std::make_unique<HidHide::Profiles::ProfileApplicationService>(repositoryRoot);
    auto initial = m_ProfilesEnforcement->Observe();
    m_ProfileApplication->OpenOrCreateObserved(initial.success && initial.observedKnown && !initial.conflict
        ? std::optional<std::set<std::filesystem::path>>(initial.observed.allowedApplications) : std::nullopt);
    m_ProfilesCoordinator = std::make_unique<CProfilesCoordinator>(*m_ProfilesEnforcement, repositoryRoot, true,
        CProfilesCoordinator::ProcessSource{}, CProfilesCoordinator::StartupIntegration{}, CProfilesCoordinator::MaintenanceSource{}, initial);
    m_ProfilesEnforcement->InstallConfigurationChannel();
    m_ProfilesCoordinator->SetNotificationWindow(m_hWnd, WM_PROFILE_CHANGED);
    // Production owns no profile controls: the editor is an independent process.
    // The native page remains available only to isolated historical acceptance fixtures.
    m_EditorService = std::make_unique<HidHide::Editor::Service>(*m_ProfileApplication, *m_ProfilesCoordinator, m_ProfileDevices);
    m_EditorServer = std::make_unique<HidHide::Channel::Server>(HidHide::Editor::PipeName, true);
    m_FilterDriverProxy->SetMaintenanceHandler([this]
    {
        if (m_Exiting || !IsWindowEnabled()) throw std::runtime_error("Close open HidHide dialogs before running setup");
        if (m_EditorService && m_EditorService->EditorOpen()) throw std::runtime_error("Close the HidHide Profiles editor before running setup. Apply or discard its unsaved changes first");
        if (m_ProfilesCoordinator && m_ProfilesCoordinator->HasLaunchedProcess())
            throw std::runtime_error("Close the directly launched application before running setup maintenance");
        if (m_ProfilesPage && !m_ProfilesPage->ConfirmAbandon(L"entering setup maintenance"))
            throw std::runtime_error("Setup maintenance was cancelled because a profile draft remains open");
        auto confirmed = m_ProfilesEnforcement->PrepareMaintenance(); m_MaintenancePrepared = true; EnableWindow(FALSE); return confirmed;
    });
    m_ConfigurationServer = std::make_unique<HidHide::Channel::Server>();
    m_ProfilesCoordinator->Tick();
    AddTrayIcon();
    m_ChannelStop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr); if (!m_ChannelStop) THROW_WIN32_LAST_ERROR;
    m_DeviceStop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr); m_DeviceWake = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_DeviceStop || !m_DeviceWake) THROW_WIN32_LAST_ERROR;
    PostMessageW(WM_HIDE_AFTER_START);
    CM_NOTIFY_FILTER filter{ sizeof(CM_NOTIFY_FILTER), CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES, CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE, 0, 0 };
    if (auto const result = ::CM_Register_Notification(&filter, m_hWnd, &DevicesChanged, &m_DeviceNotification); result != CR_SUCCESS)
        THROW_CONFIGRET(result);

    if (!::SetPropW(m_hWnd, HidHide::ManagerActivation::WindowProperty, reinterpret_cast<HANDLE>(1)))
        THROW_WIN32_LAST_ERROR;
    try
    {
        m_ChannelWorker = std::thread(&CHidHideClientDlg::ChannelWorkerMain, this);
        m_DeviceWorker = std::thread(&CHidHideClientDlg::DeviceWorkerMain, this); m_DeviceRefreshPending = true; ::SetEvent(m_DeviceWake);
    }
    catch (...)
    {
        ::SetEvent(m_ChannelStop); ::SetEvent(m_DeviceStop);
        if (m_ChannelWorker.joinable()) m_ChannelWorker.join(); if (m_DeviceWorker.joinable()) m_DeviceWorker.join(); throw;
    }
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
    wcscpy_s(m_NotifyIcon.szTip, L"HidHide Profiles");
    m_TrayIconAdded = FALSE != ::Shell_NotifyIconW(NIM_ADD, &m_NotifyIcon);
}

void CHidHideClientDlg::RemoveTrayIcon() noexcept
{
    if (nullptr != m_NotifyIcon.hWnd) ::Shell_NotifyIconW(NIM_DELETE, &m_NotifyIcon);
    m_NotifyIcon.hWnd = nullptr; m_TrayIconAdded = false;
}

void CHidHideClientDlg::HideToTray()
{
    ShowWindow(SW_HIDE);
    if (!m_HideNoticeShown)
    {
        m_HideNoticeShown = true;
        m_NotifyIcon.uFlags = NIF_INFO;
        wcscpy_s(m_NotifyIcon.szInfoTitle, L"HidHide Profiles");
        wcscpy_s(m_NotifyIcon.szInfo, L"Profile monitoring is still running. Use the tray icon to reopen or exit.");
        m_NotifyIcon.dwInfoFlags = NIIF_INFO;
        ::Shell_NotifyIconW(NIM_MODIFY, &m_NotifyIcon);
    }
}

void CHidHideClientDlg::ShowFromTray()
{
    if (!m_Acceptance)
    {
        try { HidHide::Editor::Launch(); }
        catch (std::exception const& error) { ::MessageBoxA(m_hWnd, error.what(), "HidHide Profiles", MB_OK | MB_ICONERROR); }
        return;
    }
    ShowWindow(SW_RESTORE);
    SetForegroundWindow();
}

void CHidHideClientDlg::UpdateTrayTooltip()
{
    if (!m_ProfilesCoordinator) return;
    auto const& selection = m_ProfilesCoordinator->EffectiveSelection(); auto const& snapshot = m_ProfilesCoordinator->Snapshot();
    auto selected = snapshot.profiles.find(selection.profileId); std::wstring profileLabel;
    if (selected != snapshot.profiles.end())
    {
        profileLabel = selected->second.name;
        if (!m_ProfilesCoordinator->EffectiveSelectionVerified()) profileLabel += L" (saved selection; current unconfirmed)";
        else if (selection.reason == HidHide::Profiles::SelectionReason::Paused) profileLabel += L" (paused; hiding off)";
        else if (selected->second.kind == HidHide::Profiles::Kind::Application) profileLabel += L" (running application)";
        else if (selection.reason == HidHide::Profiles::SelectionReason::ManualGlobal) profileLabel += L" (manual Use Global)";
        else profileLabel += L" (Automatic fallback)";
    }
    auto const activeProfileCount{ profileLabel.empty() ? 0u : 1u };
    auto const status = m_ProfilesCoordinator->Status();
    if (m_LastTrayProfileCount == activeProfileCount && m_LastTrayProfileLabel == profileLabel && m_LastStatus == status) return;

    std::wostringstream text;
    text << L"HidHide Profiles v" << _L(BldProductVersion);
    if (!profileLabel.empty()) text << L" - " << profileLabel;
    text << L" - " << status;
    SetWindowTextW(L"HidHide Profiles");
    m_NotifyIcon.uFlags = NIF_TIP;
    wcsncpy_s(m_NotifyIcon.szTip, text.str().c_str(), _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &m_NotifyIcon);
    m_LastTrayProfileCount = activeProfileCount;
    m_LastTrayProfileLabel = std::move(profileLabel);
    m_LastStatus = status;
}

bool CHidHideClientDlg::DriverAdoptionAvailable() const
{
    return m_ProfilesCoordinator && m_ProfilesCoordinator->HasDriverConflict()
        && !m_ProfilesCoordinator->HasRepositoryDiagnostics();
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
    if (m_ProfilesPage) m_ProfilesPage->ShowWindow(SW_SHOW);
}

_Use_decl_annotations_
void CHidHideClientDlg::OnTcnSelchangeTabApplication(NMHDR* pNMHDR, LRESULT* pResult)
{
    TRACE_ALWAYS(L"");
    UNREFERENCED_PARAMETER(pNMHDR);
    ResyncTabDialogVisibilityState();
    *pResult = 0;
}

void CHidHideClientDlg::OnWindowPosChanging(WINDOWPOS* position)
{
    // MFC's modal loop attempts to show on idle; suppress that in the resident
    // engine, including launch/maximize messages, without constructing a page.
    if (!m_Acceptance) { position->flags &= ~SWP_SHOWWINDOW; position->flags |= SWP_HIDEWINDOW; }
    CDialogEx::OnWindowPosChanging(position);
}

_Use_decl_annotations_
void CHidHideClientDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
    TRACE_ALWAYS(L"");
    CDialogEx::OnShowWindow(bShow, nStatus);
    ResyncTabDialogVisibilityState();
    if (bShow && m_DeviceRefreshPending) PostMessageW(WM_COALESCED_DEVICE_REFRESH);
    if (bShow && !IsIconic() && m_ProfileStatusPending && m_ProfilesPage)
    {
        auto repository = std::exchange(m_ProfileRepositoryPending, false); m_ProfileStatusPending = false;
        if (repository) m_ProfilesPage->RepositoryChanged(); else m_ProfilesPage->RefreshCoordinatorPresentation();
    }
}

void CHidHideClientDlg::ChannelWorkerMain() noexcept
{
    HANDLE waits[]{ m_ChannelStop, m_ConfigurationServer->WakeHandle(), m_EditorServer->WakeHandle() };
    while (true)
    {
        try
        {
            auto timeout = std::min(m_ConfigurationServer->WaitTimeoutMs(), m_EditorServer->WaitTimeoutMs());
            if (::WaitForMultipleObjects(3, waits, FALSE, timeout) == WAIT_OBJECT_0) return;
            auto dispatchRequest = [this](auto const& request, bool editor)
            {
                auto dispatch = new ChannelDispatch(request); dispatch->editor = editor;
                if (!::PostMessageW(m_hWnd, WM_CHANNEL_REQUEST, 0, reinterpret_cast<LPARAM>(dispatch)))
                { dispatch->Release(); dispatch->Release(); throw std::runtime_error("Configuration request could not reach the owner window"); }
                HANDLE waits[]{ m_ChannelStop, dispatch->completed }; auto wait = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wait != WAIT_OBJECT_0 + 1) { dispatch->cancelled = true; dispatch->Release(); throw std::runtime_error("Configuration request was cancelled during shutdown"); }
                auto failure = dispatch->failure; auto response = std::move(dispatch->response); dispatch->Release();
                if (failure) std::rethrow_exception(failure); return response;
            };
            if (m_ConfigurationServer) m_ConfigurationServer->Pump([&](auto const& request) { return dispatchRequest(request, false); });
            if (m_EditorServer) m_EditorServer->Pump([&](auto const& request) { return dispatchRequest(request, true); });
            if (m_MaintenancePrepared)
            {
                if (!m_ConfigurationServer->Connected()) { ::PostMessageW(m_hWnd, WM_CHANNEL_STATE_CHANGED, 1, 0); return; }
            }
        }
        catch (...) { ::PostMessageW(m_hWnd, WM_CHANNEL_STATE_CHANGED, 0, 0); }
    }
}

void CHidHideClientDlg::DeviceWorkerMain() noexcept
{
    HANDLE waits[]{ m_DeviceStop, m_DeviceWake };
    while (::WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
    {
        // Bound enumeration to one pass per 250 ms window. Device-interface
        // storms are folded together without delaying stop responsiveness.
        auto deadline = ::GetTickCount64() + 250;
        for (;;)
        {
            auto now = ::GetTickCount64(); if (now >= deadline) break;
            auto wait = ::WaitForMultipleObjects(2, waits, FALSE, static_cast<DWORD>(deadline - now));
            if (wait == WAIT_OBJECT_0) return; if (wait == WAIT_TIMEOUT) break;
        }
        try { if (m_Acceptance) m_Acceptance->devices.Refresh(); else m_ProfileDevices.Refresh(); } catch (...) {}
        ::PostMessageW(m_hWnd, WM_DEVICE_REFRESH_COMPLETE, 0, 0);
    }
}

void CHidHideClientDlg::OnClose()
{
    if (m_ProfilesPage && !m_ProfilesPage->ConfirmAbandon(L"closing the window")) return;
    HideToTray();
}

void CHidHideClientDlg::OnSysCommand(UINT id, LPARAM parameter)
{
    if ((id & 0xfff0) == ABOUT_COMMAND)
    {
        std::wstring message = L"HidHide Profiles\nProduct version: " + std::wstring(_L(BldProductVersion))
            + L"\nInstalled driver file version: " + InstalledDriverVersion()
            + L"\n\nBased on HidHide by Nefarius Software Solutions and Eric Korff de Gidts."
              L"\nThe Microsoft-signed upstream driver is distributed unchanged."
              L"\nSee the installed Driver\\LICENSE.rtf for upstream license terms.";
        MessageBoxW(message.c_str(), L"About HidHide Profiles", MB_OK | MB_ICONINFORMATION);
        return;
    }
    CDialogEx::OnSysCommand(id, parameter);
}

void CHidHideClientDlg::OnCancel()
{
    if (!m_Exiting)
    {
        OnClose();
        return;
    }
    CDialogEx::OnCancel();
}

void CHidHideClientDlg::OnOK()
{
    OnClose();
}

void CHidHideClientDlg::OnDestroy()
{
    m_ThemeObserver.reset(); // Revoke callbacks before this HWND can be destroyed/reused.
    if (!m_Acceptance) ::RemovePropW(m_hWnd, HidHide::ManagerActivation::WindowProperty);
    if (m_DeviceNotification)
    {
        ::CM_Unregister_Notification(m_DeviceNotification);
        m_DeviceNotification = nullptr;
    }
    if (m_ChannelStop) ::SetEvent(m_ChannelStop);
    if (m_DeviceStop) ::SetEvent(m_DeviceStop);
    if (m_ChannelWorker.joinable()) m_ChannelWorker.join();
    if (m_DeviceWorker.joinable()) m_DeviceWorker.join();
    MSG pending{}; while (::PeekMessageW(&pending, m_hWnd, WM_CHANNEL_REQUEST, WM_CHANNEL_REQUEST, PM_REMOVE))
    {
        auto dispatch = reinterpret_cast<ChannelDispatch*>(pending.lParam); dispatch->cancelled = true; dispatch->Release();
    }
    m_ConfigurationServer.reset();
    m_EditorServer.reset(); m_EditorService.reset();
    if (m_ChannelStop) { ::CloseHandle(m_ChannelStop); m_ChannelStop = nullptr; }
    if (m_DeviceStop) { ::CloseHandle(m_DeviceStop); m_DeviceStop = nullptr; }
    if (m_DeviceWake) { ::CloseHandle(m_DeviceWake); m_DeviceWake = nullptr; }
    if (m_ProfilesCoordinator) { m_ProfilesCoordinator->SetNotificationWindow(nullptr, 0); m_ProfilesCoordinator->Stop(); }
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
        menu.AppendMenuW(MF_STRING, TRAY_COMMAND_SHOW, L"Open HidHide Profiles");
        if (m_Acceptance)
        {
            menu.AppendMenuW(MF_STRING, TRAY_COMMAND_PAUSE, L"Stage Pause (Apply in window)...");
            if (DriverAdoptionAvailable()) menu.AppendMenuW(MF_STRING, TRAY_COMMAND_ACCEPT_CURRENT, L"Resolve conflict: accept current driver settings");
            menu.AppendMenuW(MF_STRING, TRAY_COMMAND_RESUME, L"Stage Resume (Apply in window)...");
        }
        menu.AppendMenuW(MF_SEPARATOR);
        menu.AppendMenuW(MF_STRING, TRAY_COMMAND_EXIT, L"Exit and restore device settings");

        CPoint point;
        ::GetCursorPos(&point);
        SetForegroundWindow();
        auto const command{ menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, this) };
        if (TRAY_COMMAND_SHOW == command) ShowFromTray();
        try
        {
            if (TRAY_COMMAND_ACCEPT_CURRENT == command) { ShowFromTray(); if (m_ProfilesPage) (void)m_ProfilesPage->AdoptCurrentDriverSettings(); }
            if (TRAY_COMMAND_RESUME == command) { ShowFromTray(); if (m_ProfilesPage) m_ProfilesPage->StagePause(false); }
            if (TRAY_COMMAND_PAUSE == command) { ShowFromTray(); if (m_ProfilesPage) m_ProfilesPage->StagePause(true); }
            UpdateTrayTooltip();
        }
        catch (std::exception const& error) { ::MessageBoxA(m_hWnd, error.what(), "HidHide configuration", MB_OK | MB_ICONERROR); }
        if (TRAY_COMMAND_EXIT == command)
        {
            if (m_ProfilesPage && !m_ProfilesPage->ConfirmAbandon(L"exiting HidHide Profiles")) return 0;
            try { m_ProfilesCoordinator->ExitSafely(); m_Exiting = true; CDialogEx::OnCancel(); }
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
    if (!m_Acceptance)
    {
        ShowWindow(SW_HIDE);
        if (!m_StartHidden && !std::exchange(m_EditorPrelaunched, false)) ShowFromTray();
    }
    else if (m_StartHidden) ShowWindow(SW_HIDE);
    return 0;
}

_Use_decl_annotations_
LRESULT CHidHideClientDlg::OnShowManager(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    m_StartHidden = false;
    ShowFromTray();
    return HidHide::ManagerActivation::Acknowledged;
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


HidHide::DeviceInstancePaths CHidHideClientDlg::Baseline()
{ return m_FilterDriverProxy->GetBlacklist(); }

void CHidHideClientDlg::EditBaseline(HidHide::DeviceInstancePaths const& displayed, HidHide::DeviceInstancePaths const& requested)
{ m_FilterDriverProxy->SetBlacklist(displayed, requested); }

void CHidHideClientDlg::SetEnabled(bool displayed, bool requested)
{
    UNREFERENCED_PARAMETER(displayed); UNREFERENCED_PARAMETER(requested);
}

LRESULT CHidHideClientDlg::OnDevicesChanged(WPARAM, LPARAM)
{
    m_DeviceRefreshPending = true; if (m_DeviceWake) ::SetEvent(m_DeviceWake);
    return 0;
}


LRESULT CHidHideClientDlg::OnCoalescedDeviceRefresh(WPARAM, LPARAM)
{
    if (!IsWindowVisible() || IsIconic()) return 0;
    m_DeviceRefreshPending = false; if (m_ProfilesPage) m_ProfilesPage->DevicesChanged(); return 0;
}

LRESULT CHidHideClientDlg::OnProfileChanged(WPARAM repositoryChange, LPARAM)
{
    auto reloaded = repositoryChange != 0 && m_ProfilesCoordinator && m_ProfilesCoordinator->ReloadRepositoryIfChanged();
    if (m_ProfilesCoordinator) m_ProfilesCoordinator->Tick();
    if (m_ProfilesPage && IsWindowVisible() && !IsIconic()) { if (reloaded) m_ProfilesPage->RepositoryChanged(); else m_ProfilesPage->RefreshCoordinatorPresentation(); }
    else if (m_ProfilesPage) { m_ProfileStatusPending = true; m_ProfileRepositoryPending = m_ProfileRepositoryPending || reloaded; }
    UpdateTrayTooltip();
    return 0;
}

LRESULT CHidHideClientDlg::OnChannelStateChanged(WPARAM maintenanceComplete, LPARAM)
{
    if (maintenanceComplete) { m_Exiting = true; CDialogEx::OnCancel(); return 0; }
    if (m_ProfilesPage) m_ProfilesPage->RefreshCoordinatorPresentation(); UpdateTrayTooltip(); return 0;
}

LRESULT CHidHideClientDlg::OnChannelRequest(WPARAM, LPARAM value)
{
    auto& dispatch = *reinterpret_cast<ChannelDispatch*>(value);
    try
    {
        if (m_Exiting || dispatch.cancelled) throw std::runtime_error("HidHide Profiles is closing");
        if (dispatch.editor)
        {
            if (m_MaintenancePrepared || !m_EditorService) throw std::runtime_error("Setup maintenance is in progress");
            dispatch.response = m_EditorService->Handle(dispatch.request);
        }
        else dispatch.response = m_FilterDriverProxy->HandleRequest(dispatch.request);
        // The editor snapshot observes directly and editor mutations reconcile
        // through the coordinator; a second readback here repeated driver I/O.
        if (!dispatch.editor && m_ProfilesCoordinator) (void)m_ProfilesCoordinator->ObserveEnforcement();
        if (m_ProfilesPage) m_ProfilesPage->RefreshCoordinatorPresentation(); UpdateTrayTooltip();
    }
    catch (...) { dispatch.failure = std::current_exception(); }
    ::SetEvent(dispatch.completed); dispatch.Release(); return TRUE;
}

void CHidHideClientDlg::OnSize(UINT type, int width, int height)
{
    CDialogEx::OnSize(type, width, height);
    if (m_ProfilesPage && m_ProfilesPage->GetSafeHwnd() && type != SIZE_MINIMIZED)
        m_ProfilesPage->SetWindowPos(nullptr, 0, 0, std::max(0, width), std::max(0, height), SWP_NOZORDER);
    if (type != SIZE_MINIMIZED && m_ProfileStatusPending && m_ProfilesPage)
    {
        auto repository = std::exchange(m_ProfileRepositoryPending, false); m_ProfileStatusPending = false;
        if (repository) m_ProfilesPage->RepositoryChanged(); else m_ProfilesPage->RefreshCoordinatorPresentation();
    }
}

void CHidHideClientDlg::OnGetMinMaxInfo(MINMAXINFO* info)
{
    CDialogEx::OnGetMinMaxInfo(info);
    auto dpi = GetSafeHwnd() ? ::GetDpiForWindow(m_hWnd) : USER_DEFAULT_SCREEN_DPI;
    RECT bounds{0,0,::MulDiv(1040,static_cast<int>(dpi),96),::MulDiv(680,static_cast<int>(dpi),96)};
    ::AdjustWindowRectExForDpi(&bounds,static_cast<DWORD>(GetStyle()),FALSE,static_cast<DWORD>(GetExStyle()),dpi);
    info->ptMinTrackSize.x=bounds.right-bounds.left;info->ptMinTrackSize.y=bounds.bottom-bounds.top;
}

LRESULT CHidHideClientDlg::OnDpiChanged(WPARAM dpi,LPARAM value)
{
    if(m_ProfilesPage)m_ProfilesPage->SendMessageW(WM_DPICHANGED,dpi,0);
    auto rect=reinterpret_cast<RECT*>(value); if(rect)SetWindowPos(nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;
}

LRESULT CHidHideClientDlg::OnThemeChanged(WPARAM,LPARAM)
{
    ProfilesView::RefreshTheme();ProfilesView::ApplyWindowTheme(m_hWnd);
    if(m_ProfilesPage&&m_ProfilesPage->GetSafeHwnd())m_ProfilesPage->SendMessageW(ProfilesView::ThemeChangedMessage);
    return 0;
}
void CHidHideClientDlg::OnSettingChange(UINT flags,LPCTSTR section)
{ CDialogEx::OnSettingChange(flags,section);OnThemeChanged(0,0); }
void CHidHideClientDlg::OnSysColorChange()
{ ProfilesView::RefreshTheme();CDialogEx::OnSysColorChange();OnThemeChanged(0,0); }

bool CHidHideClientDlg::AcceptancePresentation(bool dirty,UINT dpi,int width,int height)
{
    return m_Acceptance&&m_ProfilesPage&&m_ProfilesPage->AcceptancePresentation(dirty,dpi,width,height);
}
