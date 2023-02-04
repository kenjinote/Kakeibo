#pragma once
#include "window.h"
class navi :
	public window
{
public:
	navi();
	~navi();
	virtual LRESULT CALLBACK LocalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

