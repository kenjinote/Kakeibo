#include "option.h"


option::option()
{
	m_szClassName = TEXT("OptionWindow");
	m_dwWindowStyle = WS_CHILD | WS_VISIBLE;
}

option::~option()
{
}

LRESULT CALLBACK option::LocalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
		//MessageBox(hWnd, TEXT("option"), 0, 0);
		break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		TextOut(hdc, 0, 0, TEXT("option"), 6);
		EndPaint(hWnd, &ps);
	}
	break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}
