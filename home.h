#pragma once
#include "window.h"

struct Item {
	LPWSTR lpszName;
	DWORD dwValue;
};

class home :
	public window
{
public:
	home();
	~home();
	void DrawPie(HDC hdc, int x, int y, int width, Item* items, DWORD dwSize);
	virtual LRESULT CALLBACK LocalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

