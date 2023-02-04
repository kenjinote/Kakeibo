#pragma once
#include "window.h"

class input :
	public window
{
public:
	input();
	~input();
	virtual LRESULT CALLBACK LocalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};