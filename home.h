#pragma once
#include "window.h"
class home :
	public window
{
public:
	home();
	~home();
	virtual LRESULT CALLBACK LocalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

