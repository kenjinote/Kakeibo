#pragma once
#include <windows.h>
class window
{
	HWND m_hWnd;
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	virtual LRESULT CALLBACK LocalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
public:
	window();
	~window();
	HWND GetWindowHandle()const{ return m_hWnd; }
	LPTSTR m_szClassName;
	DWORD m_dwWindowStyle;
	HWND create(HWND hParent/*, int x, int y, int width, int height*/);
};

