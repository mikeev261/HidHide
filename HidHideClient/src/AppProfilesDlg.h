// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
#pragma once

#include "FilterDriverProxy.h"
#include "DeviceSelectionTree.h"
#include "IDropTarget.h"

#include <optional>

class CHidHideClientDlg;

class CAppProfilesDlg : public CDialogEx, public HidHide::IDropTarget
{
    DECLARE_DYNAMIC(CAppProfilesDlg)

public:
    CAppProfilesDlg(_In_ CHidHideClientDlg& hidHideClientDlg, _In_opt_ CWnd* pParent);
    virtual ~CAppProfilesDlg();
    void DevicesChanged() { m_RefreshPending = true; }

    DROPEFFECT OnDragEnter(_In_ CWnd* pWnd, _In_ COleDataObject* pDataObject, _In_ DWORD dwKeyState, _In_ CPoint point) override;
    DROPEFFECT OnDragOver(_In_ CWnd* pWnd, _In_ COleDataObject* pDataObject, _In_ DWORD dwKeyState, _In_ CPoint point) override;
    DROPEFFECT OnDropEx(_In_ CWnd* pWnd, _In_ COleDataObject* pDataObject, _In_ DROPEFFECT dropDefault, _In_ DROPEFFECT dropList, _In_ CPoint point) override;

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_DIALOG_APP_PROFILES };
#endif

protected:
    void DoDataExchange(_In_ CDataExchange* pDX) override;
    BOOL OnInitDialog() override;
    DECLARE_MESSAGE_MAP()

private:
    CHidHideClientDlg& m_HidHideClientDlg;
    HidHide::FilterDriverProxy& FilterDriverProxy() noexcept;

    void RefreshApps(_In_opt_ HidHide::FullImageName const* selectProfile = nullptr);
    void RefreshDevices(HidHide::AppProfiles const& profiles);
    void UpdateProfileFromTree();
    void UpdateStatus();
    std::optional<HidHide::FullImageName> SelectedProfile() const;
    std::filesystem::path DisplayPath(_In_ HidHide::FullImageName const& fullImageName) const;
    CListBox m_AppsList;
    CTreeCtrl m_DevicesTree;
    CButton m_GamingOnly;
    CButton m_ShowDisconnected;
    CStatic m_ProfilePath;
    CStatic m_ProfileStatus;

    std::vector<HidHide::FullImageName> m_AppPaths;
    DeviceSelectionTree m_Selector;
    HidHide::FullImageNames m_DropTargetFullImageNames;
    HidHide::AppProfiles m_DisplayedProfiles;
    std::optional<HidHide::FullImageName> m_DisplayedProfile;
    bool m_RefreshPending{ false };
    bool m_SavePending{ false };
    bool m_Refreshing{ false };

    afx_msg void OnLbnSelchangeListApps();
    afx_msg void OnTvnItemChangedTreeDevices(_In_ NMHDR* pNMHDR, _Out_ LRESULT* pResult);
    afx_msg void OnBnClickedButtonAddApp();
    afx_msg void OnBnClickedButtonDelApp();
    afx_msg void OnBnClickedDeviceFilter();
    afx_msg void OnShowWindow(_In_ BOOL bShow, _In_ UINT nStatus);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
};
