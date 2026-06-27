// AppProfilesDlg.h
#pragma once
#include "FilterDriverProxy.h"
#include "HID.h"

class CHidHideClientDlg;

class CAppProfilesDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CAppProfilesDlg)

public:
    CAppProfilesDlg(_In_ CHidHideClientDlg& hidHideClientDlg, _In_opt_ CWnd* pParent);
    virtual ~CAppProfilesDlg();

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

    void RefreshApps();
    void RefreshDevices();

    CListBox m_AppsList;
    CTreeCtrl m_DevicesTree;
    HidHide::FriendlyNamesAndHidDeviceInformation m_DeviceItemData;

    afx_msg void OnLbnSelchangeListApps();
    afx_msg void OnTvnItemChangedTreeDevices(_In_ NMHDR* pNMHDR, _Out_ LRESULT* pResult);
    afx_msg void OnBnClickedButtonAddApp();
    afx_msg void OnBnClickedButtonDelApp();
    afx_msg void OnShowWindow(_In_ BOOL bShow, _In_ UINT nStatus);
};
