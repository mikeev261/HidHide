// SPDX-License-Identifier: MIT
#pragma once

#include "ProfilesCoordinator.h"
#include "ProfileApplicationService.h"
#include "ProfileDeviceProjection.h"
#include "ProfilesView.h"
#include <optional>

using ProfilesDeviceItem = HidHide::Profiles::ProfileDeviceGroup;
constexpr UINT WM_PROFILE_CHANGED{ WM_APP + 4 };
class IProfilesDeviceSource { public: virtual ~IProfilesDeviceSource() = default; virtual std::vector<ProfilesDeviceItem> Enumerate() = 0; virtual void Refresh() {} virtual std::uint64_t RefreshCount() const { return 0; } };
class CProductionProfilesDeviceSource final : public IProfilesDeviceSource
{
public:
    std::vector<ProfilesDeviceItem> Enumerate() override;
    void Refresh() override;
private:
    std::mutex m_Mutex;
    std::vector<ProfilesDeviceItem> m_Cached;
    bool m_Known{};
};

class CProfilesPage : public CDialogEx
{
    DECLARE_DYNAMIC(CProfilesPage)
public:
    CProfilesPage(HidHide::Profiles::ProfileApplicationService& application, CProfilesCoordinator& coordinator, IProfilesDeviceSource& devices, CWnd* parent);
    bool ConfirmAbandon(wchar_t const* action);
    void RefreshStatus();
    void RefreshCoordinatorPresentation();
    void RepositoryChanged();
    void DevicesChanged();
    void StagePause(bool paused);
    void StageAllowedApplications(std::set<std::filesystem::path> allowed);
    bool AcceptanceStageProfile();
    bool AcceptanceApply();
    bool AcceptanceLoaded();
    bool AcceptanceRepositoryBlocked() const;
    bool AcceptanceBlockedCommandsSafe();
    bool AcceptanceExerciseDraftOnlyEvents();
    bool AcceptanceRestoreBackup(std::filesystem::path const& source);
    bool AcceptanceDirtyPromptSemantics();
    bool AcceptanceObservationKnown(bool expectedKnown, bool expectedVerified);
    bool AcceptanceDetachedDraftHasNoRepositoryConflict() const;
    bool AcceptanceStageDetachedStatusDraft();
    bool AcceptanceStartupFailure();
    bool AcceptanceSavedEnforcementFailure();
    bool AcceptanceEditorShows(std::wstring const& name) const;
    bool AcceptanceRetryActivation();
    bool AcceptanceRetryCurrentPolicy();
    bool AcceptanceSearchSelectsOtherProfile();
    bool AcceptanceDirtyRepositoryInvalidation();
    bool AcceptanceVerificationInvalidation(std::function<void(bool)> failScans);
    bool AcceptanceMainDriverConflictAction(bool expectedAvailable, bool expectAllowedAppsDraft = false,
        std::function<void()> beforeConfirm = {}, bool expectedAdopted = true);
    bool AcceptanceAdoptionFailureOffersRetry();
    bool AcceptanceGlobalPresentation(std::wstring const& rowState, std::wstring const& topState, std::wstring const& reasonState) const;
    bool AcceptanceSetGlobalMode(bool manual, bool paused);
    bool AcceptanceAbandonAdoptionDraft(unsigned action);
    bool AcceptanceApplyAdoptedSettingsFailure();
    bool AcceptanceObservedPresentationAlreadyUpdated(bool expectedKnown) const;
    void AcceptanceReloadSelectedProfile();
    bool AcceptanceLiveStatus(std::wstring const& highState, std::wstring const& lowState, std::wstring const& effectiveName) const;
    bool AcceptanceBeginLiveStatusDraft();
    bool AcceptancePresentation(bool dirty, UINT dpi, int width, int height);
    bool AdoptCurrentDriverSettings();
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_DIALOG_PROFILES_FIRST };
#endif
protected:
    void DoDataExchange(CDataExchange* exchange) override;
    BOOL OnInitDialog() override;
    BOOL PreTranslateMessage(MSG* message) override;
    DECLARE_MESSAGE_MAP()
private:
    enum class RowVisibility { Visible, Hidden, Mixed };
    struct DeviceRow { std::wstring identity, friendly; bool connected{}, connectionKnown{ true }; std::optional<RowVisibility> currently; std::vector<std::wstring> policyIdentities; };
    void RefreshProfiles(std::optional<std::wstring> select = std::nullopt, bool revealSelection = true);
    std::wstring ProfileRowText(std::wstring const& id, HidHide::Profiles::Profile const& profile) const;
    void LoadProfile(std::wstring const& id);
    void RefreshDevices();
    void RefreshDraftControls();
    void RefreshDirtyState(std::wstring const& validation = {});
    bool Dirty() const;
    bool ApplyDraft();
    std::optional<std::wstring> SelectedId() const;
    static std::filesystem::path PickFolder(CWnd* owner, wchar_t const* title);
    void BeginNewApplication(std::filesystem::path const& executable);
    bool RestoreBackup(std::filesystem::path const& source);
    void Layout();
    void UpdateView();
    void UpdateRail();
    void SetDpi(UINT dpi);
    void ChooseVisibility(size_t index, bool hidden);
    void ShowDeviceDetails(size_t index);

    CProfilesCoordinator& m_Coordinator;
    HidHide::Profiles::ProfileApplicationService& m_Application;
    IProfilesDeviceSource& m_DeviceSource;
    CStatic m_Effective, m_Reason, m_Result;
    CEdit m_Path, m_Identity;
    CComboBox m_Mode;
    ProfilesView::Combo m_Global;
    ProfilesView::Button m_Pause, m_Hidden, m_Visible, m_Apply, m_Discard, m_Retry, m_ChangeApp, m_Enabled;
    ProfilesView::SearchEdit m_Search;
    CEdit m_Name, m_Priority;
    ProfilesView::Rail m_Profiles;
    CListCtrl m_Devices;
    std::vector<std::wstring> m_ProfileIds, m_GlobalIds;
    std::vector<DeviceRow> m_DeviceRows;
    std::optional<HidHide::Profiles::Profile> m_Saved, m_Draft;
    std::optional<HidHide::Profiles::SavedVersion> m_Expected;
    HidHide::Profiles::Settings m_SavedSettings, m_DraftSettings;
    HidHide::Profiles::SavedVersion m_ExpectedSettings;
    bool m_Refreshing{};
    bool m_DeleteStaged{};
    bool m_RepositoryWritable{ true };
    bool m_RetryAvailable{};
    UINT m_Dpi{96};
    CFont m_Font, m_SmallFont, m_BoldFont, m_TitleFont;
    CBrush m_BackgroundBrush, m_SurfaceBrush, m_SubtleBrush, m_DirtyBrush;
    ProfilesView::DeviceTable m_DeviceTable;
    std::map<int, std::unique_ptr<ProfilesView::Button>> m_StyledButtons;
    std::map<int, std::unique_ptr<CStatic>> m_Labels;
    CRect m_TopCard, m_EditorCard, m_TriggerCard, m_TableHeader;
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg HBRUSH OnCtlColor(CDC*, CWnd*, UINT);
    afx_msg void OnNewMenu();
    afx_msg void OnProfileMenu();
    afx_msg void OnAutomatic();
    afx_msg void OnUseGlobal();
    afx_msg void OnViewPath();
    afx_msg void OnAddRemembered();
    afx_msg LRESULT OnDpiChanged(WPARAM, LPARAM);
    afx_msg void OnSysColorChange();
    afx_msg LRESULT OnThemeChanged(WPARAM, LPARAM);

    afx_msg void OnProfileSelection();
    afx_msg void OnSearchChanged();
    afx_msg void OnDraftChanged();
    afx_msg void OnDeviceSelection(NMHDR*, LRESULT* result);
    afx_msg void OnVisibility();
    afx_msg void OnHiddenVisibility();
    afx_msg void OnVisibleVisibility();
    afx_msg void OnNewApplication();
    afx_msg void OnNewGlobal();
    afx_msg void OnChangeApplication();
    afx_msg void OnApply();
    afx_msg void OnDiscard();
    afx_msg void OnRetry();
    afx_msg void OnModeChanged();
    afx_msg void OnGlobalChanged();
    afx_msg void OnPause();
    afx_msg void OnAllowedApps();
    afx_msg void OnSettings();
    afx_msg void OnOpenFolder();
    afx_msg void OnImport();
    afx_msg void OnBackup();
    afx_msg void OnDelete();
    afx_msg void OnExport();
    afx_msg void OnOpenJson();
    afx_msg void OnSize(UINT type, int width, int height);
};
