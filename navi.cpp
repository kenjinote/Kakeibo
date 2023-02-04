#include "navi.h"
#include "resource.h"

navi::navi()
{
	m_szClassName = TEXT("NaviWindow");
	m_dwWindowStyle = WS_CHILD | WS_VISIBLE;
}

navi::~navi()
{
}

LRESULT CALLBACK navi::LocalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static HWND hButton[3];
	switch (msg)
	{
	case WM_CREATE:
		hButton[0] = CreateWindow(TEXT("BUTTON"), TEXT("ƒz[ƒ€"), WS_VISIBLE | WS_CHILD, 0, 0, 64, 32, hWnd, (HMENU)ID_HOME, ((LPCREATESTRUCT)lParam)->hInstance, 0);
		hButton[1] = CreateWindow(TEXT("BUTTON"), TEXT("“ü—Í"), WS_VISIBLE | WS_CHILD, 0, 32, 64, 32, hWnd, (HMENU)ID_INPUT, ((LPCREATESTRUCT)lParam)->hInstance, 0);
		hButton[2] = CreateWindow(TEXT("BUTTON"), TEXT("Ý’è"), WS_VISIBLE | WS_CHILD, 0, 64, 64, 32, hWnd, (HMENU)ID_OPTION, ((LPCREATESTRUCT)lParam)->hInstance, 0);
		break;
	case WM_COMMAND:
		if (LOWORD(wParam) == ID_HOME || LOWORD(wParam) == ID_INPUT || LOWORD(wParam) == ID_OPTION)
		{
			SendMessage(GetParent(hWnd), msg, wParam, lParam);
		}
		break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}
