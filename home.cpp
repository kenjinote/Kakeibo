#include "home.h"
#define _USE_MATH_DEFINES
#include <math.h>

// via https://contents-open.hatenablog.com/entry/2021/08/19/231157#:~:text=10%E8%89%B2%EF%BC%88Paul%20Tol%E6%B0%8F%E6%8F%90%E6%A1%88%20Muted%EF%BC%89
COLORREF colors[] = {
	RGB(51,34,136),
	RGB(136,204,238),
	RGB(68,170,153),
	RGB(17,119,51),
	RGB(153,153,51),
	RGB(221,204,119),
	RGB(204,102,119),
	RGB(136,34,58),
	RGB(170,68,153),
	RGB(221,221,221),
};

home::home()
{
	m_szClassName = TEXT("HomeWindow");
	m_dwWindowStyle = WS_CHILD;
}

home::~home()
{
}

void home::DrawPie(HDC hdc, int x, int y, int width, Item* items, DWORD dwSize)
{
	float xMax = 0;
	for (int i = 0; i < dwSize; i++)
	{
		xMax += (float)items[i].dwValue;
	}
	int nX = x;
	int nY = y;
	DWORD dwRadius = width;
	float xStartAngle = 0.0f;
	HPEN hPen = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
	HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
	for (int i = 0; i < dwSize; i++)
	{
		HBRUSH hBrush = CreateSolidBrush(colors[i%_countof(colors)]);
		BeginPath(hdc);
		HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
		MoveToEx(hdc, nX, nY, (LPPOINT)NULL);
		AngleArc(hdc, nX, nY, dwRadius, xStartAngle, items[i].dwValue * 360.0f / xMax);
		LineTo(hdc, nX, nY);
		EndPath(hdc);
		StrokeAndFillPath(hdc);
		SelectObject(hdc, hOldBrush);
		DeleteObject(hBrush);
		RECT rect = {};
		rect.left = rect.right = nX + (width / 1.8) * cos(2.0 * M_PI * (xStartAngle + items[i].dwValue * 360.0f / xMax / 2.0f) / 360.0f);
		rect.top = rect.bottom = nY - (width / 1.8) * sin(2.0 * M_PI * (xStartAngle + items[i].dwValue * 360.0f / xMax / 2.0f) / 360.0f);
		DrawText(hdc, items[i].lpszName, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
		xStartAngle += items[i].dwValue * 360.0f / xMax;
	}
	SelectObject(hdc, hOldPen);
	DeleteObject(hPen);
}

LRESULT CALLBACK home::LocalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
		break;
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);

			Item item1[2];
			item1[0].dwValue = 50;
			item1[0].lpszName = L"‹‹—^";
			item1[1].dwValue = 20;
			item1[1].lpszName = L"‰Æ’À";
			DrawPie(hdc, 200, 200, 100, item1, _countof(item1));

			Item item2[4];
			item2[0].dwValue = 50;
			item2[0].lpszName = L"‰Æ’À";
			item2[1].dwValue = 20;
			item2[1].lpszName = L"“d‹C‘ã";
			item2[2].dwValue = 20;
			item2[2].lpszName = L"ƒKƒX‘ã";
			item2[3].dwValue = 20;
			item2[3].lpszName = L"’ÊM”ï";
			DrawPie(hdc, 600, 200, 100, item2, _countof(item2));

			EndPaint(hWnd, &ps);
	}
		break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}