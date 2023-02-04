#include "input.h"


input::input()
{
	m_szClassName = TEXT("InputWindow");
	m_dwWindowStyle = WS_CHILD | WS_VISIBLE;
}

input::~input()
{
}

LRESULT CALLBACK input::LocalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
		break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		TextOut(hdc, 0, 0, TEXT("input"), 5);
		EndPaint(hWnd, &ps);
	}
	break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}