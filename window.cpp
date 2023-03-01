#include <windows.h>
#include "window.h"

window::window() : m_hWnd(NULL), m_szClassName(NULL), m_dwWindowStyle(0)
{
	m_szClassName = TEXT("Sample Class Name");
	m_dwWindowStyle = WS_CHILD;
}

window::~window()
{
	DestroyWindow(m_hWnd);
	UnregisterClass(m_szClassName, GetModuleHandle(0));
}

HWND window::create(HWND hParent)
{
	const HINSTANCE hInstance = GetModuleHandle(0);
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
		m_szClassName
	};
	RegisterClass(&wndclass);
	return m_hWnd = CreateWindow(
		m_szClassName,
		0,
		m_dwWindowStyle,
		0/*x*/,
		0/*y*/,
		0/*width*/,
		0/*height*/,
		hParent,
		0,
		hInstance,
		this
		);
}

LRESULT CALLBACK window::LocalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
		MessageBox(hWnd, TEXT("window"), 0, 0);
		break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK window::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	window* _this = 0;
	if (msg == WM_CREATE)
	{
		_this = (window*)((LPCREATESTRUCT)lParam)->lpCreateParams;
		SetWindowLong(hWnd, GWL_USERDATA, (LONG)_this);
	}
	else
	{
		_this = (window*)GetWindowLong(hWnd, GWL_USERDATA);
	}
	if (_this)
	{
		return _this->LocalWndProc(hWnd, msg, wParam, lParam);
	}
	else
	{
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
}
