#include <windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <functional>
#include <uxtheme.h>
#include "sqlite3.h"
#include "resource.h"

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "legacy_stdio_definitions.lib")

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace Microsoft::WRL;

// -----------------------------------------------------------------------------
// Helper: 金額フォーマット (カンマ区切り)
// -----------------------------------------------------------------------------
std::wstring FormatMoney(float amount) {
    static bool s_initialized = false;
    static wchar_t s_decimalSep[4] = L".";
    static wchar_t s_thousandSep[4] = L",";

    if (!s_initialized) {
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, s_decimalSep, 4) == 0) wcscpy_s(s_decimalSep, L".");
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, s_thousandSep, 4) == 0) wcscpy_s(s_thousandSep, L",");
        s_initialized = true;
    }

    wchar_t plainNum[64];
    swprintf_s(plainNum, L"%.0f", amount);

    NUMBERFMTW fmt = { 0 };
    fmt.NumDigits = 0;       // 小数点以下なし
    fmt.LeadingZero = 0;
    fmt.Grouping = 3;        // 3桁区切り
    fmt.lpDecimalSep = s_decimalSep;
    fmt.lpThousandSep = s_thousandSep;
    fmt.NegativeOrder = 1;   // 負の数は -123

    wchar_t out[64];
    // フォーマット成功ならその値を、失敗なら数字をそのまま返す
    if (GetNumberFormatW(LOCALE_USER_DEFAULT, 0, plainNum, &fmt, out, 64) > 0) {
        return std::wstring(out);
    }
    return std::wstring(plainNum);
}

// -----------------------------------------------------------------------------
// ModernCalendar Class
// -----------------------------------------------------------------------------
namespace UiControls {
    const D2D1_COLOR_F COL_BG = D2D1::ColorF(1.0f, 1.0f, 1.0f);
    const D2D1_COLOR_F COL_TEXT = D2D1::ColorF(0.11f, 0.11f, 0.12f);
    const D2D1_COLOR_F COL_TEXT_DIM = D2D1::ColorF(0.6f, 0.6f, 0.6f);
    const D2D1_COLOR_F COL_ACCENT = D2D1::ColorF(0.0f, 0.48f, 1.0f);
    const D2D1_COLOR_F COL_HOVER = D2D1::ColorF(0.95f, 0.95f, 0.97f);
    const D2D1_COLOR_F COL_MARKED = D2D1::ColorF(0.88f, 0.95f, 1.0f);

    enum class CalendarViewMode { Day, Month, Year };
    struct DateInfo { int year; int month; int day; };

    class ModernCalendar {
    public:
        ModernCalendar() : m_hwnd(NULL), m_currentView(CalendarViewMode::Day), m_hoveredIndex(-1), m_isAnimating(false) {
            time_t t = time(0); struct tm now; localtime_s(&now, &t);
            m_currentYear = m_selectedYear = now.tm_year + 1900;
            m_currentMonth = m_selectedMonth = now.tm_mon + 1;
            m_selectedDay = now.tm_mday;
        }
        ~ModernCalendar() { DiscardDeviceResources(); }

        CalendarViewMode GetViewMode() const { return m_currentView; }
        int GetCurrentYear() const { return m_currentYear; }

        void SetMarkedDays(int year, int month, const std::vector<int>& days) {
            long long key = (long long)year * 100 + month;
            m_markedData[key].clear();
            for (int d : days) m_markedData[key].insert(d);
            InvalidateRect(m_hwnd, NULL, FALSE);
        }

        void SetMarkedMonths(int year, const std::set<int>& months) {
            m_markedMonthsData[year] = months;
            InvalidateRect(m_hwnd, NULL, FALSE);
        }

        void SetRecordedYears(const std::set<int>& years) {
            m_recordedYearsData = years;
            InvalidateRect(m_hwnd, NULL, FALSE);
        }

        HRESULT Initialize(HWND hwndParent) {
            m_hwnd = hwndParent;
            HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_pD2DFactory.GetAddressOf());
            if (SUCCEEDED(hr)) hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_pDWriteFactory.GetAddressOf()));

            if (SUCCEEDED(hr)) hr = m_pDWriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"ja-jp", m_pHeaderFormat.GetAddressOf());
            if (SUCCEEDED(hr)) { m_pHeaderFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); m_pHeaderFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }

            if (SUCCEEDED(hr)) hr = m_pDWriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 17.0f, L"ja-jp", m_pContentFormat.GetAddressOf());
            if (SUCCEEDED(hr)) { m_pContentFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); m_pContentFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }
            return hr;
        }

        void OnResize(UINT width, UINT height) { if (m_pRenderTarget) m_pRenderTarget->Resize(D2D1::SizeU(width, height)); }
        void OnPaint() { CreateDeviceResources(); if (m_pRenderTarget) Draw(); }

        void SetOnDateChanged(std::function<void(DateInfo)> callback) { m_onDateChangedCallback = callback; }
        void SetOnViewChanged(std::function<void(int year, int month)> callback) { m_onViewChangedCallback = callback; }

        DateInfo GetSelectedDate() const { return { m_selectedYear, m_selectedMonth, m_selectedDay }; }

        void OnMouseMove(int x, int y) {
            if (m_isAnimating) return;
            int prev = m_hoveredIndex; m_hoveredIndex = -1;
            float gridTop = m_contentRect.top + ((m_currentView == CalendarViewMode::Day) ? 24.0f : 0);

            if (x >= m_contentRect.left && x <= m_contentRect.right && y >= gridTop && y <= m_contentRect.bottom) {
                int cols = (m_currentView == CalendarViewMode::Day) ? 7 : 4;
                int rows = (m_currentView == CalendarViewMode::Day) ? 6 : (m_currentView == CalendarViewMode::Month ? 3 : 4);
                float cellW = (m_contentRect.right - m_contentRect.left) / cols;
                float cellH = (m_contentRect.bottom - gridTop) / rows;
                int c = (int)((x - m_contentRect.left) / cellW);
                int r = (int)((y - gridTop) / cellH);
                if (c >= 0 && c < cols && r >= 0 && r < rows) m_hoveredIndex = r * cols + c;
            }
            if (prev != m_hoveredIndex) InvalidateRect(m_hwnd, NULL, FALSE);
        }

        void OnLButtonDown(int x, int y) {
            if (m_isAnimating) return;
            if (x >= m_headerRect.left && x <= m_headerRect.right && y >= m_headerRect.top && y <= m_headerRect.bottom) {
                if (m_currentView == CalendarViewMode::Day) StartViewTransition(CalendarViewMode::Month);
                else if (m_currentView == CalendarViewMode::Month) StartViewTransition(CalendarViewMode::Year);
                return;
            }
            if (m_hoveredIndex != -1) {
                if (m_currentView == CalendarViewMode::Day) {
                    int f = GetDayOfWeek(m_currentYear, m_currentMonth, 1);
                    int d = m_hoveredIndex - f + 1;
                    if (d > 0 && d <= GetDaysInMonth(m_currentYear, m_currentMonth)) {
                        m_selectedDay = d; m_selectedMonth = m_currentMonth; m_selectedYear = m_currentYear;
                        if (m_onDateChangedCallback) m_onDateChangedCallback({ m_selectedYear, m_selectedMonth, m_selectedDay });
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                }
                else if (m_currentView == CalendarViewMode::Month) {
                    m_currentMonth = m_hoveredIndex + 1;
                    StartViewTransition(CalendarViewMode::Day);
                }
                else if (m_currentView == CalendarViewMode::Year) {
                    m_currentYear = (m_currentYear - (m_currentYear % 16)) + m_hoveredIndex;
                    StartViewTransition(CalendarViewMode::Month);
                }
            }
        }

        void OnMouseWheel(short delta) { StartScrollTransition((delta < 0) ? 1 : -1); }

        void OnTimer() {
            if (!m_isAnimating) return;
            DWORD t = GetTickCount(); m_animProgress = (t - m_animStartTime) / 250.0f;
            if (m_animProgress >= 1.0f) {
                m_animProgress = 1.0f; m_isAnimating = false; KillTimer(m_hwnd, 1);
                if (m_onViewChangedCallback) {
                    m_onViewChangedCallback(m_currentYear, m_currentMonth);
                }
            }
            InvalidateRect(m_hwnd, NULL, FALSE);
        }

        void SetSelectedDate(int y, int m, int d) {
            m_selectedYear = y; m_selectedMonth = m; m_selectedDay = d;
            m_currentYear = y; m_currentMonth = m;
            InvalidateRect(m_hwnd, NULL, FALSE);
        }

    private:
        HWND m_hwnd;
        ComPtr<ID2D1Factory> m_pD2DFactory;
        ComPtr<IDWriteFactory> m_pDWriteFactory;
        ComPtr<ID2D1HwndRenderTarget> m_pRenderTarget;
        ComPtr<ID2D1SolidColorBrush> m_pTextBrush, m_pTextDimBrush, m_pAccentBrush, m_pHoverBrush, m_pMarkedBrush;
        ComPtr<ID2D1SolidColorBrush> m_pWhiteBrush;
        ComPtr<IDWriteTextFormat> m_pHeaderFormat, m_pContentFormat;
        CalendarViewMode m_currentView;
        int m_currentYear, m_currentMonth, m_selectedYear, m_selectedMonth, m_selectedDay, m_hoveredIndex;
        D2D1_RECT_F m_headerRect, m_contentRect;
        bool m_isAnimating;
        float m_animProgress; DWORD m_animStartTime;
        struct Snap { CalendarViewMode v; int y; int m; } m_prev, m_next;
        enum AT { None, ZoomIn, ZoomOut, Next, Prev } m_at;
        std::map<long long, std::set<int>> m_markedData;
        std::map<int, std::set<int>> m_markedMonthsData;
        std::set<int> m_recordedYearsData;
        std::function<void(DateInfo)> m_onDateChangedCallback;
        std::function<void(int, int)> m_onViewChangedCallback;

        void CreateDeviceResources() {
            if (m_pRenderTarget) return;
            RECT rc; GetClientRect(m_hwnd, &rc);
            D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
            m_pD2DFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(m_hwnd, size), m_pRenderTarget.GetAddressOf());
            m_pRenderTarget->CreateSolidColorBrush(COL_TEXT, m_pTextBrush.GetAddressOf());
            m_pRenderTarget->CreateSolidColorBrush(COL_TEXT_DIM, m_pTextDimBrush.GetAddressOf());
            m_pRenderTarget->CreateSolidColorBrush(COL_ACCENT, m_pAccentBrush.GetAddressOf());
            m_pRenderTarget->CreateSolidColorBrush(COL_HOVER, m_pHoverBrush.GetAddressOf());
            m_pRenderTarget->CreateSolidColorBrush(COL_MARKED, m_pMarkedBrush.GetAddressOf());
            m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), m_pWhiteBrush.GetAddressOf());
        }
        void DiscardDeviceResources() {
            m_pRenderTarget.Reset(); m_pTextBrush.Reset(); m_pTextDimBrush.Reset();
            m_pAccentBrush.Reset(); m_pHoverBrush.Reset(); m_pMarkedBrush.Reset(); m_pWhiteBrush.Reset();
        }

        void Draw() {
            m_pRenderTarget->BeginDraw(); m_pRenderTarget->Clear(COL_BG);
            D2D1_SIZE_F sz = m_pRenderTarget->GetSize();
            float headerH = 30.0f;
            m_headerRect = D2D1::RectF(0, 0, sz.width, headerH);
            m_contentRect = D2D1::RectF(0, headerH, sz.width, sz.height);

            if (m_isAnimating) DrawAnim(sz); else DrawView(m_currentView, m_currentYear, m_currentMonth, 1, 1, 0);
            if (m_pRenderTarget->EndDraw() == D2DERR_RECREATE_TARGET) DiscardDeviceResources();
        }

        void DrawAnim(D2D1_SIZE_F sz) {
            float t = m_animProgress, e = 1.0f - (float)pow(1.0f - t, 3);
            D2D1_POINT_2F c = D2D1::Point2F(sz.width / 2, sz.height / 2);
            float os = 1, ns = 1, oo = 1, no = 0, oy = 0, ny = 0;
            if (m_at == AT::ZoomIn) { os = 1 + 0.2f * e; oo = 1 - e; ns = 0.8f + 0.2f * e; no = e; }
            else if (m_at == AT::ZoomOut) { os = 1 - 0.2f * e; oo = 1 - e; ns = 1.2f - 0.2f * e; no = e; }
            else { float d = 30; oy = (m_at == AT::Next ? -d : d) * e; ny = (m_at == AT::Next ? d : -d) * (1 - e); oo = 1 - e; no = e; }
            m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(os, os, c) * D2D1::Matrix3x2F::Translation(0, oy));
            DrawView(m_prev.v, m_prev.y, m_prev.m, oo, os, oy);
            m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(ns, ns, c) * D2D1::Matrix3x2F::Translation(0, ny));
            DrawView(m_next.v, m_next.y, m_next.m, no, ns, ny);
            m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        }

        void DrawView(CalendarViewMode v, int y, int m, float o, float s, float oy) {
            D2D1_LAYER_PARAMETERS p = D2D1::LayerParameters(); p.opacity = o; p.contentBounds = D2D1::InfiniteRect();
            ComPtr<ID2D1Layer> l; m_pRenderTarget->CreateLayer(&l); m_pRenderTarget->PushLayer(p, l.Get());
            wchar_t buf[64];
            if (v == CalendarViewMode::Day) {
                swprintf_s(buf, L"%d年 %d月", y, m); m_pRenderTarget->DrawText(buf, wcslen(buf), m_pHeaderFormat.Get(), m_headerRect, m_pTextBrush.Get());
                const wchar_t* wd[] = { L"日",L"月",L"火",L"水",L"木",L"金",L"土" };
                float cw = (m_contentRect.right - m_contentRect.left) / 7;
                float weekH = 24.0f;
                for (int i = 0; i < 7; ++i) m_pRenderTarget->DrawText(wd[i], 1, m_pContentFormat.Get(), D2D1::RectF(m_contentRect.left + i * cw, m_contentRect.top, m_contentRect.left + (i + 1) * cw, m_contentRect.top + weekH), m_pTextDimBrush.Get());
                float gt = m_contentRect.top + weekH; float ch = (m_contentRect.bottom - gt) / 6;
                int days = GetDaysInMonth(y, m), f = GetDayOfWeek(y, m, 1), idx = 0;
                for (int r = 0; r < 6; ++r) for (int c = 0; c < 7; ++c) {
                    int d = idx - f + 1; idx++;
                    if (d > 0 && d <= days) DrawCell(c, r, cw, ch, gt, std::to_wstring(d).c_str(), (d == m_selectedDay && m == m_selectedMonth && y == m_selectedYear), idx - 1 == m_hoveredIndex && !m_isAnimating, y, m);
                }
            }
            else if (v == CalendarViewMode::Month) {
                swprintf_s(buf, L"%d年", y); m_pRenderTarget->DrawText(buf, wcslen(buf), m_pHeaderFormat.Get(), m_headerRect, m_pTextBrush.Get());
                float cw = (m_contentRect.right - m_contentRect.left) / 4, ch = (m_contentRect.bottom - m_contentRect.top) / 3;
                for (int i = 0; i < 12; ++i) DrawCell(i % 4, i / 4, cw, ch, m_contentRect.top, (std::to_wstring(i + 1) + L"月").c_str(), (y == m_selectedYear && i + 1 == m_selectedMonth), i == m_hoveredIndex && !m_isAnimating, y, i + 1);
            }
            else {
                int sy = y - (y % 16); swprintf_s(buf, L"%d - %d", sy, sy + 15); m_pRenderTarget->DrawText(buf, wcslen(buf), m_pHeaderFormat.Get(), m_headerRect, m_pTextBrush.Get());
                float cw = (m_contentRect.right - m_contentRect.left) / 4, ch = (m_contentRect.bottom - m_contentRect.top) / 4;
                for (int i = 0; i < 16; ++i) DrawCell(i % 4, i / 4, cw, ch, m_contentRect.top, std::to_wstring(sy + i).c_str(), (sy + i == m_selectedYear), i == m_hoveredIndex && !m_isAnimating, sy + i, 0);
            }
            m_pRenderTarget->PopLayer();
        }

        void DrawCell(int c, int r, float w, float h, float t, const wchar_t* txt, bool sel, bool hov, int year, int month) {
            D2D1_RECT_F rc = D2D1::RectF(m_contentRect.left + c * w, t + r * h, m_contentRect.left + (c + 1) * w, t + (r + 1) * h);
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2), 4, 4);
            int val = _wtoi(txt);
            bool isMarked = false;

            if (m_currentView == CalendarViewMode::Day) {
                if (val > 0 && month > 0) {
                    long long key = (long long)year * 100 + month;
                    if (m_markedData.count(key) && m_markedData[key].count(val)) isMarked = true;
                }
            }
            else if (m_currentView == CalendarViewMode::Month) {
                if (month > 0 && m_markedMonthsData.count(year) && m_markedMonthsData[year].count(month)) isMarked = true;
            }
            else if (m_currentView == CalendarViewMode::Year) {
                if (year > 0 && m_recordedYearsData.count(year)) isMarked = true;
            }

            if (sel) {
                m_pRenderTarget->FillRoundedRectangle(rr, m_pAccentBrush.Get());
                m_pRenderTarget->DrawText(txt, wcslen(txt), m_pContentFormat.Get(), rc, m_pWhiteBrush.Get());
            }
            else {
                if (hov) m_pRenderTarget->FillRoundedRectangle(rr, m_pHoverBrush.Get());
                else if (isMarked) m_pRenderTarget->FillRoundedRectangle(rr, m_pMarkedBrush.Get());
                m_pRenderTarget->DrawText(txt, wcslen(txt), m_pContentFormat.Get(), rc, m_pTextBrush.Get());
            }
        }

        void StartViewTransition(CalendarViewMode n) { if (m_isAnimating) return; m_prev = { m_currentView, m_currentYear, m_currentMonth }; m_next = { n, m_currentYear, m_currentMonth }; m_at = (n > m_currentView) ? AT::ZoomOut : AT::ZoomIn; m_currentView = n; StartAnim(); }
        void StartScrollTransition(int d) { if (m_isAnimating) return; m_prev = { m_currentView, m_currentYear, m_currentMonth }; int nextYear = m_currentYear; int nextMonth = m_currentMonth; CalendarViewMode v = m_currentView; if (v == CalendarViewMode::Day) { nextMonth += d; while (nextMonth > 12) { nextMonth -= 12; nextYear++; } while (nextMonth < 1) { nextMonth += 12; nextYear--; } } else if (v == CalendarViewMode::Month) nextYear += d; else nextYear += (d * 16); if (v == CalendarViewMode::Day && m_onViewChangedCallback) m_onViewChangedCallback(nextYear, nextMonth); m_currentYear = nextYear; m_currentMonth = nextMonth; m_next = { m_currentView, m_currentYear, m_currentMonth }; m_at = (d > 0) ? AT::Next : AT::Prev; StartAnim(); }
        void StartAnim() { m_isAnimating = true; m_animStartTime = GetTickCount(); SetTimer(m_hwnd, 1, 16, NULL); }
        int GetDaysInMonth(int y, int m) { if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 29 : 28; static const int d[] = { 0,31,0,31,30,31,30,31,31,30,31,30,31 }; return d[m]; }
        int GetDayOfWeek(int y, int m, int d) { if (m < 3) { y--; m += 12; } return (y + y / 4 - y / 100 + y / 400 + (13 * m + 8) / 5 + d) % 7; }
    };
}

// 定数・構造体・カラーパレット
enum ReportMode { MODE_MONTHLY, MODE_YEARLY };
enum GraphType { GRAPH_PIE, GRAPH_LINE };
enum TransType { TYPE_EXPENSE = 0, TYPE_INCOME = 1 };
const int TIMER_ANIM_ID = 2;

namespace ColorPalette {
    const D2D1_COLOR_F Background = { 0.96f, 0.96f, 0.97f, 1.0f };
    const D2D1_COLOR_F Sidebar = { 1.0f, 1.0f, 1.0f, 1.0f };
    const D2D1_COLOR_F Card = { 1.0f, 1.0f, 1.0f, 1.0f };
    const D2D1_COLOR_F TextPrimary = { 0.11f, 0.11f, 0.12f, 1.0f };
    const D2D1_COLOR_F TextSecondary = { 0.55f, 0.55f, 0.57f, 1.0f };
    const D2D1_COLOR_F Separator = { 0.90f, 0.90f, 0.92f, 1.0f };
    const D2D1_COLOR_F Graph[] = {
        {0.35f, 0.78f, 0.98f, 1.0f}, {1.0f, 0.58f, 0.63f, 1.0f}, {0.49f, 0.83f, 0.58f, 1.0f}, {1.0f, 0.84f, 0.38f, 1.0f},
        {0.73f, 0.62f, 0.91f, 1.0f}, {1.0f, 0.71f, 0.51f, 1.0f}, {0.50f, 0.50f, 0.50f, 1.0f}, {0.25f, 0.25f, 0.25f, 1.0f}
    };
}

struct TransactionSummary { std::wstring label; float amount; D2D1_COLOR_F color; bool hasChildren; };
struct TimeSeriesData { int timeUnit; float income; float expense; };
struct CategoryItem { int id; std::wstring name; int sortOrder; int parentId; };
struct TransactionDetail { int id; std::wstring date; std::wstring category; float amount; int type; };

const float PI = 3.14159265f;
const int ID_BTN_SETTINGS = 300;
const int ID_LIST_CATS = 301;
const int ID_BTN_UP = 302;
const int ID_BTN_DOWN = 303;
const int ID_EDIT_NEW_CAT = 304;
const int ID_COMBO_PARENT = 305;
const int ID_BTN_ADD_CAT = 306;
const int ID_BTN_UPDATE_CAT = 307;
const int ID_BTN_DELETE_CAT = 308;
const int WM_APP_CAL_CHANGE = WM_APP + 200;
const int WM_APP_CAL_VIEW_CHANGE = WM_APP + 201;

// -----------------------------------------------------------------------------
// ドラッグ＆ドロップ用ヘルパー
// -----------------------------------------------------------------------------
class DragImageWindow {
    HWND hWindow;
public:
    DragImageWindow() : hWindow(NULL) {}
    void Show(HWND hList, int index, POINT ptCursor) {
        if (hWindow) Destroy();
        RECT rc; SendMessage(hList, LB_GETITEMRECT, index, (LPARAM)&rc);
        int width = rc.right - rc.left; int height = rc.bottom - rc.top;
        int len = (int)SendMessage(hList, LB_GETTEXTLEN, index, 0); std::vector<wchar_t> buf(len + 1); SendMessage(hList, LB_GETTEXT, index, (LPARAM)buf.data());
        WNDCLASS wc = { 0 }; wc.lpfnWndProc = DefWindowProc; wc.hInstance = GetModuleHandle(NULL); wc.lpszClassName = L"DragImageWnd"; wc.hCursor = LoadCursor(NULL, IDC_ARROW); RegisterClass(&wc);
        hWindow = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"DragImageWnd", NULL, WS_POPUP, ptCursor.x + 10, ptCursor.y + 10, width, height, NULL, NULL, wc.hInstance, NULL);
        HDC hdcScreen = GetDC(NULL); HDC hdcMem = CreateCompatibleDC(hdcScreen); HBITMAP hBm = CreateCompatibleBitmap(hdcScreen, width, height); HBITMAP hOldBm = (HBITMAP)SelectObject(hdcMem, hBm);
        RECT bgRc = { 0, 0, width, height }; HBRUSH hBr = CreateSolidBrush(RGB(245, 245, 250)); FillRect(hdcMem, &bgRc, hBr); DeleteObject(hBr);
        SetBkMode(hdcMem, TRANSPARENT); HFONT hFont = (HFONT)SendMessage(hList, WM_GETFONT, 0, 0); SelectObject(hdcMem, hFont); SetTextColor(hdcMem, RGB(0, 0, 0));
        RECT textRc = { 0, 0, width, height }; OffsetRect(&textRc, 5, 0); DrawText(hdcMem, buf.data(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        HBRUSH frameBr = CreateSolidBrush(RGB(0, 122, 255)); FrameRect(hdcMem, &textRc, frameBr); DeleteObject(frameBr);
        POINT ptSrc = { 0, 0 }; SIZE wndSize = { width, height }; BLENDFUNCTION blend = { AC_SRC_OVER, 0, 200, 0 };
        UpdateLayeredWindow(hWindow, hdcScreen, NULL, &wndSize, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
        SelectObject(hdcMem, hOldBm); DeleteObject(hBm); DeleteDC(hdcMem); ReleaseDC(NULL, hdcScreen); ShowWindow(hWindow, SW_SHOWNA);
    }
    void Move(POINT ptCursor) { if (hWindow) SetWindowPos(hWindow, NULL, ptCursor.x + 15, ptCursor.y + 15, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); }
    void Destroy() { if (hWindow) { DestroyWindow(hWindow); hWindow = NULL; } }
};

WNDPROC g_OldListBoxProc = NULL; WNDPROC g_OldEditProc = NULL;
int g_DragSourceIdx = -1; int g_LastInsertIdx = -1; bool g_IsDragging = false; POINT g_DragStartPt = { 0 }; DragImageWindow g_DragImg;

void DrawInsertLine(HWND hwnd, int idx) {
    HDC hdc = GetDC(hwnd); RECT rc; int count = (int)SendMessage(hwnd, LB_GETCOUNT, 0, 0);
    if (idx >= 0 && idx < count) SendMessage(hwnd, LB_GETITEMRECT, idx, (LPARAM)&rc); else if (idx == count) { SendMessage(hwnd, LB_GETITEMRECT, count - 1, (LPARAM)&rc); rc.top = rc.bottom; rc.bottom += 2; }
    else { ReleaseDC(hwnd, hdc); return; }
    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 122, 255)); HPEN hOldPen = (HPEN)SelectObject(hdc, hPen); MoveToEx(hdc, rc.left, rc.top, NULL); LineTo(hdc, rc.right, rc.top);
    SelectObject(hdc, hOldPen); DeleteObject(hPen); ReleaseDC(hwnd, hdc);
}

LRESULT CALLBACK ListBoxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        DWORD res = (DWORD)SendMessage(hwnd, LB_ITEMFROMPOINT, 0, lp);
        // HIWORD(res) == 0 : アイテム上
        // HIWORD(res) == 1 : アイテム外(空白)
        if (HIWORD(res) == 0) {
            int idx = LOWORD(res);
            if (idx != -1) {
                g_DragSourceIdx = idx;
                g_DragStartPt.x = LOWORD(lp);
                g_DragStartPt.y = HIWORD(lp);
                g_IsDragging = false;
                g_LastInsertIdx = -1;
            }
        }
        else {
            // ★修正: 空白クリック時に選択を解除し、親へ通知する
            SendMessage(hwnd, LB_SETCURSEL, (WPARAM)-1, 0);
            SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), LBN_SELCHANGE), (LPARAM)hwnd);
        }
        break;
    }
    case WM_MOUSEMOVE: {
        if (g_DragSourceIdx != -1 && (wp & MK_LBUTTON)) {
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
            if (!g_IsDragging) { if (abs(pt.x - g_DragStartPt.x) > GetSystemMetrics(SM_CXDRAG) || abs(pt.y - g_DragStartPt.y) > GetSystemMetrics(SM_CYDRAG)) { g_IsDragging = true; SetCapture(hwnd); POINT ptScreen; GetCursorPos(&ptScreen); g_DragImg.Show(hwnd, g_DragSourceIdx, ptScreen); } }
            else { POINT ptScreen; GetCursorPos(&ptScreen); g_DragImg.Move(ptScreen); DWORD res = (DWORD)SendMessage(hwnd, LB_ITEMFROMPOINT, 0, lp); int targetIdx = LOWORD(res); if (HIWORD(res) == 1) { int count = (int)SendMessage(hwnd, LB_GETCOUNT, 0, 0); targetIdx = count; } if (g_LastInsertIdx != targetIdx) { g_LastInsertIdx = targetIdx; InvalidateRect(hwnd, NULL, FALSE); UpdateWindow(hwnd); } if (g_LastInsertIdx != -1) DrawInsertLine(hwnd, g_LastInsertIdx); }
        } break;
    }
    case WM_LBUTTONUP: {
        if (g_IsDragging) { ReleaseCapture(); g_DragImg.Destroy(); if (g_LastInsertIdx != -1 && g_LastInsertIdx != g_DragSourceIdx && g_LastInsertIdx != g_DragSourceIdx + 1) SendMessage(GetParent(hwnd), WM_APP + 1, (WPARAM)g_DragSourceIdx, (LPARAM)g_LastInsertIdx); InvalidateRect(hwnd, NULL, TRUE); }
        g_IsDragging = false; g_DragSourceIdx = -1; g_LastInsertIdx = -1; break;
    }
    case WM_PAINT: { LRESULT lRes = CallWindowProc(g_OldListBoxProc, hwnd, msg, wp, lp); if (g_IsDragging && g_LastInsertIdx != -1) DrawInsertLine(hwnd, g_LastInsertIdx); return lRes; }
    } return CallWindowProc(g_OldListBoxProc, hwnd, msg, wp, lp);
}

// -----------------------------------------------------------------------------
// ExpenseManager (Model)
// -----------------------------------------------------------------------------
class ExpenseManager {
    sqlite3* db; const char* DB_FILE = "kakeibo_v7.db";
public:
    ExpenseManager() : db(nullptr) {} ~ExpenseManager() { if (db) sqlite3_close(db); }
    void Initialize() {
        if (sqlite3_open(DB_FILE, &db) == SQLITE_OK) {
            sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS transactions (id INTEGER PRIMARY KEY, date TEXT, category TEXT, amount REAL, type INTEGER);", NULL, NULL, NULL);
            sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS categories (id INTEGER PRIMARY KEY, name TEXT, type INTEGER, sort_order INTEGER, parent_id INTEGER DEFAULT 0);", NULL, NULL, NULL);
            // 固定費テーブル
            sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS fixed_costs (id INTEGER PRIMARY KEY, day INTEGER, category TEXT, amount REAL, type INTEGER, name TEXT);", NULL, NULL, NULL);
            InitDefaultCategories();
        }
    }
    void InitDefaultCategories() {
        // --- 支出カテゴリの初期化 ---
        if (GetCategoryCount(TYPE_EXPENSE) == 0) {
            // 食費
            int idFood = AddCategory(L"食費", TYPE_EXPENSE, 0);
            AddCategory(L"自炊", TYPE_EXPENSE, idFood);
            AddCategory(L"外食", TYPE_EXPENSE, idFood);

            // 住居費
            int idHouse = AddCategory(L"住居費", TYPE_EXPENSE, 0);
            AddCategory(L"家賃", TYPE_EXPENSE, idHouse);
            AddCategory(L"住宅設備", TYPE_EXPENSE, idHouse);

            // 水道光熱費
            int idUtil = AddCategory(L"水道光熱費", TYPE_EXPENSE, 0);
            AddCategory(L"電気代", TYPE_EXPENSE, idUtil);
            AddCategory(L"水道代", TYPE_EXPENSE, idUtil);
            AddCategory(L"ガス代", TYPE_EXPENSE, idUtil);

            // 通信費
            int idComm = AddCategory(L"通信費", TYPE_EXPENSE, 0);
            AddCategory(L"インターネット", TYPE_EXPENSE, idComm);
            AddCategory(L"スマホ・電話", TYPE_EXPENSE, idComm);

            // 生活・その他
            AddCategory(L"日用品", TYPE_EXPENSE, 0);
            AddCategory(L"交通費", TYPE_EXPENSE, 0);
            AddCategory(L"サブスク", TYPE_EXPENSE, 0);
            AddCategory(L"医療費", TYPE_EXPENSE, 0);
            AddCategory(L"娯楽・趣味", TYPE_EXPENSE, 0);
        }

        // --- 収入カテゴリの初期化 ---
        if (GetCategoryCount(TYPE_INCOME) == 0) {
            AddCategory(L"給与", TYPE_INCOME, 0);
            AddCategory(L"ボーナス", TYPE_INCOME, 0);
            AddCategory(L"株式配当", TYPE_INCOME, 0);
            AddCategory(L"臨時収入", TYPE_INCOME, 0);
        }
    }
    int GetCategoryCount(int type) { sqlite3_stmt* stmt; int count = 0; if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM categories WHERE type=?", -1, &stmt, NULL) == SQLITE_OK) { sqlite3_bind_int(stmt, 1, type); if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0); } sqlite3_finalize(stmt); return count; }
    int AddCategory(const std::wstring& name, int type, int parentId = 0) {
        sqlite3_stmt* check; char nameUtf8[256]; WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nameUtf8, 256, NULL, NULL);
        if (sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE name=? AND type=?", -1, &check, NULL) == SQLITE_OK) { sqlite3_bind_text(check, 1, nameUtf8, -1, SQLITE_STATIC); sqlite3_bind_int(check, 2, type); if (sqlite3_step(check) == SQLITE_ROW) { int id = sqlite3_column_int(check, 0); sqlite3_finalize(check); return id; } } sqlite3_finalize(check);
        int maxOrder = 0; sqlite3_stmt* ord; if (sqlite3_prepare_v2(db, "SELECT MAX(sort_order) FROM categories WHERE type=?", -1, &ord, NULL) == SQLITE_OK) { sqlite3_bind_int(ord, 1, type); if (sqlite3_step(ord) == SQLITE_ROW) maxOrder = sqlite3_column_int(ord, 0); } sqlite3_finalize(ord);
        sqlite3_stmt* ins; if (sqlite3_prepare_v2(db, "INSERT INTO categories (name, type, sort_order, parent_id) VALUES (?, ?, ?, ?)", -1, &ins, NULL) == SQLITE_OK) { sqlite3_bind_text(ins, 1, nameUtf8, -1, SQLITE_STATIC); sqlite3_bind_int(ins, 2, type); sqlite3_bind_int(ins, 3, maxOrder + 1); sqlite3_bind_int(ins, 4, parentId); sqlite3_step(ins); } sqlite3_finalize(ins); return (int)sqlite3_last_insert_rowid(db);
    }
    void UpdateCategory(int id, const std::wstring& newName, int newParentId) {
        std::wstring oldName = L""; sqlite3_stmt* q;
        if (sqlite3_prepare_v2(db, "SELECT name FROM categories WHERE id=?", -1, &q, NULL) == SQLITE_OK) { sqlite3_bind_int(q, 1, id); if (sqlite3_step(q) == SQLITE_ROW) { wchar_t buf[256]; MultiByteToWideChar(CP_UTF8, 0, (const char*)sqlite3_column_text(q, 0), -1, buf, 256); oldName = buf; } } sqlite3_finalize(q); if (oldName.empty()) return;
        char nameUtf8[256]; WideCharToMultiByte(CP_UTF8, 0, newName.c_str(), -1, nameUtf8, 256, NULL, NULL); char oldNameUtf8[256]; WideCharToMultiByte(CP_UTF8, 0, oldName.c_str(), -1, oldNameUtf8, 256, NULL, NULL);
        sqlite3_stmt* upd; if (sqlite3_prepare_v2(db, "UPDATE categories SET name=?, parent_id=? WHERE id=?", -1, &upd, NULL) == SQLITE_OK) { sqlite3_bind_text(upd, 1, nameUtf8, -1, SQLITE_STATIC); sqlite3_bind_int(upd, 2, newParentId); sqlite3_bind_int(upd, 3, id); sqlite3_step(upd); } sqlite3_finalize(upd);
        if (newName != oldName) { sqlite3_stmt* trUpd; if (sqlite3_prepare_v2(db, "UPDATE transactions SET category=? WHERE category=?", -1, &trUpd, NULL) == SQLITE_OK) { sqlite3_bind_text(trUpd, 1, nameUtf8, -1, SQLITE_STATIC); sqlite3_bind_text(trUpd, 2, oldNameUtf8, -1, SQLITE_STATIC); sqlite3_step(trUpd); } sqlite3_finalize(trUpd); }
    }
    void DeleteCategory(int id) { sqlite3_stmt* upd; if (sqlite3_prepare_v2(db, "UPDATE categories SET parent_id=0 WHERE parent_id=?", -1, &upd, NULL) == SQLITE_OK) { sqlite3_bind_int(upd, 1, id); sqlite3_step(upd); } sqlite3_finalize(upd); sqlite3_stmt* del; if (sqlite3_prepare_v2(db, "DELETE FROM categories WHERE id=?", -1, &del, NULL) == SQLITE_OK) { sqlite3_bind_int(del, 1, id); sqlite3_step(del); } sqlite3_finalize(del); }
    std::vector<CategoryItem> GetCategories(int type) {
        std::vector<CategoryItem> all; sqlite3_stmt* stmt; if (sqlite3_prepare_v2(db, "SELECT id, name, sort_order, parent_id FROM categories WHERE type=? ORDER BY sort_order ASC", -1, &stmt, NULL) == SQLITE_OK) { sqlite3_bind_int(stmt, 1, type); while (sqlite3_step(stmt) == SQLITE_ROW) { int id = sqlite3_column_int(stmt, 0); wchar_t name[256]; MultiByteToWideChar(CP_UTF8, 0, (const char*)sqlite3_column_text(stmt, 1), -1, name, 256); all.push_back({ id, name, sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3) }); } } sqlite3_finalize(stmt);
        std::vector<CategoryItem> sorted; for (const auto& p : all) { if (p.parentId == 0) { sorted.push_back(p); for (const auto& c : all) { if (c.parentId == p.id) sorted.push_back(c); } } } for (const auto& c : all) { bool found = false; for (const auto& s : sorted) if (s.id == c.id) found = true; if (!found) sorted.push_back(c); } return sorted;
    }
    void SwapCategoryOrder(int id1, int order1, int id2, int order2) { char sql[256]; sprintf_s(sql, "UPDATE categories SET sort_order=%d WHERE id=%d;", order2, id1); sqlite3_exec(db, sql, NULL, NULL, NULL); sprintf_s(sql, "UPDATE categories SET sort_order=%d WHERE id=%d;", order1, id2); sqlite3_exec(db, sql, NULL, NULL, NULL); }
    void AddTransaction(const std::wstring& date, const std::wstring& category, float amount, int type) { char catUtf8[256], dateUtf8[32]; WideCharToMultiByte(CP_UTF8, 0, category.c_str(), -1, catUtf8, 256, NULL, NULL); WideCharToMultiByte(CP_UTF8, 0, date.c_str(), -1, dateUtf8, 32, NULL, NULL); sqlite3_stmt* stmt; sqlite3_prepare_v2(db, "INSERT INTO transactions (date, category, amount, type) VALUES (?, ?, ?, ?);", -1, &stmt, NULL); sqlite3_bind_text(stmt, 1, dateUtf8, -1, SQLITE_STATIC); sqlite3_bind_text(stmt, 2, catUtf8, -1, SQLITE_STATIC); sqlite3_bind_double(stmt, 3, amount); sqlite3_bind_int(stmt, 4, type); sqlite3_step(stmt); sqlite3_finalize(stmt); AddCategory(category, type, 0); }
    std::vector<TransactionDetail> GetTransactionsForDate(const std::wstring& date) {
        std::vector<TransactionDetail> result; char dateUtf8[32]; WideCharToMultiByte(CP_UTF8, 0, date.c_str(), -1, dateUtf8, 32, NULL, NULL); sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT id, date, category, amount, type FROM transactions WHERE date = ? ORDER BY id ASC", -1, &stmt, NULL) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, dateUtf8, -1, SQLITE_STATIC); while (sqlite3_step(stmt) == SQLITE_ROW) { int id = sqlite3_column_int(stmt, 0); wchar_t wDate[32], wCat[256]; MultiByteToWideChar(CP_UTF8, 0, (const char*)sqlite3_column_text(stmt, 1), -1, wDate, 32); MultiByteToWideChar(CP_UTF8, 0, (const char*)sqlite3_column_text(stmt, 2), -1, wCat, 256); result.push_back({ id, wDate, wCat, (float)sqlite3_column_double(stmt, 3), sqlite3_column_int(stmt, 4) }); } } sqlite3_finalize(stmt); return result;
    }
    void DeleteTransaction(int id) { sqlite3_stmt* stmt; if (sqlite3_prepare_v2(db, "DELETE FROM transactions WHERE id = ?", -1, &stmt, NULL) == SQLITE_OK) { sqlite3_bind_int(stmt, 1, id); sqlite3_step(stmt); } sqlite3_finalize(stmt); }
    void UpdateTransaction(int id, const std::wstring& category, float amount, int type) { char catUtf8[256]; WideCharToMultiByte(CP_UTF8, 0, category.c_str(), -1, catUtf8, 256, NULL, NULL); sqlite3_stmt* stmt; if (sqlite3_prepare_v2(db, "UPDATE transactions SET category=?, amount=?, type=? WHERE id=?", -1, &stmt, NULL) == SQLITE_OK) { sqlite3_bind_text(stmt, 1, catUtf8, -1, SQLITE_STATIC); sqlite3_bind_double(stmt, 2, amount); sqlite3_bind_int(stmt, 3, type); sqlite3_bind_int(stmt, 4, id); sqlite3_step(stmt); } sqlite3_finalize(stmt); }
    std::vector<TransactionSummary> GetPieData(const std::wstring& startDate, const std::wstring& endDate, int type, const std::wstring& parentCategoryName) {
        std::vector<TransactionSummary> result; char sDate[32], eDate[32]; WideCharToMultiByte(CP_UTF8, 0, startDate.c_str(), -1, sDate, 32, NULL, NULL); WideCharToMultiByte(CP_UTF8, 0, endDate.c_str(), -1, eDate, 32, NULL, NULL);
        std::map<std::wstring, std::wstring> childToParent; std::map<std::wstring, bool> isParent; auto cats = GetCategories(type); for (const auto& c : cats) { if (c.parentId != 0) { auto it = std::find_if(cats.begin(), cats.end(), [&](const CategoryItem& p) { return p.id == c.parentId; }); if (it != cats.end()) { childToParent[c.name] = it->name; isParent[it->name] = true; } } }
        std::map<std::wstring, float> sums; sqlite3_stmt* stmt; if (sqlite3_prepare_v2(db, "SELECT category, amount FROM transactions WHERE type=? AND date >= ? AND date <= ?", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, type);
            sqlite3_bind_text(stmt, 2, sDate, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, eDate, -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) { wchar_t wCat[256]; MultiByteToWideChar(CP_UTF8, 0, (const char*)sqlite3_column_text(stmt, 0), -1, wCat, 256); float amt = (float)sqlite3_column_double(stmt, 1); std::wstring catName = wCat; std::wstring parent = childToParent.count(catName) ? childToParent[catName] : L""; if (parentCategoryName.empty()) { if (!parent.empty()) sums[parent] += amt; else sums[catName] += amt; } else { if (parent == parentCategoryName) sums[catName] += amt; } }
        } sqlite3_finalize(stmt);
        int idx = 0; for (auto const& [name, amount] : sums) { bool children = parentCategoryName.empty() && isParent[name]; result.push_back({ name, amount, ColorPalette::Graph[idx % 8], children }); idx++; } std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.amount > b.amount; }); return result;
    }
    std::vector<TimeSeriesData> GetLineData(const std::wstring& startDate, const std::wstring& endDate, ReportMode mode) {
        std::vector<TimeSeriesData> result; char sDate[32], eDate[32]; WideCharToMultiByte(CP_UTF8, 0, startDate.c_str(), -1, sDate, 32, NULL, NULL); WideCharToMultiByte(CP_UTF8, 0, endDate.c_str(), -1, eDate, 32, NULL, NULL); std::string groupBy = (mode == MODE_MONTHLY) ? "SUBSTR(date, 9, 2)" : "SUBSTR(date, 6, 2)"; std::string sql = "SELECT " + groupBy + ", type, SUM(amount) FROM transactions WHERE date >= ? AND date <= ? GROUP BY " + groupBy + ", type ORDER BY " + groupBy + ";";
        sqlite3_stmt* stmt; std::map<int, std::pair<float, float>> aggregation; if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sDate, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, eDate, -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) { int timeVal = atoi((const char*)sqlite3_column_text(stmt, 0)); int type = sqlite3_column_int(stmt, 1); float amt = (float)sqlite3_column_double(stmt, 2); if (type == TYPE_INCOME) aggregation[timeVal].first += amt; else aggregation[timeVal].second += amt; }
        } sqlite3_finalize(stmt); for (auto const& [time, val] : aggregation) result.push_back({ time, val.first, val.second }); return result;
    }

    std::vector<int> GetRecordedDays(int year, int month) {
        std::vector<int> days; char datePattern[32]; sprintf_s(datePattern, "%04d-%02d-%%", year, month);
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT DISTINCT CAST(strftime('%d', date) AS INTEGER) FROM transactions WHERE date LIKE ?", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, datePattern, -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) { days.push_back(sqlite3_column_int(stmt, 0)); }
        } sqlite3_finalize(stmt); return days;
    }

    // --- 追加: 指定した年にデータが存在する月(1-12)を取得 ---
    std::set<int> GetRecordedMonths(int year) {
        std::set<int> months; char yearStr[16]; sprintf_s(yearStr, "%04d", year); sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT DISTINCT CAST(strftime('%m', date) AS INTEGER) FROM transactions WHERE strftime('%Y', date) = ?", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, yearStr, -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) months.insert(sqlite3_column_int(stmt, 0));
        } sqlite3_finalize(stmt); return months;
    }

    // --- 追加: データが存在する年を取得 ---
    std::set<int> GetRecordedYears() {
        std::set<int> years; sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT DISTINCT CAST(strftime('%Y', date) AS INTEGER) FROM transactions", -1, &stmt, NULL) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) years.insert(sqlite3_column_int(stmt, 0));
        } sqlite3_finalize(stmt); return years;
    }

    // 固定費関連
    void AddFixedCost(int day, const std::wstring& category, float amount, int type, const std::wstring& name) {
        char catUtf8[256], nameUtf8[256];
        WideCharToMultiByte(CP_UTF8, 0, category.c_str(), -1, catUtf8, 256, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nameUtf8, 256, NULL, NULL);
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "INSERT INTO fixed_costs (day, category, amount, type, name) VALUES (?, ?, ?, ?, ?)", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, day); sqlite3_bind_text(stmt, 2, catUtf8, -1, SQLITE_STATIC); sqlite3_bind_double(stmt, 3, amount); sqlite3_bind_int(stmt, 4, type); sqlite3_bind_text(stmt, 5, nameUtf8, -1, SQLITE_STATIC); sqlite3_step(stmt);
        } sqlite3_finalize(stmt);
    }
    struct FixedCostItem { int id; int day; std::wstring category; float amount; int type; std::wstring name; };
    std::vector<FixedCostItem> GetFixedCosts(int type) {
        std::vector<FixedCostItem> list; sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT id, day, category, amount, type, name FROM fixed_costs WHERE type=? ORDER BY day ASC", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, type);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                wchar_t cat[256], name[256]; MultiByteToWideChar(CP_UTF8, 0, (const char*)sqlite3_column_text(stmt, 2), -1, cat, 256);
                const char* n = (const char*)sqlite3_column_text(stmt, 5); if (n) MultiByteToWideChar(CP_UTF8, 0, n, -1, name, 256); else wcscpy_s(name, L"");
                list.push_back({ sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1), cat, (float)sqlite3_column_double(stmt, 3), sqlite3_column_int(stmt, 4), name });
            }
        } sqlite3_finalize(stmt); return list;
    }
    void DeleteFixedCost(int id) { sqlite3_stmt* stmt; if (sqlite3_prepare_v2(db, "DELETE FROM fixed_costs WHERE id=?", -1, &stmt, NULL) == SQLITE_OK) { sqlite3_bind_int(stmt, 1, id); sqlite3_step(stmt); } sqlite3_finalize(stmt); }
    void CheckAndApplyFixedCosts() {
        time_t t = time(0); struct tm now; localtime_s(&now, &t);
        int currentYear = now.tm_year + 1900; int currentMonth = now.tm_mon + 1; int currentDay = now.tm_mday;
        std::vector<FixedCostItem> allCosts;
        for (int type : {TYPE_EXPENSE, TYPE_INCOME}) { auto costs = GetFixedCosts(type); allCosts.insert(allCosts.end(), costs.begin(), costs.end()); }
        sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
        for (const auto& fc : allCosts) {
            if (currentDay >= fc.day) {
                char dateTarget[32]; sprintf_s(dateTarget, "%04d-%02d-%02d", currentYear, currentMonth, fc.day);
                char catUtf8[256]; WideCharToMultiByte(CP_UTF8, 0, fc.category.c_str(), -1, catUtf8, 256, NULL, NULL);
                sqlite3_stmt* check; bool exists = false;
                if (sqlite3_prepare_v2(db, "SELECT id FROM transactions WHERE date=? AND category=? AND amount=? AND type=?", -1, &check, NULL) == SQLITE_OK) {
                    sqlite3_bind_text(check, 1, dateTarget, -1, SQLITE_STATIC); sqlite3_bind_text(check, 2, catUtf8, -1, SQLITE_STATIC); sqlite3_bind_double(check, 3, fc.amount); sqlite3_bind_int(check, 4, fc.type);
                    if (sqlite3_step(check) == SQLITE_ROW) exists = true;
                } sqlite3_finalize(check);
                if (!exists) { wchar_t wDate[32]; MultiByteToWideChar(CP_UTF8, 0, dateTarget, -1, wDate, 32); AddTransaction(wDate, fc.category, fc.amount, fc.type); }
            }
        } sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    }
};

// -----------------------------------------------------------------------------
// ChartCanvas (View)
// -----------------------------------------------------------------------------
class ChartCanvas {
    ID2D1Factory* pFactory; ID2D1HwndRenderTarget* pRT; ID2D1SolidColorBrush* pBrush; IDWriteFactory* pDWFactory; IDWriteTextFormat* pTxtTitle; IDWriteTextFormat* pTxtNormal; IDWriteTextFormat* pTxtSmall; IDWriteTextFormat* pTxtLegend;
    D2D1_POINT_2F m_mousePos; wchar_t m_decimalSep[4]; wchar_t m_thousandSep[4]; std::wstring m_currentDrillParent; std::wstring m_lastHoveredName;
    bool m_isAnimating; DWORD m_animStartTime; float m_animProgress; bool m_lastHoveredHasChildren;
public:
    ChartCanvas() : pFactory(NULL), pRT(NULL), pBrush(NULL), pDWFactory(NULL), pTxtTitle(NULL), pTxtNormal(NULL), pTxtSmall(NULL), pTxtLegend(NULL), m_isAnimating(false), m_animProgress(1.0f), m_animStartTime(0), m_lastHoveredHasChildren(false) { m_mousePos = D2D1::Point2F(-1, -1); GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, m_decimalSep, 4); GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, m_thousandSep, 4); }
    ~ChartCanvas() { if (pRT) pRT->Release(); if (pBrush) pBrush->Release(); if (pFactory) pFactory->Release(); if (pTxtTitle) pTxtTitle->Release(); if (pTxtNormal) pTxtNormal->Release(); if (pTxtSmall) pTxtSmall->Release(); if (pTxtLegend) pTxtLegend->Release(); if (pDWFactory) pDWFactory->Release(); }

    void Initialize() {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pFactory);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&pDWFactory);
        const wchar_t* fontName = L"Segoe UI";
        pDWFactory->CreateTextFormat(fontName, NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 24.0f, L"ja-jp", &pTxtTitle);
        pDWFactory->CreateTextFormat(fontName, NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"ja-jp", &pTxtNormal);
        pDWFactory->CreateTextFormat(fontName, NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"ja-jp", &pTxtSmall);
        pDWFactory->CreateTextFormat(fontName, NULL, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"ja-jp", &pTxtLegend);
        pTxtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void Resize(HWND hwnd) { if (pRT) { RECT rc; GetClientRect(hwnd, &rc); pRT->Resize(D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)); } }
    void UpdateMousePos(int x, int y) { m_mousePos = D2D1::Point2F((float)x, (float)y); }
    std::wstring GetHoveredCategory() { return m_lastHoveredName; }
    void DrillDown(const std::wstring& parentName) { m_currentDrillParent = parentName; StartAnimation(); }
    void DrillUp() { m_currentDrillParent = L""; StartAnimation(); }
    bool IsDrillingDown() const { return !m_currentDrillParent.empty(); }
    bool IsHoveredDrillable() { return m_lastHoveredHasChildren; }
    std::wstring FormatMoney(float amount) { return ::FormatMoney(amount); }

    // ★修正: テキスト量に合わせてツールチップサイズを自動調整
    void DrawTooltip(const wchar_t* text, D2D1_SIZE_F size) {
        if (!pDWFactory || !pRT) return;

        IDWriteTextLayout* pLayout = NULL;
        // 最大幅を300pxとし、テキストレイアウトを作成してサイズを計測
        HRESULT hr = pDWFactory->CreateTextLayout(text, (UINT32)wcslen(text), pTxtNormal, 300.0f, 1000.0f, &pLayout);

        if (SUCCEEDED(hr)) {
            DWRITE_TEXT_METRICS metrics;
            pLayout->GetMetrics(&metrics);

            float padX = 10.0f;
            float padY = 8.0f;

            // テキストサイズ + パディングでボックスサイズを決定
            float tipW = metrics.width + (padX * 2);
            float tipH = metrics.height + (padY * 2);

            // 画面端の判定と位置補正
            float tipX = m_mousePos.x + 15;
            float tipY = m_mousePos.y - tipH - 5;
            if (tipY < 0) tipY = m_mousePos.y + 15;
            if (tipX + tipW > size.width) tipX = m_mousePos.x - tipW - 10;
            if (tipX < 0) tipX = 0; // 左端チェック

            D2D1_RECT_F tipRect = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);

            // 影
            pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.1f));
            pRT->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tipX + 2, tipY + 2, tipX + tipW + 2, tipY + tipH + 2), 6, 6), pBrush);

            // 背景
            pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f));
            pRT->FillRoundedRectangle(D2D1::RoundedRect(tipRect, 6, 6), pBrush);

            // 枠線
            pBrush->SetColor(ColorPalette::Separator);
            pRT->DrawRoundedRectangle(D2D1::RoundedRect(tipRect, 6, 6), pBrush, 1.0f);

            // テキスト描画 (DrawTextLayoutを使うことで計測通りの配置が可能)
            pBrush->SetColor(ColorPalette::TextPrimary);
            pRT->DrawTextLayout(D2D1::Point2F(tipX + padX, tipY + padY), pLayout, pBrush);

            pLayout->Release();
        }
    }

    void StartAnimation() {
        if (!m_isAnimating) { m_animStartTime = GetTickCount(); m_animProgress = 0.0f; } m_isAnimating = true;
    }
    bool UpdateAnimation() {
        if (!m_isAnimating) return false; DWORD now = GetTickCount(); float t = (now - m_animStartTime) / 600.0f;
        if (t >= 1.0f) { t = 1.0f; m_isAnimating = false; } m_animProgress = (float)(1.0 - pow(1.0f - t, 3)); return true;
    }

    void Render(HWND hwnd, ExpenseManager& db, const std::wstring& start, const std::wstring& end, std::wstring title, GraphType gType, ReportMode rMode, int currentType) {
        if (!pFactory) return; RECT rc; GetClientRect(hwnd, &rc); if (!pRT) { pFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)), &pRT); pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &pBrush); }
        pRT->BeginDraw(); pRT->Clear(ColorPalette::Background); D2D1_SIZE_F size = pRT->GetSize();
        m_lastHoveredName = L""; m_lastHoveredHasChildren = false;

        float sidebarWidth = 320.0f;

        pBrush->SetColor(ColorPalette::Sidebar); pRT->FillRectangle(D2D1::RectF(0, 0, sidebarWidth, size.height), pBrush); pBrush->SetColor(ColorPalette::Separator); pRT->DrawLine(D2D1::Point2F(sidebarWidth, 0), D2D1::Point2F(sidebarWidth, size.height), pBrush, 1.0f);
        float margin = 20.0f; float graphLeft = sidebarWidth + margin; float graphTop = margin; float graphRight = size.width - margin; float graphBottom = size.height - margin;
        D2D1_RECT_F graphRect = D2D1::RectF(graphLeft, graphTop, graphRight, graphBottom); pBrush->SetColor(ColorPalette::Card); pRT->FillRoundedRectangle(D2D1::RoundedRect(graphRect, 10, 10), pBrush); pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.05f)); pRT->DrawRoundedRectangle(D2D1::RoundedRect(graphRect, 10, 10), pBrush, 1.0f);
        float contentPadding = 30.0f;

        bool isDrillDown = !m_currentDrillParent.empty();
        if (isDrillDown) {
            title += L" (" + m_currentDrillParent + L")";
            pBrush->SetColor(ColorPalette::TextSecondary);
            pRT->DrawText(L"← 右クリックで戻る", 10, pTxtSmall, D2D1::RectF(graphLeft + contentPadding, graphTop + 60, graphRight, graphTop + 80), pBrush);
        }
        pBrush->SetColor(ColorPalette::TextPrimary); pRT->DrawText(title.c_str(), (UINT32)title.length(), pTxtTitle, D2D1::RectF(graphLeft + contentPadding, graphTop + contentPadding, graphRight, graphTop + 80), pBrush);

        D2D1_RECT_F chartArea = D2D1::RectF(graphLeft + contentPadding, graphTop + 80, graphRight - contentPadding, graphBottom - contentPadding);
        if (gType == GRAPH_PIE) {
            if (isDrillDown) {
                auto dataExp = db.GetPieData(start, end, TYPE_EXPENSE, m_currentDrillParent); auto dataInc = db.GetPieData(start, end, TYPE_INCOME, m_currentDrillParent);
                if (!dataExp.empty()) DrawPieChart(dataExp, chartArea, TYPE_EXPENSE); else if (!dataInc.empty()) DrawPieChart(dataInc, chartArea, TYPE_INCOME);
                else { D2D1_POINT_2F c = D2D1::Point2F((chartArea.left + chartArea.right) / 2, (chartArea.top + chartArea.bottom) / 2); pBrush->SetColor(ColorPalette::TextSecondary); pTxtTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); pRT->DrawText(L"データがありません", 9, pTxtTitle, D2D1::RectF(c.x - 100, c.y, c.x + 100, c.y + 50), pBrush); }
            }
            else {
                float width = chartArea.right - chartArea.left; float midX = chartArea.left + width / 2.0f; float gap = 10.0f;
                auto dataExp = db.GetPieData(start, end, TYPE_EXPENSE, m_currentDrillParent); DrawPieChart(dataExp, D2D1::RectF(chartArea.left, chartArea.top, midX - gap, chartArea.bottom), TYPE_EXPENSE);
                auto dataInc = db.GetPieData(start, end, TYPE_INCOME, m_currentDrillParent); DrawPieChart(dataInc, D2D1::RectF(midX + gap, chartArea.top, chartArea.right, chartArea.bottom), TYPE_INCOME);
            }
        }
        else { auto data = db.GetLineData(start, end, rMode); DrawLineChart(data, chartArea, rMode); } pRT->EndDraw();
    }
private:
    void DrawPieChart(const std::vector<TransactionSummary>& data, D2D1_RECT_F area, int type) {
        float fullW = area.right - area.left; float fullH = area.bottom - area.top; float pieAreaH = fullH * 0.70f; float legendAreaTop = area.top + pieAreaH;
        D2D1_POINT_2F center = D2D1::Point2F(area.left + fullW / 2.0f, area.top + pieAreaH / 2.0f); float radius = min(fullW, pieAreaH) * 0.45f;
        float animRadius = radius * (0.8f + 0.2f * m_animProgress); float animMaxAngle = 360.0f * m_animProgress; float currentAngleFromStart = 0.0f;
        float total = 0; for (const auto& d : data) total += d.amount;
        const TransactionSummary* pHoveredItem = nullptr; float hoveredPercentage = 0.0f;

        if (total > 0) {
            float startAngle = -90.0f; float dx = m_mousePos.x - center.x; float dy = m_mousePos.y - center.y; float dist = sqrt(dx * dx + dy * dy); float mouseAngle = atan2(dy, dx) * 180.0f / PI; if (mouseAngle < -90.0f) mouseAngle += 360.0f;
            for (const auto& d : data) {
                float sweepAngle = (d.amount / total) * 360.0f; if (sweepAngle > 360.0f) sweepAngle = 360.0f; float visibleSweep = sweepAngle;
                if (currentAngleFromStart + sweepAngle > animMaxAngle) { visibleSweep = animMaxAngle - currentAngleFromStart; if (visibleSweep < 0) visibleSweep = 0; }
                if (visibleSweep > 0) {
                    bool isHovered = false;
                    if (!m_isAnimating && dist <= animRadius) { float checkEnd = startAngle + sweepAngle; if (mouseAngle >= startAngle && mouseAngle < checkEnd) { isHovered = true; pHoveredItem = &d; m_lastHoveredName = d.label; m_lastHoveredHasChildren = d.hasChildren; hoveredPercentage = (d.amount / total) * 100.0f; } }
                    ID2D1PathGeometry* pPath = NULL; pFactory->CreatePathGeometry(&pPath); ID2D1GeometrySink* pSink = NULL; pPath->Open(&pSink); float drawRadius = isHovered ? animRadius * 1.05f : animRadius;
                    pSink->BeginFigure(center, D2D1_FIGURE_BEGIN_FILLED); float radStart = startAngle * (PI / 180.0f); float radEnd = (startAngle + visibleSweep) * (PI / 180.0f);
                    D2D1_POINT_2F startPt = D2D1::Point2F(center.x + drawRadius * cos(radStart), center.y + drawRadius * sin(radStart)); pSink->AddLine(startPt);
                    if (visibleSweep >= 359.9f) { float radMid = (startAngle + 180.0f) * (PI / 180.0f); D2D1_POINT_2F midPt = D2D1::Point2F(center.x + drawRadius * cos(radMid), center.y + drawRadius * sin(radMid)); pSink->AddArc(D2D1::ArcSegment(midPt, D2D1::SizeF(drawRadius, drawRadius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL)); pSink->AddArc(D2D1::ArcSegment(startPt, D2D1::SizeF(drawRadius, drawRadius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL)); }
                    else { D2D1_POINT_2F endPt = D2D1::Point2F(center.x + drawRadius * cos(radEnd), center.y + drawRadius * sin(radEnd)); pSink->AddArc(D2D1::ArcSegment(endPt, D2D1::SizeF(drawRadius, drawRadius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, visibleSweep > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL)); }
                    pSink->EndFigure(D2D1_FIGURE_END_CLOSED); pSink->Close(); pBrush->SetColor(d.color); pRT->FillGeometry(pPath, pBrush); pBrush->SetColor(ColorPalette::Card); pRT->DrawGeometry(pPath, pBrush, 1.5f); pPath->Release(); pSink->Release();
                } startAngle += sweepAngle; currentAngleFromStart += sweepAngle;
            }
            std::wstring moneyStr = ::FormatMoney(total); wchar_t totalStr[64]; swprintf_s(totalStr, L"%s合計\n¥%s", (type == TYPE_INCOME) ? L"収入" : L"支出", moneyStr.c_str());
            float holeRadius = animRadius * 0.5f; pBrush->SetColor(ColorPalette::Card); pBrush->SetOpacity(1.0f); pRT->FillEllipse(D2D1::Ellipse(center, holeRadius, holeRadius), pBrush);
            float textStartThreshold = 0.3f; float textAnimT = 0.0f; if (m_animProgress > textStartThreshold) { float rawT = (m_animProgress - textStartThreshold) / (1.0f - textStartThreshold); textAnimT = (float)(1.0 - pow(1.0f - rawT, 3)); }
            if (textAnimT > 0.0f) { pBrush->SetColor(ColorPalette::TextPrimary); pBrush->SetOpacity(textAnimT); float scale = 0.7f + 0.3f * textAnimT; D2D1_MATRIX_3X2_F oldTransform; pRT->GetTransform(&oldTransform); pRT->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, center) * oldTransform); pTxtTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); pRT->DrawText(totalStr, (UINT32)wcslen(totalStr), pTxtTitle, D2D1::RectF(center.x - holeRadius, center.y - 30, center.x + holeRadius, center.y + 30), pBrush); pRT->SetTransform(oldTransform); pBrush->SetOpacity(1.0f); }
            if (!data.empty()) {
                float legendY = legendAreaTop; float itemH = 22.0f; size_t maxLen = 0; for (const auto& d : data) { std::wstring label = d.label; if (d.hasChildren) label += L" (+)"; if (label.length() > maxLen) maxLen = label.length(); }
                float maxTextW = maxLen * 14.0f; float totalBlockW = 16.0f + 5.0f + maxTextW; float fixedStartX = center.x - (totalBlockW / 2.0f); pTxtLegend->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                float legendAnimOffset = (1.0f - m_animProgress) * 20.0f; float legendOpacity = (m_animProgress - 0.5f) * 2.0f; if (legendOpacity < 0) legendOpacity = 0; else if (legendOpacity > 1) legendOpacity = 1;
                if (legendOpacity > 0) {
                    for (const auto& d : data) { if (legendY + itemH > area.bottom) break; std::wstring label = d.label; if (d.hasChildren) label += L" (+)"; pBrush->SetColor(d.color); pBrush->SetOpacity(legendOpacity); pRT->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(fixedStartX, legendY + 4 + legendAnimOffset, fixedStartX + 12, legendY + 16 + legendAnimOffset), 2, 2), pBrush); pBrush->SetColor(ColorPalette::TextPrimary); pBrush->SetOpacity(legendOpacity); pRT->DrawText(label.c_str(), (UINT32)label.length(), pTxtNormal, D2D1::RectF(fixedStartX + 18, legendY + legendAnimOffset, area.right, legendY + 20 + legendAnimOffset), pBrush); legendY += itemH; }
                } pBrush->SetOpacity(1.0f);
            }
            if (pHoveredItem) { std::wstring tipMoney = ::FormatMoney(pHoveredItem->amount); wchar_t tipText[128]; std::wstring hint = pHoveredItem->hasChildren ? L"\n(クリックで詳細)" : L""; swprintf_s(tipText, L"%s\n¥%s (%.1f%%)%s", pHoveredItem->label.c_str(), tipMoney.c_str(), hoveredPercentage, hint.c_str()); DrawTooltip(tipText, pRT->GetSize()); }
        }
        else { pBrush->SetColor(ColorPalette::TextSecondary); pTxtTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); pRT->DrawText(L"データがありません", 9, pTxtTitle, D2D1::RectF(center.x - 100, center.y, center.x + 100, center.y + 50), pBrush); }
    }
    void DrawLineChart(const std::vector<TimeSeriesData>& data, D2D1_RECT_F area, ReportMode rMode) {
        float left = area.left + 50; float right = area.right - 20; float top = area.top + 20; float bottom = area.bottom - 40; float width = right - left; float height = bottom - top; if (width <= 0 || height <= 0) return;
        float maxVal = 1000.0f; for (const auto& d : data) { if (d.income > maxVal) maxVal = d.income; if (d.expense > maxVal) maxVal = d.expense; }
        float magnitude = (float)pow(10, floor(log10(maxVal))); maxVal = ceil(maxVal / magnitude) * magnitude; if (maxVal == 0) maxVal = 1000;
        int divY = 5; pTxtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        for (int i = 0; i <= divY; i++) { float val = maxVal * i / divY; float y = bottom - (val / maxVal) * height; pBrush->SetColor(ColorPalette::Separator); pRT->DrawLine(D2D1::Point2F(left, y), D2D1::Point2F(right, y), pBrush, 1.0f); std::wstring labelStr = ::FormatMoney(val); pBrush->SetColor(ColorPalette::TextSecondary); pRT->DrawText(labelStr.c_str(), (UINT32)labelStr.length(), pTxtSmall, D2D1::RectF(area.left, y - 8, left - 10, y + 8), pBrush); }
        int maxTime = (rMode == MODE_MONTHLY) ? 31 : 12; float stepX = width / (float)maxTime; pTxtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        for (int i = 1; i <= maxTime; i++) { float x = left + (i - 1) * stepX; bool showLabel = (rMode == MODE_YEARLY) || (i == 1 || i % 5 == 0 || i == maxTime); if (showLabel) { wchar_t buf[16]; swprintf_s(buf, L"%d", i); pRT->DrawText(buf, (UINT32)wcslen(buf), pTxtSmall, D2D1::RectF(x - 15, bottom + 5, x + 15, bottom + 25), pBrush); } }
        if (data.empty()) return;
        std::vector<float> incMap(maxTime + 1, 0), expMap(maxTime + 1, 0); for (const auto& d : data) { if (d.timeUnit >= 1 && d.timeUnit <= maxTime) { incMap[d.timeUnit] = d.income; expMap[d.timeUnit] = d.expense; } }
        struct HitPoint { float x, y, val; int time; bool isInc; D2D1_COLOR_F color; }; HitPoint bestHit = { 0 }; bool isHit = false; float minHitDist = 20.0f;
        float animHeightFactor = m_animProgress;
        auto ProcessPolyLine = [&](const std::vector<float>& values, D2D1_COLOR_F color, bool isInc) {
            ID2D1PathGeometry* pPath = NULL; pFactory->CreatePathGeometry(&pPath); ID2D1GeometrySink* pSink = NULL; pPath->Open(&pSink); bool first = true;
            for (int i = 1; i <= maxTime; i++) {
                float x = left + (i - 1) * stepX; float val = values[i]; float currentVal = val * animHeightFactor; float y = bottom - (currentVal / maxVal) * height;
                if (first) { pSink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_HOLLOW); first = false; }
                else { pSink->AddLine(D2D1::Point2F(x, y)); }
                float finalY = bottom - (val / maxVal) * height; float dx = m_mousePos.x - x; float dy = m_mousePos.y - finalY; float dist = sqrt(dx * dx + dy * dy);
                if (dist < minHitDist) { minHitDist = dist; bestHit = { x, finalY, val, i, isInc, color }; isHit = true; }
            } pSink->EndFigure(D2D1_FIGURE_END_OPEN); pSink->Close(); pBrush->SetColor(color); pRT->DrawGeometry(pPath, pBrush, 3.0f); pPath->Release(); pSink->Release();
            };
        ProcessPolyLine(incMap, ColorPalette::Graph[0], true); ProcessPolyLine(expMap, ColorPalette::Graph[1], false);
        pTxtLegend->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING); float lx = right - 150; float ly = top;
        pBrush->SetColor(ColorPalette::Graph[0]); pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lx, ly + 7), 4, 4), pBrush); pBrush->SetColor(ColorPalette::TextPrimary); pRT->DrawText(L"収入", 2, pTxtLegend, D2D1::RectF(lx + 10, ly, lx + 50, ly + 20), pBrush);
        lx += 60; pBrush->SetColor(ColorPalette::Graph[1]); pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lx, ly + 7), 4, 4), pBrush); pBrush->SetColor(ColorPalette::TextPrimary); pRT->DrawText(L"支出", 2, pTxtLegend, D2D1::RectF(lx + 10, ly, lx + 50, ly + 20), pBrush);
        if (isHit && !m_isAnimating) {
            pBrush->SetColor(ColorPalette::Card); pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bestHit.x, bestHit.y), 6.0f, 6.0f), pBrush); pBrush->SetColor(bestHit.color); pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(bestHit.x, bestHit.y), 6.0f, 6.0f), pBrush, 3.0f); std::wstring hitValStr = ::FormatMoney(bestHit.val); wchar_t tipText[128]; wchar_t unit[4]; wcscpy_s(unit, (rMode == MODE_MONTHLY) ? L"日" : L"月"); swprintf_s(tipText, L"%d%s (%s)\n¥%s", bestHit.time, unit, bestHit.isInc ? L"収入" : L"支出", hitValStr.c_str()); DrawTooltip(tipText, pRT->GetSize());
        }
    }
};

// -----------------------------------------------------------------------------
// SettingsWindow
// -----------------------------------------------------------------------------
class SettingsWindow {
public:
    static HWND hTab;
    static HWND hRadioExp, hRadioInc;

    // カテゴリタブ用
    static HWND hListCat;
    static HWND hEditNew, hComboParent;
    static HWND hBtnCatAdd, hBtnCatUpd, hBtnCatDel, hBtnCatUp, hBtnCatDown;
    static HWND hLblCatTitle, hLblCatName, hLblCatParent;

    // 固定費タブ用
    static HWND hListFixed;
    static HWND hEditFixDay, hEditFixAmt, hEditFixName;
    static HWND hBtnFixAdd, hBtnFixDel;
    static HWND hLblFixDayPre, hLblFixDayPost, hLblFixAmt, hLblFixName;

    static ExpenseManager* pDB;
    static int currentType;
    static std::vector<CategoryItem> s_currentItems;
    static std::vector<ExpenseManager::FixedCostItem> s_currentFixed;
    static HBRUSH hBrWhite;

    static void Show(HWND hParent, ExpenseManager* db) {
        pDB = db;
        currentType = TYPE_EXPENSE;
        WNDCLASS wc = { 0 };
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"SettingsWnd";
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        RegisterClass(&wc);

        int w = 560; int h = 620;
        RECT rc; GetWindowRect(hParent, &rc);
        int x = rc.left + (rc.right - rc.left - w) / 2;
        int y = rc.top + (rc.bottom - rc.top - h) / 2;

        HWND hWnd = CreateWindow(L"SettingsWnd", L"設定", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
            x, y, w, h, hParent, NULL, wc.hInstance, NULL);

        EnableWindow(hParent, FALSE);
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); break; }
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (!IsWindow(hWnd)) break;
        }
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
    }

    static void RefreshList() {
        int tabIdx = TabCtrl_GetCurSel(hTab);
        SendMessage(hComboParent, CB_RESETCONTENT, 0, 0);
        auto cats = pDB->GetCategories(currentType);

        for (const auto& item : cats) {
            if (item.parentId == 0 || tabIdx == 1) {
                int idx = (int)SendMessage(hComboParent, CB_ADDSTRING, 0, (LPARAM)item.name.c_str());
                SendMessage(hComboParent, CB_SETITEMDATA, idx, (LPARAM)item.id);
            }
        }

        if (tabIdx == 0) {
            int idx = (int)SendMessage(hComboParent, CB_INSERTSTRING, 0, (LPARAM)L"(親なし:トップ)");
            SendMessage(hComboParent, CB_SETITEMDATA, idx, 0);
            SendMessage(hComboParent, CB_SETCURSEL, 0, 0);

            SendMessage(hListCat, LB_RESETCONTENT, 0, 0);
            s_currentItems = pDB->GetCategories(currentType);
            for (const auto& item : s_currentItems) {
                std::wstring disp = item.name;
                if (item.parentId != 0) disp = L"    " + disp;
                int idx = (int)SendMessage(hListCat, LB_ADDSTRING, 0, (LPARAM)disp.c_str());
                SendMessage(hListCat, LB_SETITEMDATA, idx, (LPARAM)item.id);
            }
            SetWindowText(hEditNew, L"");
        }
        else {
            if (cats.size() > 0) SendMessage(hComboParent, CB_SETCURSEL, 0, 0);
            ListView_DeleteAllItems(hListFixed);
            s_currentFixed = pDB->GetFixedCosts(currentType);
            for (size_t i = 0; i < s_currentFixed.size(); ++i) {
                auto& item = s_currentFixed[i];
                wchar_t buf[64];
                LVITEM lvi = { 0 }; lvi.mask = LVIF_TEXT | LVIF_PARAM; lvi.iItem = (int)i; lvi.lParam = item.id;

                swprintf_s(buf, L"%d日", item.day); lvi.pszText = buf;
                ListView_InsertItem(hListFixed, &lvi);

                ListView_SetItemText(hListFixed, i, 1, (LPWSTR)item.category.c_str());
                std::wstring sAmt = FormatMoney(item.amount);
                ListView_SetItemText(hListFixed, i, 2, (LPWSTR)sAmt.c_str());
                ListView_SetItemText(hListFixed, i, 3, (LPWSTR)item.name.c_str());
            }
        }
    }

    static void SelectCategoryById(HWND hParent, int targetId) {
        int count = (int)SendMessage(hListCat, LB_GETCOUNT, 0, 0);
        for (int i = 0; i < count; i++) {
            int id = (int)SendMessage(hListCat, LB_GETITEMDATA, i, 0);
            if (id == targetId) {
                SendMessage(hListCat, LB_SETCURSEL, i, 0);
                SendMessage(hParent, WM_COMMAND, MAKEWPARAM(ID_LIST_CATS, LBN_SELCHANGE), (LPARAM)hListCat);
                return;
            }
        }
    }

    static void ToggleControls(int tabIdx) {
        bool isCat = (tabIdx == 0);
        int showCat = isCat ? SW_SHOW : SW_HIDE;
        int showFix = isCat ? SW_HIDE : SW_SHOW;
        HWND catCtrls[] = { hListCat, hEditNew, hBtnCatAdd, hBtnCatUpd, hBtnCatDel, hBtnCatUp, hBtnCatDown, hLblCatTitle, hLblCatName, hLblCatParent };
        for (HWND h : catCtrls) ShowWindow(h, showCat);
        HWND fixCtrls[] = { hListFixed, hEditFixDay, hEditFixAmt, hEditFixName, hBtnFixAdd, hBtnFixDel, hLblFixDayPre, hLblFixDayPost, hLblFixAmt, hLblFixName };
        for (HWND h : fixCtrls) ShowWindow(h, showFix);
    }

    static void ApplyModernStyle(HWND hControl) {
        SetWindowTheme(hControl, L"Explorer", NULL);
    }

    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE: {
            hBrWhite = CreateSolidBrush(RGB(255, 255, 255));
            hTab = CreateWindow(WC_TABCONTROL, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 10, 10, 520, 30, hwnd, NULL, NULL, NULL);
            TCITEM tie = { 0 }; tie.mask = TCIF_TEXT;
            tie.pszText = (LPWSTR)L"カテゴリ編集"; TabCtrl_InsertItem(hTab, 0, &tie);
            tie.pszText = (LPWSTR)L"固定費 (自動登録)"; TabCtrl_InsertItem(hTab, 1, &tie);
            SendMessage(hTab, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 0);

            CreateWindow(L"STATIC", L"区分:", WS_CHILD | WS_VISIBLE, 20, 50, 40, 20, hwnd, NULL, NULL, NULL);
            hRadioExp = CreateWindow(L"BUTTON", L"支出", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 70, 48, 60, 25, hwnd, (HMENU)400, NULL, NULL);
            hRadioInc = CreateWindow(L"BUTTON", L"収入", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 140, 48, 60, 25, hwnd, (HMENU)401, NULL, NULL);
            SendMessage(hRadioExp, BM_SETCHECK, BST_CHECKED, 0);

            hListCat = CreateWindow(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
                20, 80, 280, 350, hwnd, (HMENU)ID_LIST_CATS, NULL, NULL);
            g_OldListBoxProc = (WNDPROC)SetWindowLongPtr(hListCat, GWLP_WNDPROC, (LONG_PTR)ListBoxProc);
            ApplyModernStyle(hListCat);

            hBtnCatUp = CreateWindow(L"BUTTON", L"↑", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 310, 150, 40, 40, hwnd, (HMENU)ID_BTN_UP, NULL, NULL);
            hBtnCatDown = CreateWindow(L"BUTTON", L"↓", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 310, 200, 40, 40, hwnd, (HMENU)ID_BTN_DOWN, NULL, NULL);

            hLblCatTitle = CreateWindow(L"STATIC", L"編集 / 追加", WS_CHILD | WS_VISIBLE, 20, 445, 100, 20, hwnd, NULL, NULL, NULL);
            hLblCatName = CreateWindow(L"STATIC", L"名前:", WS_CHILD | WS_VISIBLE, 20, 473, 40, 20, hwnd, NULL, NULL, NULL);
            hEditNew = CreateWindowEx(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER, 70, 470, 160, 28, hwnd, (HMENU)ID_EDIT_NEW_CAT, NULL, NULL);
            hLblCatParent = CreateWindow(L"STATIC", L"親:", WS_CHILD | WS_VISIBLE, 240, 473, 30, 20, hwnd, NULL, NULL, NULL);
            hComboParent = CreateWindow(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 270, 470, 140, 200, hwnd, (HMENU)ID_COMBO_PARENT, NULL, NULL);
            ApplyModernStyle(hComboParent);

            hBtnCatAdd = CreateWindow(L"BUTTON", L"新規追加", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 515, 100, 35, hwnd, (HMENU)ID_BTN_ADD_CAT, NULL, NULL);
            hBtnCatUpd = CreateWindow(L"BUTTON", L"変更保存", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 130, 515, 100, 35, hwnd, (HMENU)ID_BTN_UPDATE_CAT, NULL, NULL);
            hBtnCatDel = CreateWindow(L"BUTTON", L"削除", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 240, 515, 100, 35, hwnd, (HMENU)ID_BTN_DELETE_CAT, NULL, NULL);

            hListFixed = CreateWindowEx(0, WC_LISTVIEW, L"", WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER,
                20, 80, 500, 350, hwnd, (HMENU)5001, NULL, NULL);
            ListView_SetExtendedListViewStyle(hListFixed, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            ApplyModernStyle(hListFixed);

            LVCOLUMN lvc = { 0 }; lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            lvc.pszText = (LPWSTR)L"日"; lvc.cx = 40; lvc.fmt = LVCFMT_CENTER; ListView_InsertColumn(hListFixed, 0, &lvc);
            lvc.pszText = (LPWSTR)L"カテゴリ"; lvc.cx = 140; lvc.fmt = LVCFMT_LEFT; ListView_InsertColumn(hListFixed, 1, &lvc);
            lvc.pszText = (LPWSTR)L"金額"; lvc.cx = 90; lvc.fmt = LVCFMT_RIGHT; ListView_InsertColumn(hListFixed, 2, &lvc);
            lvc.pszText = (LPWSTR)L"メモ"; lvc.cx = 200; lvc.fmt = LVCFMT_LEFT; ListView_InsertColumn(hListFixed, 3, &lvc);

            hLblFixDayPre = CreateWindow(L"STATIC", L"毎月", WS_CHILD, 20, 453, 30, 20, hwnd, NULL, NULL, NULL);
            hEditFixDay = CreateWindowEx(0, L"EDIT", L"25", WS_CHILD | ES_NUMBER | WS_BORDER, 55, 450, 30, 28, hwnd, NULL, NULL, NULL);
            hLblFixDayPost = CreateWindow(L"STATIC", L"日", WS_CHILD, 90, 453, 20, 20, hwnd, NULL, NULL, NULL);
            hLblFixAmt = CreateWindow(L"STATIC", L"金額:", WS_CHILD, 120, 453, 35, 20, hwnd, NULL, NULL, NULL);
            hEditFixAmt = CreateWindowEx(0, L"EDIT", L"", WS_CHILD | ES_NUMBER | WS_BORDER, 160, 450, 100, 28, hwnd, NULL, NULL, NULL);
            hLblFixName = CreateWindow(L"STATIC", L"メモ:", WS_CHILD, 20, 493, 35, 20, hwnd, NULL, NULL, NULL);
            hEditFixName = CreateWindowEx(0, L"EDIT", L"", WS_CHILD | WS_BORDER, 60, 490, 200, 28, hwnd, NULL, NULL, NULL);

            hBtnFixAdd = CreateWindow(L"BUTTON", L"固定費を追加", WS_CHILD | BS_PUSHBUTTON, 20, 530, 120, 35, hwnd, (HMENU)5010, NULL, NULL);
            hBtnFixDel = CreateWindow(L"BUTTON", L"選択を削除", WS_CHILD | BS_PUSHBUTTON, 150, 530, 100, 35, hwnd, (HMENU)5011, NULL, NULL);

            HFONT hFont = CreateFont(19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            EnumChildWindows(hwnd, [](HWND h, LPARAM l) { SendMessage(h, WM_SETFONT, l, TRUE); return TRUE; }, (LPARAM)hFont);

            RefreshList();
            ToggleControls(0);
            return 0;
        }
        case WM_CTLCOLORSTATIC: { HDC hdc = (HDC)wp; SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(50, 50, 50)); return (LRESULT)hBrWhite; }
        case WM_CTLCOLORBTN: return (LRESULT)hBrWhite;
        case WM_NOTIFY: {
            NMHDR* pnm = (NMHDR*)lp;
            if (pnm->code == TCN_SELCHANGE) { int idx = TabCtrl_GetCurSel(hTab); ToggleControls(idx); RefreshList(); }
            return 0;
        }
        case WM_APP + 1: { // ドラッグ&ドロップ後
            int s = (int)wp, d = (int)lp;
            if (s < 0 || d < 0 || d == s + 1) return 0;
            int targetId = -1;
            if (s >= 0 && s < (int)s_currentItems.size()) targetId = s_currentItems[s].id;
            if (d > s) { for (int i = s; i < d - 1; i++) { CategoryItem& a = s_currentItems[i], & b = s_currentItems[i + 1]; pDB->SwapCategoryOrder(a.id, a.sortOrder, b.id, b.sortOrder); std::swap(a.sortOrder, b.sortOrder); std::swap(s_currentItems[i], s_currentItems[i + 1]); } }
            else { for (int i = s; i > d; i--) { CategoryItem& a = s_currentItems[i], & b = s_currentItems[i - 1]; pDB->SwapCategoryOrder(a.id, a.sortOrder, b.id, b.sortOrder); std::swap(a.sortOrder, b.sortOrder); std::swap(s_currentItems[i], s_currentItems[i - 1]); } }
            RefreshList();
            if (targetId != -1) SelectCategoryById(hwnd, targetId);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
            if (LOWORD(wp) == 400 || LOWORD(wp) == 401) { currentType = (LOWORD(wp) == 401) ? TYPE_INCOME : TYPE_EXPENSE; RefreshList(); }

            // ★修正: 選択変更時（クリック or プログラム選択 or 空白クリック）
            if (LOWORD(wp) == ID_LIST_CATS && HIWORD(wp) == LBN_SELCHANGE) {
                int idx = (int)SendMessage(hListCat, LB_GETCURSEL, 0, 0);
                if (idx != LB_ERR) {
                    // 選択あり -> 値をエディットボックス等へ反映
                    int id = (int)SendMessage(hListCat, LB_GETITEMDATA, idx, 0);
                    auto it = std::find_if(s_currentItems.begin(), s_currentItems.end(), [&](const CategoryItem& i) { return i.id == id; });
                    if (it != s_currentItems.end()) {
                        SetWindowText(hEditNew, it->name.c_str());
                        int cnt = (int)SendMessage(hComboParent, CB_GETCOUNT, 0, 0);
                        for (int i = 0; i < cnt; i++) { if ((int)SendMessage(hComboParent, CB_GETITEMDATA, i, 0) == it->parentId) { SendMessage(hComboParent, CB_SETCURSEL, i, 0); break; } }
                    }
                }
                else {
                    // ★追加: 選択なし（空白クリック等） -> エディットボックス等をクリア
                    SetWindowText(hEditNew, L"");
                    SendMessage(hComboParent, CB_SETCURSEL, 0, 0); // (親なし:トップ)等へ
                }
            }
            if (LOWORD(wp) == ID_BTN_ADD_CAT) {
                wchar_t buf[256]; GetWindowText(hEditNew, buf, 256);
                if (wcslen(buf) > 0) { int pIdx = (int)SendMessage(hComboParent, CB_GETCURSEL, 0, 0); pDB->AddCategory(buf, currentType, (pIdx != CB_ERR) ? (int)SendMessage(hComboParent, CB_GETITEMDATA, pIdx, 0) : 0); RefreshList(); }
            }
            if (LOWORD(wp) == ID_BTN_UPDATE_CAT) {
                int idx = (int)SendMessage(hListCat, LB_GETCURSEL, 0, 0);
                if (idx != LB_ERR) {
                    int id = (int)SendMessage(hListCat, LB_GETITEMDATA, idx, 0); wchar_t buf[256]; GetWindowText(hEditNew, buf, 256);
                    if (wcslen(buf) > 0) { int pIdx = (int)SendMessage(hComboParent, CB_GETCURSEL, 0, 0); int pid = (pIdx != CB_ERR) ? (int)SendMessage(hComboParent, CB_GETITEMDATA, pIdx, 0) : 0; if (pid != id) { pDB->UpdateCategory(id, buf, pid); RefreshList(); } else MessageBox(hwnd, L"自身を親にはできません", L"Err", MB_OK); }
                }
            }
            if (LOWORD(wp) == ID_BTN_DELETE_CAT) {
                int idx = (int)SendMessage(hListCat, LB_GETCURSEL, 0, 0);
                if (idx != LB_ERR && MessageBox(hwnd, L"削除しますか？", L"確認", MB_YESNO) == IDYES) { pDB->DeleteCategory((int)SendMessage(hListCat, LB_GETITEMDATA, idx, 0)); RefreshList(); }
            }
            if (LOWORD(wp) == ID_BTN_UP) MoveItem(hwnd, -1);
            if (LOWORD(wp) == ID_BTN_DOWN) MoveItem(hwnd, 1);

            if (LOWORD(wp) == 5010) {
                wchar_t dayStr[10], amtStr[32], name[128], cat[128];
                GetWindowText(hEditFixDay, dayStr, 10); GetWindowText(hEditFixAmt, amtStr, 32); GetWindowText(hEditFixName, name, 128);
                int catIdx = (int)SendMessage(hComboParent, CB_GETCURSEL, 0, 0);
                int day = _wtoi(dayStr); float amt = (float)_wtof(amtStr);
                if (day < 1 || day > 31 || amt <= 0 || catIdx == CB_ERR) MessageBox(hwnd, L"日付(1-31)と金額、カテゴリを確認してください", L"エラー", MB_OK);
                else { SendMessage(hComboParent, CB_GETLBTEXT, catIdx, (LPARAM)cat); pDB->AddFixedCost(day, cat, amt, currentType, name); RefreshList(); MessageBox(hwnd, L"固定費を設定しました。\n次回起動時(当日以降)に自動登録されます。", L"完了", MB_OK); }
            }
            if (LOWORD(wp) == 5011) {
                int idx = ListView_GetNextItem(hListFixed, -1, LVNI_SELECTED);
                if (idx != -1) { LVITEM lvi = { 0 }; lvi.iItem = idx; lvi.mask = LVIF_PARAM; ListView_GetItem(hListFixed, &lvi); pDB->DeleteFixedCost((int)lvi.lParam); RefreshList(); }
            }
            return 0;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY: DeleteObject(hBrWhite); return 0;
        }
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    static void MoveItem(HWND hParent, int dir) {
        int idx = (int)SendMessage(hListCat, LB_GETCURSEL, 0, 0); if (idx == LB_ERR) return;
        int cid = (int)SendMessage(hListCat, LB_GETITEMDATA, idx, 0);
        int cVecIdx = -1; for (int i = 0; i < (int)s_currentItems.size(); i++) if (s_currentItems[i].id == cid) { cVecIdx = i; break; }
        if (cVecIdx == -1) return;
        CategoryItem& c = s_currentItems[cVecIdx];
        int tVecIdx = -1;
        if (dir < 0) { for (int i = cVecIdx - 1; i >= 0; i--) if (s_currentItems[i].parentId == c.parentId || (s_currentItems[i].parentId == 0 && c.parentId == 0)) { tVecIdx = i; break; } }
        else { for (int i = cVecIdx + 1; i < (int)s_currentItems.size(); i++) if (s_currentItems[i].parentId == c.parentId || (s_currentItems[i].parentId == 0 && c.parentId == 0)) { tVecIdx = i; break; } }
        if (tVecIdx != -1) {
            CategoryItem& t = s_currentItems[tVecIdx];
            int oc = c.sortOrder, ot = t.sortOrder;
            if (oc == ot) { if (dir > 0) ot++; else oc++; }
            pDB->SwapCategoryOrder(c.id, oc, t.id, ot);
            int savedId = c.id;
            RefreshList();
            SelectCategoryById(hParent, savedId);
        }
    }
};

// Staticメンバの実体
HWND SettingsWindow::hTab = NULL; HWND SettingsWindow::hRadioExp = NULL; HWND SettingsWindow::hRadioInc = NULL;
HWND SettingsWindow::hListCat = NULL; HWND SettingsWindow::hEditNew = NULL; HWND SettingsWindow::hComboParent = NULL;
HWND SettingsWindow::hBtnCatAdd = NULL; HWND SettingsWindow::hBtnCatUpd = NULL; HWND SettingsWindow::hBtnCatDel = NULL; HWND SettingsWindow::hBtnCatUp = NULL; HWND SettingsWindow::hBtnCatDown = NULL;
HWND SettingsWindow::hListFixed = NULL; HWND SettingsWindow::hEditFixDay = NULL; HWND SettingsWindow::hEditFixAmt = NULL; HWND SettingsWindow::hEditFixName = NULL; HWND SettingsWindow::hBtnFixAdd = NULL; HWND SettingsWindow::hBtnFixDel = NULL;
HWND SettingsWindow::hLblCatTitle = NULL; HWND SettingsWindow::hLblCatName = NULL; HWND SettingsWindow::hLblCatParent = NULL;
HWND SettingsWindow::hLblFixDayPre = NULL; HWND SettingsWindow::hLblFixDayPost = NULL; HWND SettingsWindow::hLblFixAmt = NULL; HWND SettingsWindow::hLblFixName = NULL;
ExpenseManager* SettingsWindow::pDB = nullptr; int SettingsWindow::currentType = TYPE_EXPENSE;
std::vector<CategoryItem> SettingsWindow::s_currentItems; std::vector<ExpenseManager::FixedCostItem> SettingsWindow::s_currentFixed;
HBRUSH SettingsWindow::hBrWhite = NULL;

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_GETDLGCODE && lp) { MSG* pMsg = (MSG*)lp; if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN) return (LRESULT)CallWindowProc(g_OldEditProc, hwnd, msg, wp, lp) | DLGC_WANTALLKEYS; }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(100, BN_CLICKED), (LPARAM)GetDlgItem(GetParent(hwnd), 100)); return 0; }
    return CallWindowProc(g_OldEditProc, hwnd, msg, wp, lp);
}

UiControls::ModernCalendar* g_pCalInstance = nullptr;
LRESULT CALLBACK CalendarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        g_pCalInstance = new UiControls::ModernCalendar(); g_pCalInstance->Initialize(hwnd);
        g_pCalInstance->SetOnDateChanged([hwnd](UiControls::DateInfo d) { SendMessage(GetParent(hwnd), WM_APP_CAL_CHANGE, 0, 0); });
        g_pCalInstance->SetOnViewChanged([hwnd](int y, int m) { SendMessage(GetParent(hwnd), WM_APP_CAL_VIEW_CHANGE, (WPARAM)y, (LPARAM)m); }); return 0;
    }
    if (msg == WM_DESTROY) { delete g_pCalInstance; g_pCalInstance = nullptr; return 0; }
    if (g_pCalInstance) {
        switch (msg) {
        case WM_PAINT: g_pCalInstance->OnPaint(); ValidateRect(hwnd, NULL); return 0;
        case WM_SIZE: g_pCalInstance->OnResize(LOWORD(lp), HIWORD(lp)); return 0;
        case WM_MOUSEMOVE: g_pCalInstance->OnMouseMove(LOWORD(lp), HIWORD(lp)); return 0;
        case WM_LBUTTONDOWN: g_pCalInstance->OnLButtonDown(LOWORD(lp), HIWORD(lp)); return 0;
        case WM_MOUSEWHEEL: g_pCalInstance->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp)); return 0;
        case WM_TIMER: g_pCalInstance->OnTimer(); return 0;
        }
    } return DefWindowProc(hwnd, msg, wp, lp);
}
void RegisterCalendarClass(HINSTANCE hInst) { WNDCLASS wc = { 0 }; wc.lpfnWndProc = CalendarWndProc; wc.hInstance = hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = L"ModernCalHost"; RegisterClass(&wc); }

// -----------------------------------------------------------------------------
// InputPanel (Controller/View)
// -----------------------------------------------------------------------------
class InputPanel {
public:
    HWND hModernCalHost;
    HWND hComboCat, hEditAmt, hBtnAdd, hBtnSettings;
    HWND hRadioMonth, hRadioYear, hRadioPie, hRadioLine, hRadioExp, hRadioInc, hListTrans;
    ExpenseManager* pDB;
    int m_editingId;

    InputPanel() : m_editingId(-1) {}

    void Create(HWND parent, ExpenseManager* db) {
        pDB = db;
        const int mx = 10;
        const int panelW = 300;
        const int ctrlW = panelW;
        int y = 10;

        hModernCalHost = CreateWindowEx(0, L"ModernCalHost", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            mx, y, panelW, 250, parent, (HMENU)200, GetModuleHandle(NULL), NULL);

        y += 265;

        CreateWindow(L"STATIC", L"収支区分", WS_CHILD | WS_VISIBLE, mx, y, ctrlW, 20, parent, NULL, NULL, NULL);
        y += 22;

        int rbWidth = (ctrlW / 2) - 5;
        hRadioExp = CreateWindow(L"BUTTON", L"支出", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
            mx, y, rbWidth, 30, parent, (HMENU)400, NULL, NULL);
        hRadioInc = CreateWindow(L"BUTTON", L"収入", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            mx + rbWidth + 10, y, rbWidth, 30, parent, (HMENU)401, NULL, NULL);
        SendMessage(hRadioExp, BM_SETCHECK, BST_CHECKED, 0);

        y += 45;

        CreateWindow(L"STATIC", L"カテゴリ", WS_CHILD | WS_VISIBLE, mx, y, ctrlW, 20, parent, NULL, NULL, NULL);
        y += 22;

        int btnSetW = 50;
        int comboW = ctrlW - btnSetW - 5;
        hComboCat = CreateWindowEx(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_TABSTOP,
            mx, y + 1, comboW, 300, parent, NULL, NULL, NULL);

        hBtnSettings = CreateWindow(L"BUTTON", L"設定",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            mx + comboW + 5, y, btnSetW, 30, parent, (HMENU)ID_BTN_SETTINGS, NULL, NULL);

        y += 45;

        CreateWindow(L"STATIC", L"金額 (¥)", WS_CHILD | WS_VISIBLE, mx, y, ctrlW, 20, parent, NULL, NULL, NULL);
        y += 22;

        hEditAmt = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_RIGHT,
            mx, y, ctrlW, 30, parent, NULL, NULL, NULL);
        g_OldEditProc = (WNDPROC)SetWindowLongPtr(hEditAmt, GWLP_WNDPROC, (LONG_PTR)EditProc);

        y += 45;

        hBtnAdd = CreateWindow(L"BUTTON", L"登録する",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON | WS_TABSTOP,
            mx, y, ctrlW, 40, parent, (HMENU)100, NULL, NULL);

        y += 55;

        CreateWindow(L"STATIC", L"選択日の明細 (右クリック操作)", WS_CHILD | WS_VISIBLE, mx, y, ctrlW, 20, parent, NULL, NULL, NULL);
        y += 22;

        hListTrans = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP | LVS_NOSORTHEADER,
            mx, y, ctrlW, 180, parent, (HMENU)105, NULL, NULL);

        ListView_SetExtendedListViewStyle(hListTrans, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        LVCOLUMN lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt = LVCFMT_LEFT; lvc.cx = 120; lvc.pszText = (LPWSTR)L"カテゴリ";
        ListView_InsertColumn(hListTrans, 0, &lvc);
        lvc.fmt = LVCFMT_RIGHT; lvc.cx = 80; lvc.pszText = (LPWSTR)L"金額";
        ListView_InsertColumn(hListTrans, 1, &lvc);
        lvc.fmt = LVCFMT_CENTER; lvc.cx = 40; lvc.pszText = (LPWSTR)L"種";
        ListView_InsertColumn(hListTrans, 2, &lvc);

        y += 195;

        int labelW = 70;
        int optH = 25;

        CreateWindow(L"STATIC", L"期間", WS_CHILD | WS_VISIBLE, mx, y + 4, labelW, 20, parent, NULL, NULL, NULL);
        hRadioMonth = CreateWindow(L"BUTTON", L"月間", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
            mx + labelW, y, 60, optH, parent, (HMENU)101, NULL, NULL);
        hRadioYear = CreateWindow(L"BUTTON", L"年間", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            mx + labelW + 65, y, 60, optH, parent, (HMENU)102, NULL, NULL);
        SendMessage(hRadioMonth, BM_SETCHECK, BST_CHECKED, 0);

        y += 32;

        CreateWindow(L"STATIC", L"グラフ", WS_CHILD | WS_VISIBLE, mx, y + 4, labelW, 20, parent, NULL, NULL, NULL);
        hRadioPie = CreateWindow(L"BUTTON", L"円グラフ", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
            mx + labelW, y, 80, optH, parent, (HMENU)103, NULL, NULL);
        hRadioLine = CreateWindow(L"BUTTON", L"推移", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            mx + labelW + 85, y, 60, optH, parent, (HMENU)104, NULL, NULL);
        SendMessage(hRadioPie, BM_SETCHECK, BST_CHECKED, 0);

        ApplyFont(parent);
        ApplyModernTheme();

        UpdateCategoryList();
        RefreshTransactionList();
        RefreshCalendarMarks();
    }

    void ApplyFont(HWND parent) {
        HFONT hFont = CreateFont(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        EnumChildWindows(parent, [](HWND hwnd, LPARAM lparam) -> BOOL { SendMessage(hwnd, WM_SETFONT, (WPARAM)lparam, TRUE); return TRUE; }, (LPARAM)hFont);
    }

    void ApplyModernTheme() {
        HWND controls[] = {
            hRadioExp, hRadioInc, hBtnSettings, hBtnAdd, hListTrans,
            hRadioMonth, hRadioYear, hRadioPie, hRadioLine
        };
        for (HWND h : controls) {
            SetWindowTheme(h, L"Explorer", NULL);
        }
    }

    void UpdateCategoryList() { int type = GetCurrentType(); SendMessage(hComboCat, CB_RESETCONTENT, 0, 0); auto items = pDB->GetCategories(type); for (const auto& item : items) SendMessage(hComboCat, CB_ADDSTRING, 0, (LPARAM)item.name.c_str()); if (!items.empty()) SendMessage(hComboCat, CB_SETCURSEL, 0, 0); }

    void RefreshTransactionList() {
        ListView_DeleteAllItems(hListTrans); std::wstring date = GetSelectedDate(); auto list = pDB->GetTransactionsForDate(date);
        int i = 0; LVITEM lvi = { 0 }; lvi.mask = LVIF_TEXT | LVIF_PARAM;
        for (const auto& item : list) {
            lvi.iItem = i; lvi.iSubItem = 0; lvi.pszText = (LPWSTR)item.category.c_str(); lvi.lParam = item.id; ListView_InsertItem(hListTrans, &lvi);
            std::wstring sAmt = ::FormatMoney(item.amount);
            ListView_SetItemText(hListTrans, i, 1, (LPWSTR)sAmt.c_str());
            ListView_SetItemText(hListTrans, i, 2, (item.type == TYPE_INCOME) ? L"収" : L"支"); i++;
        } CancelEditMode();
    }

    void StartEditMode(int id, const std::wstring& cat, float amt, int type) {
        m_editingId = id; SetWindowText(hComboCat, cat.c_str()); wchar_t buf[64]; swprintf_s(buf, L"%.0f", amt); SetWindowText(hEditAmt, buf);
        if (type == TYPE_INCOME) { SendMessage(hRadioInc, BM_SETCHECK, BST_CHECKED, 0); SendMessage(hRadioExp, BM_SETCHECK, BST_UNCHECKED, 0); }
        else { SendMessage(hRadioExp, BM_SETCHECK, BST_CHECKED, 0); SendMessage(hRadioInc, BM_SETCHECK, BST_UNCHECKED, 0); }
        UpdateCategoryList(); SetWindowText(hBtnAdd, L"修正保存");
    }
    void CancelEditMode() { m_editingId = -1; SetWindowText(hEditAmt, L""); SetWindowText(hBtnAdd, L"登録する"); }
    std::wstring GetSelectedDate() { if (!g_pCalInstance) return L""; auto d = g_pCalInstance->GetSelectedDate(); wchar_t buf[32]; swprintf_s(buf, L"%04d-%02d-%02d", d.year, d.month, d.day); return buf; }
    void GetSelectedDateStruct(SYSTEMTIME* st) { if (!g_pCalInstance) return; auto d = g_pCalInstance->GetSelectedDate(); st->wYear = d.year; st->wMonth = d.month; st->wDay = d.day; }
    bool GetInput(std::wstring& outCat, float& outAmt, int& outType) { wchar_t buf[256]; GetWindowText(hComboCat, buf, 256); outCat = buf; GetWindowText(hEditAmt, buf, 256); outAmt = (float)_wtof(buf); outType = GetCurrentType(); return !outCat.empty() && outAmt > 0; }
    int GetCurrentType() { return (SendMessage(hRadioInc, BM_GETCHECK, 0, 0) == BST_CHECKED) ? TYPE_INCOME : TYPE_EXPENSE; }
    ReportMode GetReportMode() { return (SendMessage(hRadioYear, BM_GETCHECK, 0, 0) == BST_CHECKED) ? MODE_YEARLY : MODE_MONTHLY; }
    GraphType GetGraphType() { return (SendMessage(hRadioLine, BM_GETCHECK, 0, 0) == BST_CHECKED) ? GRAPH_LINE : GRAPH_PIE; }

    // --- 修正: カレンダーのマーカー更新 (月・年も対応) ---
    void RefreshCalendarMarks() {
        if (!g_pCalInstance) return;
        auto viewMode = g_pCalInstance->GetViewMode();
        int currentYear = g_pCalInstance->GetCurrentYear();

        if (viewMode == UiControls::CalendarViewMode::Day) {
            SYSTEMTIME st; GetSelectedDateStruct(&st);
            UpdateMarksForMonth(st.wYear, st.wMonth);
        }
        else if (viewMode == UiControls::CalendarViewMode::Month) {
            auto months = pDB->GetRecordedMonths(currentYear);
            g_pCalInstance->SetMarkedMonths(currentYear, months);
        }
        else {
            auto years = pDB->GetRecordedYears();
            g_pCalInstance->SetRecordedYears(years);
        }
    }

    void UpdateMarksForMonth(int year, int month) { if (!g_pCalInstance) return; auto days = pDB->GetRecordedDays(year, month); g_pCalInstance->SetMarkedDays(year, month, days); }
};

class MainApp {
    ExpenseManager dbManager; ChartCanvas canvas; InputPanel inputPanel; HWND hWnd;
    LPARAM GetTransactionIdFromList(int idx) { LVITEM lvi = { 0 }; lvi.iItem = idx; lvi.mask = LVIF_PARAM; ListView_GetItem(inputPanel.hListTrans, &lvi); return lvi.lParam; }
    std::wstring GetTransactionTextFromList(int idx, int subItem) { wchar_t buf[256]; ListView_GetItemText(inputPanel.hListTrans, idx, subItem, buf, 256); return std::wstring(buf); }
public:
    void Run(HINSTANCE hInstance) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        InitCommonControls(); RegisterCalendarClass(hInstance);
        dbManager.Initialize(); dbManager.CheckAndApplyFixedCosts();
        canvas.Initialize();
        WNDCLASS wc = { 0 }; wc.lpfnWndProc = MainApp::WndProc; wc.hInstance = hInstance; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));  wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH); wc.lpszClassName = L"SimpleKakeibo"; RegisterClass(&wc);
        hWnd = CreateWindow(L"SimpleKakeibo", L"シンプル家計簿 v1.0.0", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1200, 850, NULL, NULL, hInstance, this);
        ShowWindow(hWnd, SW_SHOW);
        MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { if (!IsDialogMessage(hWnd, &msg)) { TranslateMessage(&msg); DispatchMessage(&msg); } }
    }
private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        MainApp* pApp = NULL; if (msg == WM_NCCREATE) { CREATESTRUCT* pCreate = (CREATESTRUCT*)lp; pApp = (MainApp*)pCreate->lpCreateParams; SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pApp); pApp->hWnd = hwnd; }
        else { pApp = (MainApp*)GetWindowLongPtr(hwnd, GWLP_USERDATA); }
        if (pApp) return pApp->HandleMessage(msg, wp, lp);
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE:
            inputPanel.Create(hWnd, &dbManager);
            canvas.StartAnimation(); SetTimer(hWnd, TIMER_ANIM_ID, 16, NULL);
            return 0;
        case WM_TIMER:
            if (wp == TIMER_ANIM_ID) { if (canvas.UpdateAnimation()) InvalidateRect(hWnd, NULL, FALSE); else KillTimer(hWnd, TIMER_ANIM_ID); }
            return 0;
        case WM_CTLCOLORSTATIC: { HDC hdc = (HDC)wp; SetBkMode(hdc, TRANSPARENT); return (LRESULT)GetStockObject(WHITE_BRUSH); }
        case WM_APP_CAL_CHANGE:
            inputPanel.RefreshTransactionList(); InvalidateRect(hWnd, NULL, FALSE); return 0;
        case WM_APP_CAL_VIEW_CHANGE: {
            int year = (int)wp; int month = (int)lp;

            // ★修正: 現在のビューに合わせて適切なマークデータ（日/月/年）を再ロードする
            inputPanel.RefreshCalendarMarks();

            if (g_pCalInstance) g_pCalInstance->SetSelectedDate(year, month, 1);
            inputPanel.RefreshTransactionList();

            canvas.StartAnimation(); SetTimer(hWnd, TIMER_ANIM_ID, 16, NULL);
            InvalidateRect(hWnd, NULL, FALSE);
        } return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == 100) {
                std::wstring cat; float amt; int type;
                if (inputPanel.GetInput(cat, amt, type)) {
                    if (inputPanel.m_editingId == -1) dbManager.AddTransaction(inputPanel.GetSelectedDate(), cat, amt, type); else { dbManager.UpdateTransaction(inputPanel.m_editingId, cat, amt, type); inputPanel.CancelEditMode(); }
                    SetWindowText(inputPanel.hEditAmt, L""); inputPanel.UpdateCategoryList(); inputPanel.RefreshTransactionList(); inputPanel.RefreshCalendarMarks();
                    canvas.StartAnimation(); SetTimer(hWnd, TIMER_ANIM_ID, 16, NULL); InvalidateRect(hWnd, NULL, FALSE);
                }
                else MessageBox(hWnd, L"金額を正しく入力してください。", L"入力エラー", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (LOWORD(wp) == ID_BTN_SETTINGS) { SettingsWindow::Show(hWnd, &dbManager); inputPanel.UpdateCategoryList(); InvalidateRect(hWnd, NULL, FALSE); }
            if (LOWORD(wp) == 2001) { int idx = ListView_GetNextItem(inputPanel.hListTrans, -1, LVNI_SELECTED); if (idx != -1) { int id = (int)GetTransactionIdFromList(idx); std::wstring cat = GetTransactionTextFromList(idx, 0); float amt = (float)_wtof(GetTransactionTextFromList(idx, 1).c_str()); std::wstring typeStr = GetTransactionTextFromList(idx, 2); int type = (typeStr == L"収") ? TYPE_INCOME : TYPE_EXPENSE; inputPanel.StartEditMode(id, cat, amt, type); } }
            if (LOWORD(wp) == 2002) {
                int idx = ListView_GetNextItem(inputPanel.hListTrans, -1, LVNI_SELECTED);
                if (idx != -1 && MessageBox(hWnd, L"選択した明細を削除しますか？", L"確認", MB_YESNO | MB_ICONWARNING) == IDYES) {
                    int id = (int)GetTransactionIdFromList(idx); dbManager.DeleteTransaction(id); inputPanel.RefreshTransactionList(); inputPanel.RefreshCalendarMarks();
                    canvas.StartAnimation(); SetTimer(hWnd, TIMER_ANIM_ID, 16, NULL); InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            if (HIWORD(wp) == BN_CLICKED) {
                int id = LOWORD(wp);
                if (id == 400 || id == 401 || (id >= 101 && id <= 104)) {
                    if (id == 400 || id == 401) inputPanel.UpdateCategoryList();
                    canvas.StartAnimation(); SetTimer(hWnd, TIMER_ANIM_ID, 16, NULL); canvas.DrillUp(); InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            return 0;
        case WM_NOTIFY: { LPNMHDR pnm = (LPNMHDR)lp; if (pnm->hwndFrom == inputPanel.hListTrans && pnm->code == NM_RCLICK) { int idx = ListView_GetNextItem(inputPanel.hListTrans, -1, LVNI_SELECTED); if (idx != -1) { POINT pt; GetCursorPos(&pt); HMENU hMenu = CreatePopupMenu(); AppendMenu(hMenu, MF_STRING, 2001, L"修正"); AppendMenu(hMenu, MF_STRING, 2002, L"削除"); TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL); DestroyMenu(hMenu); } } return 0; }
        case WM_MOUSEMOVE: { int x = LOWORD(lp); int y = HIWORD(lp); canvas.UpdateMousePos(x, y); InvalidateRect(hWnd, NULL, FALSE); } return 0;
        case WM_LBUTTONDOWN: { std::wstring hovered = canvas.GetHoveredCategory(); if (!hovered.empty() && canvas.IsHoveredDrillable()) { canvas.DrillDown(hovered); SetTimer(hWnd, TIMER_ANIM_ID, 16, NULL); InvalidateRect(hWnd, NULL, FALSE); } } return 0;
        case WM_RBUTTONDOWN: { if (canvas.IsDrillingDown()) { canvas.DrillUp(); SetTimer(hWnd, TIMER_ANIM_ID, 16, NULL); InvalidateRect(hWnd, NULL, FALSE); } } return 0;
        case WM_SIZE: canvas.Resize(hWnd); InvalidateRect(hWnd, NULL, FALSE); return 0;
        case WM_PAINT: {
            SYSTEMTIME st; inputPanel.GetSelectedDateStruct(&st); ReportMode rMode = inputPanel.GetReportMode(); GraphType gType = inputPanel.GetGraphType(); int currentType = inputPanel.GetCurrentType(); wchar_t start[32], end[32], title[64];
            if (rMode == MODE_MONTHLY) { swprintf_s(start, L"%04d-%02d-01", st.wYear, st.wMonth); swprintf_s(end, L"%04d-%02d-31", st.wYear, st.wMonth); swprintf_s(title, L"%d年 %d月 の収支", st.wYear, st.wMonth); }
            else { swprintf_s(start, L"%04d-01-01", st.wYear); swprintf_s(end, L"%04d-12-31", st.wYear); swprintf_s(title, L"%d年 の収支", st.wYear); }
            canvas.Render(hWnd, dbManager, start, end, title, gType, rMode, currentType); ValidateRect(hWnd, NULL);
        } return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        } return DefWindowProc(hWnd, msg, wp, lp);
    }
};

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) { MainApp app; app.Run(h); return 0; }