#pragma comment(lib, "rpcrt4")
#pragma comment(lib, "shlwapi")
#pragma comment(lib, "legacy_stdio_definitions")
#pragma comment(linker,"/manifestdependency:\"type='win32' name = 'Microsoft.Windows.Common-Controls' version = '6.0.0.0' processorArchitecture = '*' publicKeyToken = '6595b64144ccf1df' language = '*'\"")

#include <windows.h>
#include "home.h"
#include "input.h"
#include "option.h"
#include "navi.h"
#include "database.h"
#include "resource.h"

TCHAR szClassName[] = TEXT("Window");

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static home* win;
	static input* win2;
	static option* win3;
	static navi* win4;
	static database* db;
	switch (msg)
	{
	case WM_CREATE:
		win = new home;win->create(hWnd);
		win2 = new input;win2->create(hWnd);
		win3 = new option;win3->create(hWnd);
		win4 = new navi;win4->create(hWnd);
		db = new database;db->CreateDatabase();
		PostMessage(hWnd, WM_COMMAND, ID_HOME, 0);
		break;
	case WM_SIZE:
		MoveWindow(win->GetWindowHandle(), 64, 0, LOWORD(lParam) - 64, HIWORD(lParam), 1);
		MoveWindow(win2->GetWindowHandle(), 64, 0, LOWORD(lParam) - 64, HIWORD(lParam), 1);
		MoveWindow(win3->GetWindowHandle(), 64, 0, LOWORD(lParam) - 64, HIWORD(lParam), 1);
		MoveWindow(win4->GetWindowHandle(), 0, 0, 64, HIWORD(lParam), 1);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_HOME:
			ShowWindow(win->GetWindowHandle(), SW_SHOW);
			ShowWindow(win2->GetWindowHandle(), SW_HIDE);
			ShowWindow(win3->GetWindowHandle(), SW_HIDE);
			break;
		case ID_INPUT:
			ShowWindow(win->GetWindowHandle(), SW_HIDE);
			ShowWindow(win2->GetWindowHandle(), SW_SHOW);
			ShowWindow(win3->GetWindowHandle(), SW_HIDE);
			break;
		case ID_OPTION:
			ShowWindow(win->GetWindowHandle(), SW_HIDE);
			ShowWindow(win2->GetWindowHandle(), SW_HIDE);
			ShowWindow(win3->GetWindowHandle(), SW_SHOW);
			break;
		}
		break;
	case WM_DESTROY:
		delete win;
		delete win2;
		delete win3;
		delete win4;
		delete db;
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd)
{
	CoInitialize(0);
	MSG msg;
	WNDCLASS wndclass = {
		CS_HREDRAW | CS_VREDRAW,
		WndProc,
		0,
		0,
		hInstance,
		0,
		LoadCursor(0, IDC_ARROW),
		(HBRUSH)(COLOR_WINDOW + 1),
		0,
		szClassName
	};
	RegisterClass(&wndclass);
	HWND hWnd = CreateWindowW(
		szClassName,
		L"‰ÆŒv•ë",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		0,
		CW_USEDEFAULT,
		0,
		0,
		0,
		hInstance,
		0
		);
	ShowWindow(hWnd, SW_SHOWDEFAULT);
	UpdateWindow(hWnd);
	while (GetMessage(&msg, 0, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	CoUninitialize();
	return msg.wParam;
}