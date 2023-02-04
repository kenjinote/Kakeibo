#pragma once
#include "window.h"
class option :
	public window
{
public:
	option();
	~option();
	virtual LRESULT CALLBACK LocalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

