#include <windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <ctime>
#include "sqlite3.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "legacy_stdio_definitions.lib")

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// -----------------------------------------------------------------------------
// 定数・構造体
// -----------------------------------------------------------------------------
enum ReportMode { MODE_MONTHLY, MODE_YEARLY };
enum GraphType { GRAPH_PIE, GRAPH_LINE };
enum TransType { TYPE_EXPENSE = 0, TYPE_INCOME = 1 };

struct TransactionSummary {
    std::wstring label;
    float amount;
    D2D1_COLOR_F color;
    bool hasChildren;
};

struct TimeSeriesData {
    int timeUnit;
    float income;
    float expense;
};

struct CategoryItem {
    int id;
    std::wstring name;
    int sortOrder;
    int parentId;
};

struct TransactionDetail {
    int id;
    std::wstring date;
    std::wstring category;
    float amount;
    int type;
};

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

// -----------------------------------------------------------------------------
// ドラッグ＆ドロップ用ヘルパー
// -----------------------------------------------------------------------------
class DragImageWindow {
    HWND hWindow;
public:
    DragImageWindow() : hWindow(NULL) {}

    void Show(HWND hList, int index, POINT ptCursor) {
        if (hWindow) Destroy();
        RECT rc;
        SendMessage(hList, LB_GETITEMRECT, index, (LPARAM)&rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        int len = (int)SendMessage(hList, LB_GETTEXTLEN, index, 0);
        std::vector<wchar_t> buf(len + 1);
        SendMessage(hList, LB_GETTEXT, index, (LPARAM)buf.data());

        WNDCLASS wc = { 0 };
        wc.lpfnWndProc = DefWindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"DragImageWnd";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClass(&wc);

        hWindow = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"DragImageWnd", NULL, WS_POPUP,
            ptCursor.x + 10, ptCursor.y + 10, width, height, NULL, NULL, wc.hInstance, NULL);

        HDC hdcScreen = GetDC(NULL);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hBm = CreateCompatibleBitmap(hdcScreen, width, height);
        HBITMAP hOldBm = (HBITMAP)SelectObject(hdcMem, hBm);

        RECT bgRc = { 0, 0, width, height };
        HBRUSH hBr = CreateSolidBrush(RGB(240, 240, 255));
        FillRect(hdcMem, &bgRc, hBr);
        DeleteObject(hBr);

        SetBkMode(hdcMem, TRANSPARENT);
        HFONT hFont = (HFONT)SendMessage(hList, WM_GETFONT, 0, 0);
        SelectObject(hdcMem, hFont);
        SetTextColor(hdcMem, RGB(0, 0, 0));

        RECT textRc = { 0, 0, width, height };
        OffsetRect(&textRc, 5, 0);
        DrawText(hdcMem, buf.data(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        HBRUSH frameBr = CreateSolidBrush(RGB(100, 100, 200));
        FrameRect(hdcMem, &textRc, frameBr);
        DeleteObject(frameBr);

        POINT ptSrc = { 0, 0 };
        SIZE wndSize = { width, height };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 180, 0 };
        UpdateLayeredWindow(hWindow, hdcScreen, NULL, &wndSize, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

        SelectObject(hdcMem, hOldBm);
        DeleteObject(hBm);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);

        ShowWindow(hWindow, SW_SHOWNA);
    }
    void Move(POINT ptCursor) {
        if (hWindow) {
            SetWindowPos(hWindow, NULL, ptCursor.x + 15, ptCursor.y + 15, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    void Destroy() {
        if (hWindow) { DestroyWindow(hWindow); hWindow = NULL; }
    }
};

WNDPROC g_OldListBoxProc = NULL;
WNDPROC g_OldEditProc = NULL;
int g_DragSourceIdx = -1;
int g_LastInsertIdx = -1;
bool g_IsDragging = false;
POINT g_DragStartPt = { 0 };
DragImageWindow g_DragImg;

void DrawInsertLine(HWND hwnd, int idx) {
    HDC hdc = GetDC(hwnd);
    RECT rc;
    int count = (int)SendMessage(hwnd, LB_GETCOUNT, 0, 0);
    if (idx >= 0 && idx < count) { SendMessage(hwnd, LB_GETITEMRECT, idx, (LPARAM)&rc); }
    else if (idx == count) {
        SendMessage(hwnd, LB_GETITEMRECT, count - 1, (LPARAM)&rc);
        rc.top = rc.bottom; rc.bottom += 2;
    }
    else { ReleaseDC(hwnd, hdc); return; }

    HPEN hPen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    MoveToEx(hdc, rc.left, rc.top, NULL);
    LineTo(hdc, rc.right, rc.top);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    ReleaseDC(hwnd, hdc);
}

LRESULT CALLBACK ListBoxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        DWORD res = (DWORD)SendMessage(hwnd, LB_ITEMFROMPOINT, 0, lp);
        if (HIWORD(res) == 0) {
            int idx = LOWORD(res);
            if (idx != -1) {
                g_DragSourceIdx = idx;
                g_DragStartPt.x = LOWORD(lp); g_DragStartPt.y = HIWORD(lp);
                g_IsDragging = false; g_LastInsertIdx = -1;
            }
        }
        break;
    }
    case WM_MOUSEMOVE: {
        if (g_DragSourceIdx != -1 && (wp & MK_LBUTTON)) {
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
            if (!g_IsDragging) {
                if (abs(pt.x - g_DragStartPt.x) > GetSystemMetrics(SM_CXDRAG) || abs(pt.y - g_DragStartPt.y) > GetSystemMetrics(SM_CYDRAG)) {
                    g_IsDragging = true; SetCapture(hwnd);
                    POINT ptScreen; GetCursorPos(&ptScreen);
                    g_DragImg.Show(hwnd, g_DragSourceIdx, ptScreen);
                }
            }
            else {
                POINT ptScreen; GetCursorPos(&ptScreen); g_DragImg.Move(ptScreen);
                DWORD res = (DWORD)SendMessage(hwnd, LB_ITEMFROMPOINT, 0, lp);
                int targetIdx = LOWORD(res);
                if (HIWORD(res) == 1) { int count = (int)SendMessage(hwnd, LB_GETCOUNT, 0, 0); targetIdx = count; }
                if (g_LastInsertIdx != targetIdx) { g_LastInsertIdx = targetIdx; InvalidateRect(hwnd, NULL, FALSE); UpdateWindow(hwnd); }
                if (g_LastInsertIdx != -1) DrawInsertLine(hwnd, g_LastInsertIdx);
            }
        }
        break;
    }
    case WM_LBUTTONUP: {
        if (g_IsDragging) {
            ReleaseCapture(); g_DragImg.Destroy();
            if (g_LastInsertIdx != -1 && g_LastInsertIdx != g_DragSourceIdx && g_LastInsertIdx != g_DragSourceIdx + 1) {
                SendMessage(GetParent(hwnd), WM_APP + 1, (WPARAM)g_DragSourceIdx, (LPARAM)g_LastInsertIdx);
            }
            InvalidateRect(hwnd, NULL, TRUE);
        }
        g_IsDragging = false; g_DragSourceIdx = -1; g_LastInsertIdx = -1;
        break;
    }
    case WM_PAINT: {
        LRESULT lRes = CallWindowProc(g_OldListBoxProc, hwnd, msg, wp, lp);
        if (g_IsDragging && g_LastInsertIdx != -1) DrawInsertLine(hwnd, g_LastInsertIdx);
        return lRes;
    }
    }
    return CallWindowProc(g_OldListBoxProc, hwnd, msg, wp, lp);
}

// -----------------------------------------------------------------------------
// 1. ExpenseManager (Model)
// -----------------------------------------------------------------------------
class ExpenseManager {
private:
    sqlite3* db;
    const char* DB_FILE = "kakeibo_v7.db";

public:
    ExpenseManager() : db(nullptr) {}
    ~ExpenseManager() { if (db) sqlite3_close(db); }

    void Initialize() {
        if (sqlite3_open(DB_FILE, &db) == SQLITE_OK) {
            sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS transactions (id INTEGER PRIMARY KEY, date TEXT, category TEXT, amount REAL, type INTEGER);", NULL, NULL, NULL);
            sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS categories (id INTEGER PRIMARY KEY, name TEXT, type INTEGER, sort_order INTEGER, parent_id INTEGER DEFAULT 0);", NULL, NULL, NULL);
            InitDefaultCategories();
        }
    }

    void InitDefaultCategories() {
        if (GetCategoryCount(TYPE_EXPENSE) == 0) {
            int fid = AddCategory(L"食費", TYPE_EXPENSE, 0);
            AddCategory(L"外食", TYPE_EXPENSE, fid);
            AddCategory(L"自炊", TYPE_EXPENSE, fid);
            AddCategory(L"交通費", TYPE_EXPENSE, 0);
            AddCategory(L"日用品", TYPE_EXPENSE, 0);
            AddCategory(L"住居費", TYPE_EXPENSE, 0);
        }
        if (GetCategoryCount(TYPE_INCOME) == 0) {
            AddCategory(L"給与", TYPE_INCOME, 0);
            AddCategory(L"賞与", TYPE_INCOME, 0);
        }
    }

    int GetCategoryCount(int type) {
        sqlite3_stmt* stmt;
        int count = 0;
        const char* sql = "SELECT COUNT(*) FROM categories WHERE type=?";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, type);
            if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return count;
    }

    int AddCategory(const std::wstring& name, int type, int parentId = 0) {
        sqlite3_stmt* check;
        char nameUtf8[256]; WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nameUtf8, 256, NULL, NULL);
        if (sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE name=? AND type=?", -1, &check, NULL) == SQLITE_OK) {
            sqlite3_bind_text(check, 1, nameUtf8, -1, SQLITE_STATIC);
            sqlite3_bind_int(check, 2, type);
            if (sqlite3_step(check) == SQLITE_ROW) {
                int id = sqlite3_column_int(check, 0);
                sqlite3_finalize(check);
                return id;
            }
        }
        sqlite3_finalize(check);

        int maxOrder = 0;
        sqlite3_stmt* ord;
        if (sqlite3_prepare_v2(db, "SELECT MAX(sort_order) FROM categories WHERE type=?", -1, &ord, NULL) == SQLITE_OK) {
            sqlite3_bind_int(ord, 1, type);
            if (sqlite3_step(ord) == SQLITE_ROW) maxOrder = sqlite3_column_int(ord, 0);
        }
        sqlite3_finalize(ord);

        sqlite3_stmt* ins;
        if (sqlite3_prepare_v2(db, "INSERT INTO categories (name, type, sort_order, parent_id) VALUES (?, ?, ?, ?)", -1, &ins, NULL) == SQLITE_OK) {
            sqlite3_bind_text(ins, 1, nameUtf8, -1, SQLITE_STATIC);
            sqlite3_bind_int(ins, 2, type);
            sqlite3_bind_int(ins, 3, maxOrder + 1);
            sqlite3_bind_int(ins, 4, parentId);
            sqlite3_step(ins);
        }
        sqlite3_finalize(ins);
        return (int)sqlite3_last_insert_rowid(db);
    }

    void UpdateCategory(int id, const std::wstring& newName, int newParentId) {
        std::wstring oldName = L"";
        sqlite3_stmt* q;
        if (sqlite3_prepare_v2(db, "SELECT name FROM categories WHERE id=?", -1, &q, NULL) == SQLITE_OK) {
            sqlite3_bind_int(q, 1, id);
            if (sqlite3_step(q) == SQLITE_ROW) {
                wchar_t buf[256];
                MultiByteToWideChar(CP_UTF8, 0, (const char*)sqlite3_column_text(q, 0), -1, buf, 256);
                oldName = buf;
            }
        }
        sqlite3_finalize(q);
        if (oldName.empty()) return;

        char nameUtf8[256]; WideCharToMultiByte(CP_UTF8, 0, newName.c_str(), -1, nameUtf8, 256, NULL, NULL);
        char oldNameUtf8[256]; WideCharToMultiByte(CP_UTF8, 0, oldName.c_str(), -1, oldNameUtf8, 256, NULL, NULL);

        sqlite3_stmt* upd;
        if (sqlite3_prepare_v2(db, "UPDATE categories SET name=?, parent_id=? WHERE id=?", -1, &upd, NULL) == SQLITE_OK) {
            sqlite3_bind_text(upd, 1, nameUtf8, -1, SQLITE_STATIC);
            sqlite3_bind_int(upd, 2, newParentId);
            sqlite3_bind_int(upd, 3, id);
            sqlite3_step(upd);
        }
        sqlite3_finalize(upd);

        if (newName != oldName) {
            sqlite3_stmt* trUpd;
            if (sqlite3_prepare_v2(db, "UPDATE transactions SET category=? WHERE category=?", -1, &trUpd, NULL) == SQLITE_OK) {
                sqlite3_bind_text(trUpd, 1, nameUtf8, -1, SQLITE_STATIC);
                sqlite3_bind_text(trUpd, 2, oldNameUtf8, -1, SQLITE_STATIC);
                sqlite3_step(trUpd);
            }
            sqlite3_finalize(trUpd);
        }
    }

    void DeleteCategory(int id) {
        sqlite3_stmt* upd;
        if (sqlite3_prepare_v2(db, "UPDATE categories SET parent_id=0 WHERE parent_id=?", -1, &upd, NULL) == SQLITE_OK) {
            sqlite3_bind_int(upd, 1, id);
            sqlite3_step(upd);
        }
        sqlite3_finalize(upd);
        sqlite3_stmt* del;
        if (sqlite3_prepare_v2(db, "DELETE FROM categories WHERE id=?", -1, &del, NULL) == SQLITE_OK) {
            sqlite3_bind_int(del, 1, id);
            sqlite3_step(del);
        }
        sqlite3_finalize(del);
    }

    std::vector<CategoryItem> GetCategories(int type) {
        std::vector<CategoryItem> all;
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT id, name, sort_order, parent_id FROM categories WHERE type=? ORDER BY sort_order ASC", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, type);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int id = sqlite3_column_int(stmt, 0);
                wchar_t name[256]; MultiByteToWideChar(CP_UTF8, 0, (const char*)sqlite3_column_text(stmt, 1), -1, name, 256);
                int ord = sqlite3_column_int(stmt, 2);
                int pid = sqlite3_column_int(stmt, 3);
                all.push_back({ id, name, ord, pid });
            }
        }
        sqlite3_finalize(stmt);

        std::vector<CategoryItem> sorted;
        for (const auto& p : all) {
            if (p.parentId == 0) {
                sorted.push_back(p);
                for (const auto& c : all) {
                    if (c.parentId == p.id) sorted.push_back(c);
                }
            }
        }
        for (const auto& c : all) {
            bool found = false;
            for (const auto& s : sorted) if (s.id == c.id) found = true;
            if (!found) sorted.push_back(c);
        }
        return sorted;
    }

    void SwapCategoryOrder(int id1, int order1, int id2, int order2) {
        char sql[256];
        sprintf_s(sql, "UPDATE categories SET sort_order=%d WHERE id=%d;", order2, id1); sqlite3_exec(db, sql, NULL, NULL, NULL);
        sprintf_s(sql, "UPDATE categories SET sort_order=%d WHERE id=%d;", order1, id2); sqlite3_exec(db, sql, NULL, NULL, NULL);
    }

    void AddTransaction(const std::wstring& date, const std::wstring& category, float amount, int type) {
        char catUtf8[256], dateUtf8[32];
        WideCharToMultiByte(CP_UTF8, 0, category.c_str(), -1, catUtf8, 256, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, date.c_str(), -1, dateUtf8, 32, NULL, NULL);
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "INSERT INTO transactions (date, category, amount, type) VALUES (?, ?, ?, ?);", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, dateUtf8, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, catUtf8, -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 3, amount);
        sqlite3_bind_int(stmt, 4, type);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        AddCategory(category, type, 0);
    }

    std::vector<TransactionDetail> GetTransactionsForDate(const std::wstring& date) {
        std::vector<TransactionDetail> result;
        char dateUtf8[32];
        WideCharToMultiByte(CP_UTF8, 0, date.c_str(), -1, dateUtf8, 32, NULL, NULL);

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT id, date, category, amount, type FROM transactions WHERE date = ? ORDER BY id ASC", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, dateUtf8, -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int id = sqlite3_column_int(stmt, 0);
                wchar_t wDate[32], wCat[256];
                MultiByteToWideChar(CP_UTF8, 0, (const char*)sqlite3_column_text(stmt, 1), -1, wDate, 32);
                MultiByteToWideChar(CP_UTF8, 0, (const char*)sqlite3_column_text(stmt, 2), -1, wCat, 256);
                float amt = (float)sqlite3_column_double(stmt, 3);
                int type = sqlite3_column_int(stmt, 4);
                result.push_back({ id, wDate, wCat, amt, type });
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    void DeleteTransaction(int id) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM transactions WHERE id = ?", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, id);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
    }

    void UpdateTransaction(int id, const std::wstring& category, float amount, int type) {
        char catUtf8[256];
        WideCharToMultiByte(CP_UTF8, 0, category.c_str(), -1, catUtf8, 256, NULL, NULL);
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "UPDATE transactions SET category=?, amount=?, type=? WHERE id=?", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, catUtf8, -1, SQLITE_STATIC);
            sqlite3_bind_double(stmt, 2, amount);
            sqlite3_bind_int(stmt, 3, type);
            sqlite3_bind_int(stmt, 4, id);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
    }

    std::vector<TransactionSummary> GetPieData(const std::wstring& startDate, const std::wstring& endDate, int type, const std::wstring& parentCategoryName) {
        std::vector<TransactionSummary> result;
        char sDate[32], eDate[32];
        WideCharToMultiByte(CP_UTF8, 0, startDate.c_str(), -1, sDate, 32, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, endDate.c_str(), -1, eDate, 32, NULL, NULL);

        std::map<std::wstring, std::wstring> childToParent;
        std::map<std::wstring, bool> isParent;
        auto cats = GetCategories(type);
        for (const auto& c : cats) {
            if (c.parentId != 0) {
                auto it = std::find_if(cats.begin(), cats.end(), [&](const CategoryItem& p) { return p.id == c.parentId; });
                if (it != cats.end()) { childToParent[c.name] = it->name; isParent[it->name] = true; }
            }
        }

        std::map<std::wstring, float> sums;
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT category, amount FROM transactions WHERE type=? AND date >= ? AND date <= ?", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, type);
            sqlite3_bind_text(stmt, 2, sDate, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, eDate, -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                wchar_t wCat[256]; MultiByteToWideChar(CP_UTF8, 0, (const char*)sqlite3_column_text(stmt, 0), -1, wCat, 256);
                float amt = (float)sqlite3_column_double(stmt, 1);
                std::wstring catName = wCat;
                std::wstring parent = childToParent.count(catName) ? childToParent[catName] : L"";
                if (parentCategoryName.empty()) {
                    if (!parent.empty()) sums[parent] += amt; else sums[catName] += amt;
                }
                else {
                    if (parent == parentCategoryName) sums[catName] += amt;
                }
            }
        }
        sqlite3_finalize(stmt);

        D2D1_COLOR_F palette[] = {
            {0.29f, 0.56f, 0.85f, 1.0f}, {0.92f, 0.30f, 0.30f, 1.0f}, {0.33f, 0.73f, 0.44f, 1.0f}, {0.95f, 0.77f, 0.06f, 1.0f},
            {0.61f, 0.40f, 0.80f, 1.0f}, {0.20f, 0.80f, 0.80f, 1.0f}, {0.90f, 0.50f, 0.20f, 1.0f}, {0.60f, 0.60f, 0.60f, 1.0f}
        };
        int idx = 0;
        for (auto const& [name, amount] : sums) {
            bool children = parentCategoryName.empty() && isParent[name];
            result.push_back({ name, amount, palette[idx % 8], children });
            idx++;
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.amount > b.amount; });
        return result;
    }

    std::vector<TimeSeriesData> GetLineData(const std::wstring& startDate, const std::wstring& endDate, ReportMode mode) {
        std::vector<TimeSeriesData> result;
        char sDate[32], eDate[32];
        WideCharToMultiByte(CP_UTF8, 0, startDate.c_str(), -1, sDate, 32, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, endDate.c_str(), -1, eDate, 32, NULL, NULL);
        std::string groupBy = (mode == MODE_MONTHLY) ? "SUBSTR(date, 9, 2)" : "SUBSTR(date, 6, 2)";
        std::string sql = "SELECT " + groupBy + ", type, SUM(amount) FROM transactions WHERE date >= ? AND date <= ? GROUP BY " + groupBy + ", type ORDER BY " + groupBy + ";";
        sqlite3_stmt* stmt;
        std::map<int, std::pair<float, float>> aggregation;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sDate, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, eDate, -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int timeVal = atoi((const char*)sqlite3_column_text(stmt, 0));
                int type = sqlite3_column_int(stmt, 1);
                float amt = (float)sqlite3_column_double(stmt, 2);
                if (type == TYPE_INCOME) aggregation[timeVal].first += amt; else aggregation[timeVal].second += amt;
            }
        }
        sqlite3_finalize(stmt);
        for (auto const& [time, val] : aggregation) result.push_back({ time, val.first, val.second });
        return result;
    }
};

// -----------------------------------------------------------------------------
// 2. ChartCanvas (View)
// -----------------------------------------------------------------------------
class ChartCanvas {
private:
    ID2D1Factory* pFactory;
    ID2D1HwndRenderTarget* pRT;
    ID2D1SolidColorBrush* pBrush;
    IDWriteFactory* pDWFactory;
    IDWriteTextFormat* pTxtFormat;
    IDWriteTextFormat* pTxtSmall;
    IDWriteTextFormat* pTxtLegend;
    D2D1_POINT_2F m_mousePos;
    wchar_t m_decimalSep[4];
    wchar_t m_thousandSep[4];
    std::wstring m_currentDrillParent;
    std::wstring m_lastHoveredName;

public:
    ChartCanvas() : pFactory(NULL), pRT(NULL), pBrush(NULL), pDWFactory(NULL), pTxtFormat(NULL), pTxtSmall(NULL), pTxtLegend(NULL) {
        m_mousePos = D2D1::Point2F(-1, -1);
        GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, m_decimalSep, 4);
        GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, m_thousandSep, 4);
    }
    ~ChartCanvas() {
        if (pRT) pRT->Release(); if (pBrush) pBrush->Release(); if (pFactory) pFactory->Release();
        if (pTxtFormat) pTxtFormat->Release(); if (pTxtSmall) pTxtSmall->Release(); if (pTxtLegend) pTxtLegend->Release();
        if (pDWFactory) pDWFactory->Release();
    }

    void Initialize() {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pFactory);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&pDWFactory);
        pDWFactory->CreateTextFormat(L"Meiryo UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"ja-jp", &pTxtFormat);
        pDWFactory->CreateTextFormat(L"Meiryo UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"ja-jp", &pTxtSmall);
        pDWFactory->CreateTextFormat(L"Meiryo UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"ja-jp", &pTxtLegend);
        pTxtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void Resize(HWND hwnd) {
        if (pRT) { RECT rc; GetClientRect(hwnd, &rc); pRT->Resize(D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)); }
    }
    void UpdateMousePos(int x, int y) { m_mousePos = D2D1::Point2F((float)x, (float)y); }
    std::wstring GetHoveredCategory() { return m_lastHoveredName; }
    void DrillDown(const std::wstring& parentName) { m_currentDrillParent = parentName; }
    void DrillUp() { m_currentDrillParent = L""; }

    std::wstring FormatMoney(float amount) {
        wchar_t plainNum[64]; swprintf_s(plainNum, L"%.0f", amount);
        NUMBERFMTW fmt = { 0 }; fmt.NumDigits = 0; fmt.LeadingZero = 0; fmt.Grouping = 3; fmt.lpDecimalSep = m_decimalSep; fmt.lpThousandSep = m_thousandSep; fmt.NegativeOrder = 1;
        wchar_t out[64];
        if (GetNumberFormatW(LOCALE_USER_DEFAULT, 0, plainNum, &fmt, out, 64) > 0) return std::wstring(out);
        return std::wstring(plainNum);
    }

    void DrawTooltip(const wchar_t* text, D2D1_SIZE_F size) {
        float tipW = 140.0f; float tipH = 50.0f;
        float tipX = m_mousePos.x + 20; float tipY = m_mousePos.y - tipH - 5;
        if (tipY < 0) tipY = m_mousePos.y + 20;
        if (tipX + tipW > size.width) tipX = m_mousePos.x - tipW - 10;
        D2D1_RECT_F tipRect = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);

        pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.2f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tipX + 3, tipY + 3, tipX + tipW + 3, tipY + tipH + 3), 5, 5), pBrush);
        pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(tipRect, 5, 5), pBrush);
        pBrush->SetColor(D2D1::ColorF(0.4f, 0.4f, 0.4f));
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(tipRect, 5, 5), pBrush, 1.0f);
        pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
        pRT->DrawText(text, (UINT32)wcslen(text), pTxtSmall, tipRect, pBrush);
    }

    void Render(HWND hwnd, ExpenseManager& db, const std::wstring& start, const std::wstring& end, std::wstring title, GraphType gType, ReportMode rMode, int currentType) {
        if (!pFactory) return;
        RECT rc; GetClientRect(hwnd, &rc);
        if (!pRT) { pFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)), &pRT); pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &pBrush); }
        pRT->BeginDraw();
        pRT->Clear(D2D1::ColorF(1.0f, 1.0f, 1.0f));
        float sidebarWidth = 280.0f;
        D2D1_SIZE_F size = pRT->GetSize();

        if (!m_currentDrillParent.empty()) {
            title += L" (" + m_currentDrillParent + L")";
            pBrush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f));
            pRT->DrawText(L"※右クリックで戻る", 9, pTxtSmall, D2D1::RectF(sidebarWidth + 20, 50, size.width, 70), pBrush);
        }
        pBrush->SetColor(D2D1::ColorF(0.2f, 0.2f, 0.2f));
        pRT->DrawText(title.c_str(), (UINT32)title.length(), pTxtFormat, D2D1::RectF(sidebarWidth + 20, 20, size.width, 50), pBrush);

        if (gType == GRAPH_PIE) {
            auto data = db.GetPieData(start, end, currentType, m_currentDrillParent);
            DrawPieChart(data, sidebarWidth, size, currentType);
        }
        else {
            auto data = db.GetLineData(start, end, rMode);
            DrawLineChart(data, sidebarWidth, size, rMode);
        }
        pRT->EndDraw();
    }

private:
    void DrawPieChart(const std::vector<TransactionSummary>& data, float sidebarWidth, D2D1_SIZE_F size, int type) {
        float legendWidth = 200.0f;
        float chartAreaWidth = size.width - sidebarWidth - legendWidth;
        if (chartAreaWidth < 100.0f) { legendWidth = 0.0f; chartAreaWidth = size.width - sidebarWidth; }
        D2D1_POINT_2F center = D2D1::Point2F(sidebarWidth + chartAreaWidth / 2.0f, size.height / 2.0f);
        float radius = min(chartAreaWidth, size.height) * 0.35f;
        if (radius < 1.0f) return;

        float total = 0; for (const auto& d : data) total += d.amount;
        const TransactionSummary* pHoveredItem = nullptr;
        float hoveredPercentage = 0.0f;
        m_lastHoveredName = L"";

        if (total > 0) {
            float startAngle = -90.0f;
            float dx = m_mousePos.x - center.x; float dy = m_mousePos.y - center.y;
            float dist = sqrt(dx * dx + dy * dy);
            float mouseAngle = atan2(dy, dx) * 180.0f / PI; if (mouseAngle < -90.0f) mouseAngle += 360.0f;

            for (const auto& d : data) {
                float sweepAngle = (d.amount / total) * 360.0f;
                bool isHovered = false;
                if (dist <= radius) {
                    float checkEnd = startAngle + sweepAngle;
                    if (mouseAngle >= startAngle && mouseAngle < checkEnd) {
                        isHovered = true; pHoveredItem = &d; m_lastHoveredName = d.label; hoveredPercentage = (d.amount / total) * 100.0f;
                    }
                }
                ID2D1PathGeometry* pPath = NULL; pFactory->CreatePathGeometry(&pPath);
                ID2D1GeometrySink* pSink = NULL; pPath->Open(&pSink);
                pSink->BeginFigure(center, D2D1_FIGURE_BEGIN_FILLED);
                float radStart = startAngle * (PI / 180.0f);
                float radEnd = (startAngle + sweepAngle) * (PI / 180.0f);
                float drawRadius = isHovered ? radius * 1.05f : radius;

                if (sweepAngle >= 360.0f) {
                    pSink->AddLine(D2D1::Point2F(center.x + drawRadius * cos(radStart), center.y + drawRadius * sin(radStart)));
                    pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(center.x + drawRadius * cos(radStart + PI), center.y + drawRadius * sin(radStart + PI)), D2D1::SizeF(drawRadius, drawRadius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
                    pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(center.x + drawRadius * cos(radEnd), center.y + drawRadius * sin(radEnd)), D2D1::SizeF(drawRadius, drawRadius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
                }
                else {
                    pSink->AddLine(D2D1::Point2F(center.x + drawRadius * cos(radStart), center.y + drawRadius * sin(radStart)));
                    pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(center.x + drawRadius * cos(radEnd), center.y + drawRadius * sin(radEnd)), D2D1::SizeF(drawRadius, drawRadius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, sweepAngle > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
                }
                pSink->EndFigure(D2D1_FIGURE_END_CLOSED); pSink->Close();
                pBrush->SetColor(d.color); pRT->FillGeometry(pPath, pBrush);
                pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f)); pRT->DrawGeometry(pPath, pBrush, 2.0f);
                pPath->Release(); pSink->Release();
                startAngle += sweepAngle;
            }

            if (legendWidth > 0) {
                float legendX = sidebarWidth + chartAreaWidth + 20; float legendY = 100.0f;
                pBrush->SetColor(D2D1::ColorF(0.2f, 0.2f, 0.2f));
                wchar_t legendTitle[32]; swprintf_s(legendTitle, L"【%s内訳】", (type == TYPE_INCOME) ? L"収入" : L"支出");
                pRT->DrawText(legendTitle, (UINT32)wcslen(legendTitle), pTxtLegend, D2D1::RectF(legendX, legendY - 25, size.width, size.height), pBrush);
                for (const auto& d : data) {
                    pBrush->SetColor(d.color); pRT->FillRectangle(D2D1::RectF(legendX, legendY + 2, legendX + 16, legendY + 18), pBrush);
                    std::wstring label = d.label;
                    if (d.hasChildren) label += L" (+)";
                    pBrush->SetColor(D2D1::ColorF(0.1f, 0.1f, 0.1f));
                    pRT->DrawText(label.c_str(), (UINT32)label.length(), pTxtLegend, D2D1::RectF(legendX + 25, legendY, size.width, legendY + 20), pBrush);
                    legendY += 25.0f;
                }
            }
            std::wstring moneyStr = FormatMoney(total);
            wchar_t totalStr[64]; swprintf_s(totalStr, L"%s合計: ¥%s", (type == TYPE_INCOME) ? L"収入" : L"支出", moneyStr.c_str());
            pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f));
            pRT->DrawText(totalStr, (UINT32)wcslen(totalStr), pTxtFormat, D2D1::RectF(center.x - 80, center.y + radius + 30, size.width, size.height), pBrush);

            if (pHoveredItem) {
                std::wstring tipMoney = FormatMoney(pHoveredItem->amount);
                wchar_t tipText[128];
                std::wstring hint = pHoveredItem->hasChildren ? L"\n(クリックで詳細)" : L"";
                swprintf_s(tipText, L"%s\n¥%s (%.1f%%)%s", pHoveredItem->label.c_str(), tipMoney.c_str(), hoveredPercentage, hint.c_str());
                DrawTooltip(tipText, size);
            }
        }
        else {
            pBrush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f));
            wchar_t msg[64]; swprintf_s(msg, L"%sデータなし", (type == TYPE_INCOME) ? L"収入" : L"支出");
            pRT->DrawText(msg, (UINT32)wcslen(msg), pTxtFormat, D2D1::RectF(center.x - 50, center.y, size.width, size.height), pBrush);
        }
    }

    void DrawLineChart(const std::vector<TimeSeriesData>& data, float sidebarWidth, D2D1_SIZE_F size, ReportMode rMode) {
        float left = sidebarWidth + 80; float right = size.width - 50; float top = 80; float bottom = size.height - 50;
        float width = right - left; float height = bottom - top;
        if (width <= 0 || height <= 0) return;
        float maxVal = 1000.0f;
        for (const auto& d : data) { if (d.income > maxVal) maxVal = d.income; if (d.expense > maxVal) maxVal = d.expense; }
        float magnitude = (float)pow(10, floor(log10(maxVal))); maxVal = ceil(maxVal / magnitude) * magnitude; if (maxVal == 0) maxVal = 1000;

        int divY = 5;
        pTxtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        for (int i = 0; i <= divY; i++) {
            float val = maxVal * i / divY; float y = bottom - (val / maxVal) * height;
            pBrush->SetColor(D2D1::ColorF(0.9f, 0.9f, 0.9f)); pRT->DrawLine(D2D1::Point2F(left, y), D2D1::Point2F(right, y), pBrush, 1.0f);
            std::wstring labelStr = FormatMoney(val);
            pBrush->SetColor(D2D1::ColorF(0.4f, 0.4f, 0.4f)); pRT->DrawText(labelStr.c_str(), (UINT32)labelStr.length(), pTxtSmall, D2D1::RectF(sidebarWidth, y - 8, left - 5, y + 8), pBrush);
        }
        pRT->DrawText(L"(円)", 3, pTxtSmall, D2D1::RectF(sidebarWidth, top - 20, left - 5, top), pBrush);

        int maxTime = (rMode == MODE_MONTHLY) ? 31 : 12;
        float stepX = width / (float)maxTime;
        pTxtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pBrush->SetColor(D2D1::ColorF(0.6f, 0.6f, 0.6f));
        pRT->DrawLine(D2D1::Point2F(left, bottom), D2D1::Point2F(right, bottom), pBrush, 2.0f); pRT->DrawLine(D2D1::Point2F(left, top), D2D1::Point2F(left, bottom), pBrush, 2.0f);
        for (int i = 1; i <= maxTime; i++) {
            float x = left + (i - 1) * stepX;
            bool showLabel = (rMode == MODE_YEARLY) || (i == 1 || i % 5 == 0 || i == maxTime);
            if (showLabel) {
                pBrush->SetColor(D2D1::ColorF(0.6f, 0.6f, 0.6f)); pRT->DrawLine(D2D1::Point2F(x, bottom), D2D1::Point2F(x, bottom + 5), pBrush, 1.0f);
                wchar_t buf[16]; swprintf_s(buf, L"%d", i); pRT->DrawText(buf, (UINT32)wcslen(buf), pTxtSmall, D2D1::RectF(x - 15, bottom + 5, x + 15, bottom + 25), pBrush);
            }
        }
        wchar_t unit[8]; wcscpy_s(unit, (rMode == MODE_MONTHLY) ? L"(日)" : L"(月)"); pRT->DrawText(unit, (UINT32)wcslen(unit), pTxtSmall, D2D1::RectF(right + 5, bottom + 5, right + 40, bottom + 25), pBrush);

        if (data.empty()) return;
        std::vector<float> incMap(maxTime + 1, 0), expMap(maxTime + 1, 0);
        for (const auto& d : data) { if (d.timeUnit >= 1 && d.timeUnit <= maxTime) { incMap[d.timeUnit] = d.income; expMap[d.timeUnit] = d.expense; } }

        struct HitPoint { float x, y, val; int time; bool isInc; D2D1_COLOR_F color; };
        HitPoint bestHit = { 0 }; bool isHit = false; float minHitDist = 15.0f;

        auto ProcessPolyLine = [&](const std::vector<float>& values, D2D1_COLOR_F color, bool isInc) {
            ID2D1PathGeometry* pPath = NULL; pFactory->CreatePathGeometry(&pPath); ID2D1GeometrySink* pSink = NULL; pPath->Open(&pSink);
            bool first = true;
            for (int i = 1; i <= maxTime; i++) {
                float x = left + (i - 1) * stepX; float val = values[i]; float y = bottom - (val / maxVal) * height;
                if (first) { pSink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_HOLLOW); first = false; }
                else { pSink->AddLine(D2D1::Point2F(x, y)); }
                pBrush->SetColor(color); pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 3.0f, 3.0f), pBrush);
                float dx = m_mousePos.x - x; float dy = m_mousePos.y - y; float dist = sqrt(dx * dx + dy * dy);
                if (dist < minHitDist) { minHitDist = dist; bestHit = { x, y, val, i, isInc, color }; isHit = true; }
            }
            pSink->EndFigure(D2D1_FIGURE_END_OPEN); pSink->Close();
            pBrush->SetColor(color); pRT->DrawGeometry(pPath, pBrush, 2.0f); pPath->Release(); pSink->Release();
            };
        ProcessPolyLine(incMap, D2D1::ColorF(0.2f, 0.6f, 0.9f), true); ProcessPolyLine(expMap, D2D1::ColorF(0.9f, 0.4f, 0.4f), false);

        pTxtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        pBrush->SetColor(D2D1::ColorF(0.2f, 0.6f, 0.9f)); pRT->DrawText(L"■ 収入", 4, pTxtFormat, D2D1::RectF(right - 100, top - 30, right, top), pBrush);
        pBrush->SetColor(D2D1::ColorF(0.9f, 0.4f, 0.4f)); pRT->DrawText(L"■ 支出", 4, pTxtFormat, D2D1::RectF(right - 50, top - 30, right + 50, top), pBrush);

        if (isHit) {
            pBrush->SetColor(bestHit.color); pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bestHit.x, bestHit.y), 6.0f, 6.0f), pBrush);
            pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f)); pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(bestHit.x, bestHit.y), 6.0f, 6.0f), pBrush, 2.0f);
            std::wstring hitValStr = FormatMoney(bestHit.val); wchar_t tipText[128]; wchar_t unit[4]; wcscpy_s(unit, (rMode == MODE_MONTHLY) ? L"日" : L"月");
            swprintf_s(tipText, L"%d%s (%s)\n¥%s", bestHit.time, unit, bestHit.isInc ? L"収入" : L"支出", hitValStr.c_str());
            DrawTooltip(tipText, size);
        }
    }
};

// -----------------------------------------------------------------------------
// 3. Settings Window
// -----------------------------------------------------------------------------
class SettingsWindow {
public:
    static HWND hList, hRadioExp, hRadioInc, hEditNew, hComboParent;
    static ExpenseManager* pDB;
    static int currentType;
    static std::vector<CategoryItem> s_currentItems;

    static void Show(HWND hParent, ExpenseManager* db) {
        pDB = db; currentType = TYPE_EXPENSE;
        WNDCLASS wc = { 0 }; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandle(NULL); wc.lpszClassName = L"SettingsWnd"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClass(&wc);

        int w = 420; int h = 600;
        int x = 150; int y = 150;
        if (hParent) { RECT rc; GetWindowRect(hParent, &rc); x = rc.left + (rc.right - rc.left - w) / 2; y = rc.top + (rc.bottom - rc.top - h) / 2; }

        HWND hWnd = CreateWindow(L"SettingsWnd", L"カテゴリ編集・削除", WS_VISIBLE | WS_SYSMENU | WS_CAPTION | WS_POPUPWINDOW,
            x, y, w, h, hParent, NULL, wc.hInstance, NULL);

        EnableWindow(hParent, FALSE);
        MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); break; } TranslateMessage(&msg); DispatchMessage(&msg); if (!IsWindow(hWnd)) break; }
        EnableWindow(hParent, TRUE); SetForegroundWindow(hParent);
    }

    static void RefreshList(int selectId = -1) {
        SendMessage(hList, LB_RESETCONTENT, 0, 0); SendMessage(hComboParent, CB_RESETCONTENT, 0, 0);
        int idx = (int)SendMessage(hComboParent, CB_ADDSTRING, 0, (LPARAM)L"(親なし:トップ)"); SendMessage(hComboParent, CB_SETITEMDATA, idx, 0);
        s_currentItems = pDB->GetCategories(currentType);
        int listSelectIdx = -1;
        for (const auto& item : s_currentItems) {
            std::wstring disp = item.name; if (item.parentId != 0) disp = L"    " + disp;
            idx = (int)SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)disp.c_str()); SendMessage(hList, LB_SETITEMDATA, idx, (LPARAM)item.id);
            if (item.id == selectId) listSelectIdx = idx;
            if (item.parentId == 0) { idx = (int)SendMessage(hComboParent, CB_ADDSTRING, 0, (LPARAM)item.name.c_str()); SendMessage(hComboParent, CB_SETITEMDATA, idx, (LPARAM)item.id); }
        }
        if (listSelectIdx != -1) { SendMessage(hList, LB_SETCURSEL, listSelectIdx, 0); SendMessage(GetParent(hList), WM_COMMAND, MAKEWPARAM(ID_LIST_CATS, LBN_SELCHANGE), (LPARAM)hList); }
        else { SetWindowText(hEditNew, L""); SendMessage(hComboParent, CB_SETCURSEL, 0, 0); }
    }

    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE:
            CreateWindow(L"BUTTON", L"区分", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 10, 380, 50, hwnd, NULL, NULL, NULL);
            hRadioExp = CreateWindow(L"BUTTON", L"支出", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 20, 30, 80, 20, hwnd, (HMENU)400, NULL, NULL);
            hRadioInc = CreateWindow(L"BUTTON", L"収入", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 110, 30, 80, 20, hwnd, (HMENU)401, NULL, NULL);
            SendMessage(hRadioExp, BM_SETCHECK, BST_CHECKED, 0);
            hList = CreateWindow(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 10, 70, 250, 300, hwnd, (HMENU)ID_LIST_CATS, NULL, NULL);
            g_OldListBoxProc = (WNDPROC)SetWindowLongPtr(hList, GWLP_WNDPROC, (LONG_PTR)ListBoxProc);
            CreateWindow(L"BUTTON", L"↑", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 270, 150, 40, 40, hwnd, (HMENU)ID_BTN_UP, NULL, NULL);
            CreateWindow(L"BUTTON", L"↓", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 270, 200, 40, 40, hwnd, (HMENU)ID_BTN_DOWN, NULL, NULL);
            CreateWindow(L"BUTTON", L"編集 / 追加", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 380, 380, 160, hwnd, NULL, NULL, NULL);
            CreateWindow(L"STATIC", L"名前:", WS_CHILD | WS_VISIBLE, 20, 405, 40, 20, hwnd, NULL, NULL, NULL);
            hEditNew = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 70, 400, 150, 25, hwnd, (HMENU)ID_EDIT_NEW_CAT, NULL, NULL);
            CreateWindow(L"STATIC", L"親:", WS_CHILD | WS_VISIBLE, 230, 405, 30, 20, hwnd, NULL, NULL, NULL);
            hComboParent = CreateWindow(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 260, 400, 120, 200, hwnd, (HMENU)ID_COMBO_PARENT, NULL, NULL);
            CreateWindow(L"BUTTON", L"新規追加", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 440, 100, 30, hwnd, (HMENU)ID_BTN_ADD_CAT, NULL, NULL);
            CreateWindow(L"BUTTON", L"変更を保存", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 130, 440, 100, 30, hwnd, (HMENU)ID_BTN_UPDATE_CAT, NULL, NULL);
            CreateWindow(L"BUTTON", L"削除", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 240, 440, 100, 30, hwnd, (HMENU)ID_BTN_DELETE_CAT, NULL, NULL);
            { HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Meiryo UI"); SendMessage(hList, WM_SETFONT, (WPARAM)hFont, TRUE); }
            RefreshList();
            return 0;
        case WM_APP + 1: {
            int srcIdx = (int)wp; int dstIdx = (int)lp;
            if (srcIdx < 0 || dstIdx < 0 || dstIdx == srcIdx + 1) return 0;
            if (dstIdx > srcIdx) { for (int i = srcIdx; i < dstIdx - 1; i++) { CategoryItem& a = s_currentItems[i]; CategoryItem& b = s_currentItems[i + 1]; pDB->SwapCategoryOrder(a.id, a.sortOrder, b.id, b.sortOrder); std::swap(s_currentItems[i], s_currentItems[i + 1]); } }
            else { for (int i = srcIdx; i > dstIdx; i--) { CategoryItem& a = s_currentItems[i]; CategoryItem& b = s_currentItems[i - 1]; pDB->SwapCategoryOrder(a.id, a.sortOrder, b.id, b.sortOrder); std::swap(s_currentItems[i], s_currentItems[i - 1]); } }
            RefreshList();
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
            if (LOWORD(wp) == ID_LIST_CATS && HIWORD(wp) == LBN_SELCHANGE) {
                int idx = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                if (idx != LB_ERR) {
                    int id = (int)SendMessage(hList, LB_GETITEMDATA, idx, 0);
                    auto it = std::find_if(s_currentItems.begin(), s_currentItems.end(), [&](const CategoryItem& item) { return item.id == id; });
                    if (it != s_currentItems.end()) {
                        SetWindowText(hEditNew, it->name.c_str());
                        int cnt = (int)SendMessage(hComboParent, CB_GETCOUNT, 0, 0);
                        for (int i = 0; i < cnt; i++) { int pid = (int)SendMessage(hComboParent, CB_GETITEMDATA, i, 0); if (pid == it->parentId) { SendMessage(hComboParent, CB_SETCURSEL, i, 0); break; } }
                    }
                }
            }
            if (LOWORD(wp) == 400 || LOWORD(wp) == 401) { currentType = (LOWORD(wp) == 401) ? TYPE_INCOME : TYPE_EXPENSE; RefreshList(); }
            if (LOWORD(wp) == ID_BTN_UP) MoveItem(-1);
            if (LOWORD(wp) == ID_BTN_DOWN) MoveItem(1);
            if (LOWORD(wp) == ID_BTN_ADD_CAT) {
                wchar_t buf[256]; GetWindowText(hEditNew, buf, 256);
                if (wcslen(buf) > 0) {
                    int pIdx = (int)SendMessage(hComboParent, CB_GETCURSEL, 0, 0);
                    int parentId = (pIdx != CB_ERR) ? (int)SendMessage(hComboParent, CB_GETITEMDATA, pIdx, 0) : 0;
                    pDB->AddCategory(buf, currentType, parentId); RefreshList();
                }
            }
            if (LOWORD(wp) == ID_BTN_UPDATE_CAT) {
                int listIdx = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                if (listIdx != LB_ERR) {
                    int id = (int)SendMessage(hList, LB_GETITEMDATA, listIdx, 0);
                    wchar_t buf[256]; GetWindowText(hEditNew, buf, 256);
                    if (wcslen(buf) > 0) {
                        int pIdx = (int)SendMessage(hComboParent, CB_GETCURSEL, 0, 0);
                        int parentId = (pIdx != CB_ERR) ? (int)SendMessage(hComboParent, CB_GETITEMDATA, pIdx, 0) : 0;
                        if (parentId != id) { pDB->UpdateCategory(id, buf, parentId); RefreshList(id); }
                        else { MessageBox(hwnd, L"自分自身を親カテゴリには設定できません。", L"エラー", MB_OK | MB_ICONERROR); }
                    }
                }
            }
            if (LOWORD(wp) == ID_BTN_DELETE_CAT) {
                int listIdx = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                if (listIdx != LB_ERR) {
                    if (MessageBox(hwnd, L"選択したカテゴリを削除しますか？", L"確認", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        int id = (int)SendMessage(hList, LB_GETITEMDATA, listIdx, 0); pDB->DeleteCategory(id); RefreshList();
                    }
                }
            }
            return 0;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        }
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    static void MoveItem(int dir) {
        int listIdx = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
        if (listIdx == LB_ERR) return;
        int currentId = (int)SendMessage(hList, LB_GETITEMDATA, listIdx, 0);
        int currentVecIdx = -1;
        for (int i = 0; i < (int)s_currentItems.size(); i++) { if (s_currentItems[i].id == currentId) { currentVecIdx = i; break; } }
        if (currentVecIdx == -1) return;
        CategoryItem& current = s_currentItems[currentVecIdx];
        int targetVecIdx = -1;
        if (dir < 0) {
            for (int i = currentVecIdx - 1; i >= 0; i--) {
                bool sameParent = (s_currentItems[i].parentId == current.parentId);
                bool bothAreRoots = (s_currentItems[i].parentId == 0 && current.parentId == 0);
                if (sameParent || bothAreRoots) { targetVecIdx = i; break; }
            }
        }
        else {
            for (int i = currentVecIdx + 1; i < (int)s_currentItems.size(); i++) {
                bool sameParent = (s_currentItems[i].parentId == current.parentId);
                bool bothAreRoots = (s_currentItems[i].parentId == 0 && current.parentId == 0);
                if (sameParent || bothAreRoots) { targetVecIdx = i; break; }
            }
        }
        if (targetVecIdx != -1) {
            CategoryItem& target = s_currentItems[targetVecIdx];
            pDB->SwapCategoryOrder(current.id, current.sortOrder, target.id, target.sortOrder);
            RefreshList(currentId);
        }
    }
};
HWND SettingsWindow::hList = NULL;
HWND SettingsWindow::hRadioExp = NULL;
HWND SettingsWindow::hRadioInc = NULL;
HWND SettingsWindow::hEditNew = NULL;
HWND SettingsWindow::hComboParent = NULL;
ExpenseManager* SettingsWindow::pDB = nullptr;
int SettingsWindow::currentType = TYPE_EXPENSE;
std::vector<CategoryItem> SettingsWindow::s_currentItems;

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_GETDLGCODE) {
        LRESULT result = CallWindowProc(g_OldEditProc, hwnd, msg, wp, lp);
        if (lp) {
            MSG* pMsg = (MSG*)lp;
            if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN) {
                return result | DLGC_WANTALLKEYS;
            }
        }
        return result;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(100, BN_CLICKED), (LPARAM)GetDlgItem(GetParent(hwnd), 100));
        return 0;
    }
    return CallWindowProc(g_OldEditProc, hwnd, msg, wp, lp);
}

// -----------------------------------------------------------------------------
// InputPanel (View/Controller)
// -----------------------------------------------------------------------------
class InputPanel {
public:
    HWND hCalendar, hComboCat, hEditAmt, hBtnAdd, hBtnSettings;
    HWND hRadioMonth, hRadioYear, hRadioPie, hRadioLine;
    HWND hRadioExp, hRadioInc;
    HWND hListTrans;
    ExpenseManager* pDB;
    int m_editingId;

    InputPanel() : m_editingId(-1) {}

    void Create(HWND parent, ExpenseManager* db) {
        pDB = db;
        // Tab移動のため WS_TABSTOP を追加
        hCalendar = CreateWindowEx(0, MONTHCAL_CLASS, L"", WS_CHILD | WS_VISIBLE | MCS_NOTODAYCIRCLE | WS_TABSTOP, 10, 10, 240, 160, parent, (HMENU)200, NULL, NULL);

        int y = 180;
        CreateWindow(L"BUTTON", L"区分", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, y, 220, 50, parent, NULL, NULL, NULL);

        // ラジオボタンの先頭に WS_TABSTOP (WS_GROUPがあるためグループ間移動は矢印キー)
        hRadioExp = CreateWindow(L"BUTTON", L"支出", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
            20, y + 20, 80, 20, parent, (HMENU)400, NULL, NULL);

        hRadioInc = CreateWindow(L"BUTTON", L"収入", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            110, y + 20, 80, 20, parent, (HMENU)401, NULL, NULL);

        SendMessage(hRadioExp, BM_SETCHECK, BST_CHECKED, 0);

        y += 60;
        CreateWindow(L"STATIC", L"カテゴリ:", WS_CHILD | WS_VISIBLE, 20, y, 200, 20, parent, NULL, NULL, NULL);
        hComboCat = CreateWindowEx(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_TABSTOP, 20, y + 20, 160, 200, parent, NULL, NULL, NULL);
        hBtnSettings = CreateWindow(L"BUTTON", L"設定", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 190, y + 19, 40, 25, parent, (HMENU)ID_BTN_SETTINGS, NULL, NULL);

        y += 50;
        CreateWindow(L"STATIC", L"金額:", WS_CHILD | WS_VISIBLE, 20, y, 200, 20, parent, NULL, NULL, NULL);
        hEditAmt = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 20, y + 20, 210, 25, parent, NULL, NULL, NULL);
        g_OldEditProc = (WNDPROC)SetWindowLongPtr(hEditAmt, GWLP_WNDPROC, (LONG_PTR)EditProc);

        y += 50;
        hBtnAdd = CreateWindow(L"BUTTON", L"登録", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 20, y, 210, 30, parent, (HMENU)100, NULL, NULL);

        y += 40;
        CreateWindow(L"STATIC", L"【選択日の明細】(右クリックで操作)", WS_CHILD | WS_VISIBLE, 10, y, 240, 20, parent, NULL, NULL, NULL);
        hListTrans = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
            10, y + 20, 240, 150, parent, (HMENU)105, NULL, NULL);
        ListView_SetExtendedListViewStyle(hListTrans, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMN lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt = LVCFMT_LEFT; lvc.cx = 100; lvc.pszText = (LPWSTR)L"カテゴリ";
        ListView_InsertColumn(hListTrans, 0, &lvc);
        lvc.fmt = LVCFMT_RIGHT; lvc.cx = 80; lvc.pszText = (LPWSTR)L"金額";
        ListView_InsertColumn(hListTrans, 1, &lvc);
        lvc.fmt = LVCFMT_CENTER; lvc.cx = 40; lvc.pszText = (LPWSTR)L"種別";
        ListView_InsertColumn(hListTrans, 2, &lvc);

        y += 180;
        CreateWindow(L"BUTTON", L"集計設定", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, y, 240, 80, parent, NULL, NULL, NULL);
        hRadioMonth = CreateWindow(L"BUTTON", L"月間", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 20, y + 20, 60, 25, parent, (HMENU)101, NULL, NULL);
        hRadioYear = CreateWindow(L"BUTTON", L"年間", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 90, y + 20, 60, 25, parent, (HMENU)102, NULL, NULL);
        SendMessage(hRadioMonth, BM_SETCHECK, BST_CHECKED, 0);

        hRadioPie = CreateWindow(L"BUTTON", L"円グラフ", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 20, y + 45, 80, 25, parent, (HMENU)103, NULL, NULL);
        hRadioLine = CreateWindow(L"BUTTON", L"推移", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 110, y + 45, 60, 25, parent, (HMENU)104, NULL, NULL);
        SendMessage(hRadioPie, BM_SETCHECK, BST_CHECKED, 0);

        ApplyFont(parent);
        UpdateCategoryList();
        RefreshTransactionList();
    }

    void ApplyFont(HWND parent) {
        HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Meiryo UI");
        EnumChildWindows(parent, [](HWND hwnd, LPARAM lparam) -> BOOL {
            SendMessage(hwnd, WM_SETFONT, (WPARAM)lparam, TRUE);
            return TRUE;
            }, (LPARAM)hFont);
    }

    void UpdateCategoryList() {
        int type = GetCurrentType();
        SendMessage(hComboCat, CB_RESETCONTENT, 0, 0);
        auto items = pDB->GetCategories(type);
        for (const auto& item : items) {
            SendMessage(hComboCat, CB_ADDSTRING, 0, (LPARAM)item.name.c_str());
        }
        // 修正: リスト更新後に一番上を選択する
        if (!items.empty()) {
            SendMessage(hComboCat, CB_SETCURSEL, 0, 0);
        }
    }

    void RefreshTransactionList() {
        ListView_DeleteAllItems(hListTrans);
        std::wstring date = GetSelectedDate();
        auto list = pDB->GetTransactionsForDate(date);

        LVITEM lvi = { 0 };
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        int i = 0;
        for (const auto& item : list) {
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)item.category.c_str();
            lvi.lParam = item.id;
            ListView_InsertItem(hListTrans, &lvi);

            wchar_t amtStr[32];
            swprintf_s(amtStr, L"%.0f", item.amount);
            ListView_SetItemText(hListTrans, i, 1, amtStr);

            ListView_SetItemText(hListTrans, i, 2, (item.type == TYPE_INCOME) ? L"収" : L"支");
            i++;
        }
        CancelEditMode();
    }

    void StartEditMode(int id, const std::wstring& cat, float amt, int type) {
        m_editingId = id;
        SetWindowText(hComboCat, cat.c_str());
        wchar_t buf[64]; swprintf_s(buf, L"%.0f", amt);
        SetWindowText(hEditAmt, buf);
        if (type == TYPE_INCOME) {
            SendMessage(hRadioInc, BM_SETCHECK, BST_CHECKED, 0);
            SendMessage(hRadioExp, BM_SETCHECK, BST_UNCHECKED, 0);
        }
        else {
            SendMessage(hRadioExp, BM_SETCHECK, BST_CHECKED, 0);
            SendMessage(hRadioInc, BM_SETCHECK, BST_UNCHECKED, 0);
        }
        UpdateCategoryList();
        SetWindowText(hBtnAdd, L"修正");
    }

    void CancelEditMode() {
        m_editingId = -1;
        SetWindowText(hEditAmt, L"");
        SetWindowText(hBtnAdd, L"登録");
    }

    std::wstring GetSelectedDate() {
        SYSTEMTIME st; MonthCal_GetCurSel(hCalendar, &st);
        wchar_t buf[32]; swprintf_s(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        return buf;
    }

    void GetSelectedDateStruct(SYSTEMTIME* st) { MonthCal_GetCurSel(hCalendar, st); }

    bool GetInput(std::wstring& outCat, float& outAmt, int& outType) {
        wchar_t buf[256];
        GetWindowText(hComboCat, buf, 256); outCat = buf;
        GetWindowText(hEditAmt, buf, 256); outAmt = (float)_wtof(buf);
        outType = GetCurrentType();
        return !outCat.empty() && outAmt > 0;
    }

    int GetCurrentType() { return (SendMessage(hRadioInc, BM_GETCHECK, 0, 0) == BST_CHECKED) ? TYPE_INCOME : TYPE_EXPENSE; }
    ReportMode GetReportMode() { return (SendMessage(hRadioYear, BM_GETCHECK, 0, 0) == BST_CHECKED) ? MODE_YEARLY : MODE_MONTHLY; }
    GraphType GetGraphType() { return (SendMessage(hRadioLine, BM_GETCHECK, 0, 0) == BST_CHECKED) ? GRAPH_LINE : GRAPH_PIE; }
};

// -----------------------------------------------------------------------------
// 4. MainApp (Controller)
// -----------------------------------------------------------------------------
class MainApp {
private:
    ExpenseManager dbManager;
    ChartCanvas canvas;
    InputPanel inputPanel;
    HWND hWnd;

    LPARAM GetTransactionIdFromList(int idx) {
        LVITEM lvi = { 0 };
        lvi.iItem = idx;
        lvi.mask = LVIF_PARAM;
        ListView_GetItem(inputPanel.hListTrans, &lvi);
        return lvi.lParam;
    }

    std::wstring GetTransactionTextFromList(int idx, int subItem) {
        wchar_t buf[256];
        ListView_GetItemText(inputPanel.hListTrans, idx, subItem, buf, 256);
        return std::wstring(buf);
    }

public:
    void Run(HINSTANCE hInstance) {
        InitCommonControls();
        dbManager.Initialize();
        canvas.Initialize();
        WNDCLASS wc = { 0 };
        wc.lpfnWndProc = MainApp::WndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"KakeiboAppV4";
        RegisterClass(&wc);

        hWnd = CreateWindow(L"KakeiboAppV4", L"家計簿アプリ v7.0",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1150, 750, NULL, NULL, hInstance, this);
        ShowWindow(hWnd, SW_SHOW);

        // 修正: Tabキー移動のためのIsDialogMessageの追加
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            if (!IsDialogMessage(hWnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        MainApp* pApp = NULL;
        if (msg == WM_NCCREATE) {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lp;
            pApp = (MainApp*)pCreate->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pApp);
            pApp->hWnd = hwnd;
        }
        else {
            pApp = (MainApp*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }
        if (pApp) return pApp->HandleMessage(msg, wp, lp);
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE:
            inputPanel.Create(hWnd, &dbManager);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wp) == 100) {
                std::wstring cat; float amt; int type;
                if (inputPanel.GetInput(cat, amt, type)) {
                    if (inputPanel.m_editingId == -1) {
                        dbManager.AddTransaction(inputPanel.GetSelectedDate(), cat, amt, type);
                    }
                    else {
                        dbManager.UpdateTransaction(inputPanel.m_editingId, cat, amt, type);
                        inputPanel.CancelEditMode();
                    }
                    SetWindowText(inputPanel.hEditAmt, L"");
                    inputPanel.UpdateCategoryList();
                    inputPanel.RefreshTransactionList();
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            if (LOWORD(wp) == ID_BTN_SETTINGS) {
                SettingsWindow::Show(hWnd, &dbManager);
                inputPanel.UpdateCategoryList();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            if (LOWORD(wp) == 2001) {
                int idx = ListView_GetNextItem(inputPanel.hListTrans, -1, LVNI_SELECTED);
                if (idx != -1) {
                    int id = (int)GetTransactionIdFromList(idx);
                    std::wstring cat = GetTransactionTextFromList(idx, 0);
                    float amt = (float)_wtof(GetTransactionTextFromList(idx, 1).c_str());
                    std::wstring typeStr = GetTransactionTextFromList(idx, 2);
                    int type = (typeStr == L"収") ? TYPE_INCOME : TYPE_EXPENSE;
                    inputPanel.StartEditMode(id, cat, amt, type);
                }
            }
            if (LOWORD(wp) == 2002) {
                int idx = ListView_GetNextItem(inputPanel.hListTrans, -1, LVNI_SELECTED);
                if (idx != -1) {
                    if (MessageBox(hWnd, L"選択した明細を削除しますか？", L"確認", MB_YESNO | MB_ICONWARNING) == IDYES) {
                        int id = (int)GetTransactionIdFromList(idx);
                        dbManager.DeleteTransaction(id);
                        inputPanel.RefreshTransactionList();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                }
            }

            if (HIWORD(wp) == BN_CLICKED) {
                if (LOWORD(wp) == 400 || LOWORD(wp) == 401) {
                    inputPanel.UpdateCategoryList();
                }
                canvas.DrillUp();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;

        case WM_NOTIFY:
        {
            LPNMHDR pnm = (LPNMHDR)lp;
            if (pnm->code == MCN_SELECT) {
                inputPanel.RefreshTransactionList();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            if (pnm->hwndFrom == inputPanel.hListTrans && pnm->code == NM_RCLICK) {
                int idx = ListView_GetNextItem(inputPanel.hListTrans, -1, LVNI_SELECTED);
                if (idx != -1) {
                    POINT pt; GetCursorPos(&pt);
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenu(hMenu, MF_STRING, 2001, L"修正");
                    AppendMenu(hMenu, MF_STRING, 2002, L"削除");
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
                    DestroyMenu(hMenu);
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            int x = LOWORD(lp); int y = HIWORD(lp);
            canvas.UpdateMousePos(x, y);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
        case WM_LBUTTONDOWN:
        {
            std::wstring hovered = canvas.GetHoveredCategory();
            if (!hovered.empty()) {
                canvas.DrillDown(hovered);
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        return 0;
        case WM_RBUTTONDOWN:
        {
            canvas.DrillUp();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
        case WM_SIZE:
            canvas.Resize(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        case WM_PAINT:
        {
            SYSTEMTIME st; inputPanel.GetSelectedDateStruct(&st);
            ReportMode rMode = inputPanel.GetReportMode();
            GraphType gType = inputPanel.GetGraphType();
            int currentType = inputPanel.GetCurrentType();
            wchar_t start[32], end[32], title[64];
            if (rMode == MODE_MONTHLY) {
                swprintf_s(start, L"%04d-%02d-01", st.wYear, st.wMonth);
                swprintf_s(end, L"%04d-%02d-31", st.wYear, st.wMonth);
                swprintf_s(title, L"【月間】%d年 %d月", st.wYear, st.wMonth);
            }
            else {
                swprintf_s(start, L"%04d-01-01", st.wYear);
                swprintf_s(end, L"%04d-12-31", st.wYear);
                swprintf_s(title, L"【年間】%d年", st.wYear);
            }
            canvas.Render(hWnd, dbManager, start, end, title, gType, rMode, currentType);
            ValidateRect(hWnd, NULL);
        }
        return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hWnd, msg, wp, lp);
    }
};

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) {
    MainApp app;
    app.Run(h);
    return 0;
}