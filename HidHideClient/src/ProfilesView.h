// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <vector>

// Presentation only. No repository, coordinator, or driver dependencies.
namespace ProfilesView
{
    bool HighContrast();
    bool Dark();
    // Cached on the UI thread. Reads Windows preferences only at startup/change.
    void RefreshTheme();
    void OverrideThemeForAcceptance(int dark); // -1 follows Windows; 0/1 isolated fixtures
    void ApplyWindowTheme(HWND window);
    constexpr UINT ThemeChangedMessage = WM_APP + 40;
    class ThemeObserver
    {
    public:
        explicit ThemeObserver(HWND window);
        ~ThemeObserver();
    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
    COLORREF Background();
    COLORREF Surface();
    COLORREF Ink();
    COLORREF Muted();
    COLORREF Line();
    COLORREF Accent();
    COLORREF AccentText();
    COLORREF Selected();
    COLORREF Subtle();
    COLORREF Changed();
    COLORREF Warning();
    COLORREF WarningText();
    COLORREF Success();
    void AccessibleName(HWND window, std::wstring const& name, DWORD child = 0);
    void Font(CFont& font, int pixels, UINT dpi, int weight = FW_NORMAL);

    // Keeps the native button/radio role, check state, keyboard behavior and focus.
    class Button : public CButton
    {
    public:
        bool prominent{}, link{}, rail{};
    protected:
        afx_msg void OnPaint();
        DECLARE_MESSAGE_MAP()
    };

    class Rail : public CListBox
    {
    public:
        struct Item { std::wstring name, state, section; int count{}; bool dirty{}; };
        std::vector<Item> items;
        UINT dpi{96};
        CFont *normal{}, *smallFont{}, *bold{};
        void UpdateHeights();
        void DrawItem(LPDRAWITEMSTRUCT item) override;
        void MeasureItem(LPMEASUREITEMSTRUCT item) override;
    };

    // A native combo retains selection, keyboard, popup and accessibility semantics.
    class Combo : public CComboBox
    {
    public:
        void DrawItem(LPDRAWITEMSTRUCT item) override;
        void MeasureItem(LPMEASUREITEMSTRUCT item) override;
    protected:
        afx_msg void OnPaint();
        DECLARE_MESSAGE_MAP()
    };
    class SearchEdit : public CEdit
    {
    protected:
        afx_msg void OnPaint();
        afx_msg void OnNcPaint();
        DECLARE_MESSAGE_MAP()
    };

    struct Device
    {
        std::wstring name, detail, current, identities;
        int visibility{}; // 0 Visible, 1 Hidden, 2 Mixed
        bool changed{}, writable{};
    };
    class DeviceTable : public CWnd
    {
    public:
        bool Create(CWnd* parent, UINT id);
        void Update(std::vector<Device> const& devices);
        void SetMetrics(UINT dpi, CFont* normal, CFont* smallFont, CFont* bold);
        std::function<void(size_t, bool)> choose;
        std::function<void(size_t)> details;
        size_t RowCount() const { return m_Data.size(); }
        size_t ControlRowCount() const { return m_Rows.size(); }
        bool Keyboard(MSG const& message);
        HWND Radio(size_t row, bool hidden) const;
        int ScrollPosition() const { return m_Scroll; }
    protected:
        afx_msg void OnPaint();
        afx_msg void OnSize(UINT, int, int);
        afx_msg void OnVScroll(UINT, UINT, CScrollBar*);
        afx_msg BOOL OnMouseWheel(UINT, short, CPoint);
        afx_msg HBRUSH OnCtlColor(CDC*, CWnd*, UINT);
        BOOL OnCommand(WPARAM, LPARAM) override;
        DECLARE_MESSAGE_MAP()
    private:
        struct Row
        {
            CStatic name, detail, current, changed;
            Button hidden, visible, info;
        };
        void Layout();
        void BindRows();
        void Scroll(int position);
        std::vector<std::unique_ptr<Row>> m_Rows;
        std::vector<Device> m_Data;
        UINT m_Dpi{96};
        int m_Scroll{};
        size_t m_FirstRow{};
        CFont *m_Normal{}, *m_Small{}, *m_Bold{};
        CBrush m_Surface, m_Changed;
    };
}
