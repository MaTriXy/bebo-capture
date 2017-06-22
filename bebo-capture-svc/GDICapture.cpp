#include "GDICapture.h"

#include <comdef.h>
#include <dshow.h>
#include <strsafe.h>
#include <tchar.h>
#include <windows.h>
#include <wmsdkidl.h>
#include <dxgi.h>
#include <thread>
#include "DibHelper.h"
#include "window-helpers.h"
#include "Logging.h"
#include "libyuv/convert.h"
#include "libyuv/scale_argb.h"
#include <vector>

struct Window {
	HWND hwnd;
	RECT bounds;
	bool should_capture;
};
struct EnumWindowsContext {
	std::vector<Window> windows;
	HWND capture_hwnd;
	DWORD capture_pid;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM param) {
	EnumWindowsContext* ctx = reinterpret_cast<EnumWindowsContext*>(param);

	Window win;
	win.hwnd = hwnd;

	DWORD pid = -1;
	GetWindowThreadProcessId(hwnd, &pid);
	win.should_capture = (pid == ctx->capture_pid);

	if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
		return TRUE;
	}

	GetWindowRect(hwnd, &win.bounds);
	ctx->windows.push_back(win);
	return TRUE;
}

GDICapture::GDICapture():
	negotiated_width(0),
	negotiated_height(0),
	initialized(false),
	capture_foreground(false),
	capture_screen(false),
	capture_decoration(false),
	capture_mouse(false),
	capture_hwnd(false),
	last_frame(new GDIFrame),
	negotiated_argb_buffer(nullptr)
{
	hrgn_foreground = CreateRectRgn(0, 0, 0, 0);
	hrgn_background = CreateRectRgn(0, 0, 0, 0);
	hrgn_visual = CreateRectRgn(0, 0, 0, 0);
}

GDICapture::~GDICapture() {
	if (last_frame) {
		delete last_frame;
	}

	if (negotiated_argb_buffer) {
		delete[] negotiated_argb_buffer;
	}

	if (hrgn_foreground) {
		DeleteObject(hrgn_foreground);
	}

	if (hrgn_background) {
		DeleteObject(hrgn_background);
	}

	if (hrgn_visual) {
		DeleteObject(hrgn_visual);
	}
}

void GDICapture::InitHDC(int width, int height, HWND hwnd) {
	initialized = true;
	negotiated_width = width;
	negotiated_height = height;
	capture_hwnd = hwnd;
	negotiated_argb_buffer = new BYTE[4 * negotiated_width * negotiated_height];
}

//https://github.com/mozilla/gecko-dev/blob/master/media/webrtc/trunk/webrtc/modules/desktop_capture/app_capturer_win.cc
GDIFrame* GDICapture::CaptureFrame()
{
	if (!capture_hwnd) {
		return NULL;
	}

	if (!IsWindow(capture_hwnd)) {
		return NULL;
	}

	if ((IsIconic(capture_hwnd) || !IsWindowVisible(capture_hwnd)) && last_frame) {
		debug("Window not visible, returning last frame");
		return last_frame;
	}

	RECT capture_window_rect;
	GetClientRect(capture_hwnd, &capture_window_rect);

	// UPDATE REGION
	EnumWindowsContext ctx;
	ctx.capture_hwnd = capture_hwnd;
	ctx.capture_pid = -1;
	GetWindowThreadProcessId(capture_hwnd, &ctx.capture_pid);
	EnumWindows(EnumWindowsProc, (LPARAM)&ctx);

	SetRectRgn(hrgn_foreground, 0, 0, 0, 0);
	SetRectRgn(hrgn_visual, 0, 0, 0, 0);
	SetRectRgn(hrgn_background, 0, 0, 0, 0);

	HRGN hrgn_screen = CreateRectRgn(0, 0,
		GetSystemMetrics(SM_CXVIRTUALSCREEN),
		GetSystemMetrics(SM_CYVIRTUALSCREEN));

	HRGN hrgn_window = CreateRectRgn(0, 0, 0, 0);
	HRGN hrgn_internsect = CreateRectRgn(0, 0, 0, 0);

	for (auto it = ctx.windows.rbegin(); it != ctx.windows.rend(); it++) {
		Window win = *it;
		SetRectRgn(hrgn_window, 0, 0, 0, 0);
		if (GetWindowRgn(win.hwnd, hrgn_window) == ERROR) {
			SetRectRgn(hrgn_window, win.bounds.left,
				win.bounds.top,
				win.bounds.right,
				win.bounds.bottom);
		}

		if (win.should_capture) {
			CombineRgn(hrgn_visual, hrgn_visual, hrgn_window, RGN_OR);
			CombineRgn(hrgn_foreground, hrgn_foreground, hrgn_window, RGN_DIFF);
		} else {
			SetRectRgn(hrgn_internsect, 0, 0, 0, 0);
			CombineRgn(hrgn_internsect, hrgn_visual, hrgn_window, RGN_AND);
			CombineRgn(hrgn_visual, hrgn_visual, hrgn_internsect, RGN_DIFF);
			CombineRgn(hrgn_foreground, hrgn_foreground, hrgn_internsect, RGN_OR);
		}
	}
	CombineRgn(hrgn_background, hrgn_screen, hrgn_visual, RGN_DIFF);

	if (hrgn_window) {
		DeleteObject(hrgn_window);
	}

	if (hrgn_internsect) {
		DeleteObject(hrgn_internsect);
	}
	// END OF UPDATE REGION

	HDC hdc_screen = GetDC(NULL);
	RECT capture_screen_rect = { 0, 0, 
		GetSystemMetrics(SM_CXVIRTUALSCREEN), 
		GetSystemMetrics(SM_CYVIRTUALSCREEN)};

	last_frame->updateFrame(hdc_screen, capture_screen_rect);

	GDIFrame* frame = last_frame;

	HDC mem_hdc = CreateCompatibleDC(hdc_screen);

	// create a bitmap
	/*int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	HBITMAP hbDesktop = CreateCompatibleBitmap(hdc_screen, width, height);
	// use the previously created device context with the bitmap
	SelectObject(hDest, hbDesktop);
	// copy from the desktop device context to the bitmap device context
	// call this once per 'frame'
	BitBlt(hDest, 0, 0, width, height, hdc, 0, 0, SRCCOPY);
	*/
	
	HGDIOBJ old_bmp = SelectObject(mem_hdc, frame->bitmap());
	if (!old_bmp || old_bmp == HGDI_ERROR) {
		error("SelectObject failed from mem_hdc into bitmap.");
		return NULL;
	}

	// debug("frame wh: %dx%d - xy: %d,%d - cxy: %d,%d - xy: %d,%d", frame->width(), frame->height(), frame->x(), frame->y(), cx, cy);
	SelectClipRgn(mem_hdc, hrgn_background);
	FillRect(mem_hdc, &capture_window_rect, (HBRUSH) GetStockObject(WHITE_BRUSH));

	SelectClipRgn(mem_hdc, hrgn_visual);
	int cx = GetSystemMetrics(SM_CXSIZEFRAME);
	int cy = GetSystemMetrics(SM_CYSIZEFRAME);

	SelectClipRgn(mem_hdc, hrgn_background);
	SelectObject(mem_hdc, GetStockObject(DC_BRUSH));
	SetDCBrushColor(mem_hdc, RGB(0xff, 0, 0));
	FillRect(mem_hdc, &capture_screen_rect, (HBRUSH)GetStockObject(DC_BRUSH));

	// fill foreground
	SelectClipRgn(mem_hdc, hrgn_foreground);
	SelectObject(mem_hdc, GetStockObject(DC_BRUSH));
	SetDCBrushColor(mem_hdc, RGB(0xff, 0xff, 0));
	FillRect(mem_hdc, &capture_screen_rect, (HBRUSH)GetStockObject(DC_BRUSH));

	BitBlt(mem_hdc, 0, 0, 
		frame->width(), frame->height(), 
		hdc_screen, 
		0, 0, 
		SRCCOPY);

	SelectObject(mem_hdc, old_bmp);

	DeleteDC(mem_hdc);
	ReleaseDC(NULL, hdc_screen);

	return frame;
}

bool GDICapture::GetFrame(IMediaSample *pSample)
{
	debug("CopyScreenToDataBlock - start");

	GDIFrame* frame = CaptureFrame();
	if (frame == NULL) {
		return false;
	}

	BYTE *pdata;
	pSample->GetPointer(&pdata);

	const uint8_t* src_frame = frame->data();
	int src_stride_frame = frame->stride();
	int src_width = frame->width();
	int src_height = frame->height();

	int scaled_argb_stride = 4 * negotiated_width;

	libyuv::ARGBScale(
		src_frame, src_stride_frame,
		src_width, src_height,
		negotiated_argb_buffer, scaled_argb_stride,
		negotiated_width, negotiated_height,
		libyuv::FilterMode(libyuv::kFilterBox)
	);

	uint8* y = pdata;
	int stride_y = negotiated_width;
	uint8* u = pdata + (negotiated_width * negotiated_height);
	int stride_u = (negotiated_width + 1) / 2;
	uint8* v = u + ((negotiated_width * negotiated_height) >> 2);
	int stride_v = stride_u;

	libyuv::ARGBToI420(negotiated_argb_buffer, scaled_argb_stride,
		y, stride_y,
		u, stride_u,
		v, stride_v,
		negotiated_width, negotiated_height);

	return true;
}




