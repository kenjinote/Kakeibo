#include "home.h"

home::home()
{
	m_szClassName = TEXT("HomeWindow");
	m_dwWindowStyle = WS_CHILD | WS_VISIBLE;
}

home::~home()
{
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
			TextOut(hdc, 0, 0, TEXT("home"), 4);
			EndPaint(hWnd, &ps);
		}
		break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}