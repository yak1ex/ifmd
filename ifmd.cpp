/***********************************************************************/
/*                                                                     */
/* ifmd.cpp: Source file for ifmd                                      */
/*                                                                     */
/*     Copyright (C) 2013 Yak! / Yasutaka ATARASHI                     */
/*                                                                     */
/*     This software is distributed under the terms of a zlib/libpng   */
/*     License.                                                        */
/*                                                                     */
/*     $Id: 61260feeb364cbc8e9b6daea239472ade36e8dc8 $                 */
/*                                                                     */
/***********************************************************************/
#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <cstdio>

#include <sys/stat.h>

#include "Spi_api.h"
#include "resource.h"
#ifdef DEBUG
#include <iostream>
#include <iomanip>
#include "odstream.hpp"
#define DEBUG_LOG(ARG) yak::debug::ods ARG
#else
#define DEBUG_LOG(ARG) do { } while(0)
#endif
#include "disphelper/disphelper.h"
#include <exdisp.h>
#include <comdef.h>
#include <mshtml.h>

// Discount has many conflicts with Win32 headers, so only required functions are declared.
typedef struct document Document;
extern "C" {
	int mkd_compile(Document *, DWORD);
	void mkd_cleanup(Document *);
	Document *mkd_in(FILE *, DWORD);
	Document *mkd_string(const char*,int, DWORD);
	int  mkd_document(Document *, char **);
}

__CRT_UUID_DECL(IHTMLDocument2, 0x332C4425, 0x26CB, 0x11D0, 0xB4, 0x83, 0x00, 0xC0, 0x4F, 0xD9, 0x01, 0x19);
const CLSID CLSID_HTMLDocument = { 0x25336920, 0x03F9, 0x11CF, 0x8F, 0xD0, 0x00, 0xAA, 0x00, 0x68, 0x6F, 0x13 };
// Not deeply analyzed, but definition in comdefsp.h seems to have no effect.
_COM_SMARTPTR_TYPEDEF(IHTMLDocument2,__uuidof(IHTMLDocument2));
_COM_SMARTPTR_TYPEDEF(IOleObject,__uuidof(IOleObject));

typedef std::pair<std::string, unsigned long> Key;
typedef std::vector<char> Data;
typedef std::pair<std::vector<SPI_FILEINFO>, std::vector<Data> > Value;
std::map<Key, Value> g_cache;

// Force null terminating version of strncpy
// Return length without null terminator
int safe_strncpy(char *dest, const char *src, std::size_t n)
{
	size_t i = 0;
	while(i<n && *src) { *dest++ = *src++; ++i; }
	*dest = 0;
	return i;
}

static HINSTANCE g_hInstance;

static bool g_fWarned;

static std::string g_sFFprobePath;
static std::string g_sFFmpegPath;
static int g_nImages;
static int g_nInterval;
static bool g_fImages;
static std::string g_sExtension;

const char* table[] = {
	"00IN",
	"Plugin to render Markdow file - v0.03 (2013/02/05) Written by Yak!",
	"*.md;*.markdown",
	"Markdownファイル"
};

INT PASCAL GetPluginInfo(INT infono, LPSTR buf, INT buflen)
{
	DEBUG_LOG(<< "GetPluginInfo(" << infono << ',' << buf << ',' << buflen << ')' << std::endl);
	if(0 <= infono && static_cast<size_t>(infono) < sizeof(table)/sizeof(table[0])) {
		return safe_strncpy(buf, infono == 2 ? g_sExtension.c_str() : table[infono], buflen);
	} else {
		return 0;
	}
}

static INT IsSupportedImp(LPSTR filename, LPBYTE pb)
{
	std::string name(filename);
	const char* start = g_sExtension.c_str();
	while(1) {
		while(*start && *start != '.') {
			++start;
		}
		if(!*start) break;
		const char* end = start;
		while(*end && *end != ';') {
			++end;
		}
		std::string ext(start, end);
		if(name.size() <= ext.size()) continue;
		if(!lstrcmpi(name.substr(name.size() - ext.size()).c_str(), ext.c_str())) {
			return SPI_SUPPORT_YES;
		}
		++start;
	}
	return SPI_SUPPORT_NO;
}

INT PASCAL IsSupported(LPSTR filename, DWORD dw)
{
	DEBUG_LOG(<< "IsSupported(" << filename << ',' << std::hex << std::setw(8) << std::setfill('0') << dw << ')' << std::endl);
	if(HIWORD(dw) == 0) {
		DEBUG_LOG(<< "File handle" << std::endl);
		BYTE pb[2048];
		DWORD dwSize;
		ReadFile((HANDLE)dw, pb, sizeof(pb), &dwSize, NULL);
		return IsSupportedImp(filename, pb);;
	} else {
		DEBUG_LOG(<< "Pointer" << std::endl); // By afx
		return IsSupportedImp(filename, (LPBYTE)dw);;
	}
	// not reached here
}

static bool GetHTML(LPSTR buf, LONG len, UINT flag, std::string &sHTML)
{
	Document *ctx;
	if((flag & 7) == 0) { // filename
		FILE *fp = std::fopen(buf, "r");
		std::fseek(fp, len, SEEK_SET);
		ctx = mkd_in(fp, 0);
	} else { // pointer
		ctx = mkd_string(buf, len, 0);
	}
	if(!mkd_compile(ctx, 0)) {
		mkd_cleanup(ctx);
		return false;
	}
	char *body;
	int body_size = mkd_document(ctx, &body);
	DEBUG_LOG(<< "GetHTML(): by discount: " << body << std::endl);
	sHTML = "<html><body>";
	sHTML += body;
	sHTML += "</body></html>";

	mkd_cleanup(ctx);
	return true;
}

// ref. http://eternalwindows.jp/ole/oledraw/oledraw01.html
// ref: http://eternalwindows.jp/browser/bandobject/bandobject03.html
static void DPtoHIMETRIC(LPSIZEL lpSizel)
{
	HDC hdc;
	const int HIMETRIC_INCH = 2540;

	hdc = GetDC(NULL);
	lpSizel->cx = MulDiv(lpSizel->cx, HIMETRIC_INCH, GetDeviceCaps(hdc, LOGPIXELSX));
	lpSizel->cy = MulDiv(lpSizel->cy, HIMETRIC_INCH, GetDeviceCaps(hdc, LOGPIXELSY));
	ReleaseDC(NULL, hdc);
}

static bool RenderHTML(const std::string& sHTML, HANDLE *pHBInfo, HANDLE *pHBm)
{
	IHTMLDocument2Ptr pDoc;
	HRESULT hrCreate = pDoc.CreateInstance(CLSID_HTMLDocument, 0, CLSCTX_INPROC_SERVER);
	dhCallMethod(pDoc, L".Writeln(%s)", sHTML.c_str());
	dhCallMethod(pDoc, L".Write");
	dhCallMethod(pDoc, L".Close");

	CDhStringA szHTML;
	dhGetValue(L"%s", &szHTML, pDoc, L".documentElement.outerHTML");
	DEBUG_LOG(<< "RenderHTML(): " << szHTML << std::endl);

	RECT imageRect = {0, 0, 256, 256};
	HDC hDC = GetDC(0);
	HDC hCompDC = CreateCompatibleDC(hDC);
	HBITMAP hBitmap = CreateCompatibleBitmap(hDC, 256, 256);
	HGDIOBJ hBitmapOld = SelectObject(hCompDC, hBitmap);
	IOleObjectPtr pOleObject;
	pDoc->QueryInterface(IID_PPV_ARGS(&pOleObject));
	SIZEL sizel = { 256, 256 };
	DPtoHIMETRIC(&sizel);
	pOleObject->SetExtent(DVASPECT_CONTENT, &sizel);
	OleDraw(pDoc, DVASPECT_CONTENT, hCompDC, &imageRect);

//	assert(pHBInfo);
	*pHBInfo = LocalAlloc(LMEM_MOVEABLE, sizeof(BITMAPINFOHEADER));
	BITMAPINFOHEADER *pHBInfo_ = static_cast<BITMAPINFOHEADER*>(LocalLock(*pHBInfo));
	pHBInfo_->biSize = sizeof(BITMAPINFOHEADER);
	pHBInfo_->biWidth = pHBInfo_->biHeight = 256;
	pHBInfo_->biPlanes = 1;
	pHBInfo_->biBitCount = 24;
	pHBInfo_->biCompression= BI_RGB;
	pHBInfo_->biSizeImage = 0;
	pHBInfo_->biXPelsPerMeter = pHBInfo_->biYPelsPerMeter = 0;
	pHBInfo_->biClrUsed = pHBInfo_->biClrImportant = 0;

//	assert(pHBm);
	*pHBm = LocalAlloc(LMEM_MOVEABLE, 256 * 256 * 3);
	GetDIBits(hCompDC, hBitmap, 0, 256, LocalLock(*pHBm), static_cast<LPBITMAPINFO>(static_cast<void*>(pHBInfo_)), DIB_RGB_COLORS);

	LocalUnlock(*pHBInfo);
	LocalUnlock(*pHBm);
	SelectObject(hCompDC, hBitmapOld);
	DeleteObject(hBitmap);
	DeleteDC(hCompDC);

	return true;
}

INT PASCAL GetPictureInfo(LPSTR buf, LONG len, UINT flag, SPI_PICTINFO *lpInfo)
{
	// if ((flag & 7) == 0)
	//     buf -> filename
	//     len -> offset
	// else
	//     buf -> pointer
	//     len -> size
	DEBUG_LOG(<< "GetPictureInfo(" << ((flag & 7) == 0 ? std::string(buf) : std::string(buf, std::min<DWORD>(len, 128))) << ',' << len << ',' << std::hex << std::setw(8) << std::setfill('0') << flag << ',' << lpInfo << ')' << std::endl);

	std::string sHTML;
	if(!GetHTML(buf, len, flag, sHTML)) {
		DEBUG_LOG(<< "GetPicture(): couldn't compile input");
		return SPI_ERR_INTERNAL_ERROR;
	}
	HANDLE hBInfo, hBm;
	if(!RenderHTML(sHTML, &hBInfo, &hBm)) {
		DEBUG_LOG(<< "GetPicture(): couldn't render HTML");
		return SPI_ERR_INTERNAL_ERROR;
	}

	BITMAPINFOHEADER *pHBInfo = static_cast<BITMAPINFOHEADER*>(LocalLock(hBInfo));
	lpInfo->left = lpInfo->top = 0;
	lpInfo->width = pHBInfo->biWidth;
	lpInfo->height = pHBInfo->biHeight;
	lpInfo->x_density = pHBInfo->biXPelsPerMeter;
	lpInfo->y_density = pHBInfo->biYPelsPerMeter;
	lpInfo->colorDepth = pHBInfo->biBitCount;
	lpInfo->hInfo = 0;

	LocalUnlock(hBInfo);
	LocalFree(hBInfo);
	LocalFree(hBm);

	return SPI_ERR_NO_ERROR;
}

INT PASCAL GetPicture(LPSTR buf, LONG len, UINT flag, HANDLE *pHBInfo, HANDLE *pHBm, FARPROC lpPrgressCallback, LONG lData)
{
	DEBUG_LOG(<< "GetPicture(" << ((flag & 7) == 0 ? std::string(buf) : std::string(buf, std::min<DWORD>(len, 128))) << ',' << len << ',' << std::hex << std::setw(8) << std::setfill('0') << flag << ')' << std::endl);

	std::string sHTML;
	if(!GetHTML(buf, len, flag, sHTML)) {
		DEBUG_LOG(<< "GetPicture(): couldn't compile input");
		return SPI_ERR_INTERNAL_ERROR;
	}
	if(!RenderHTML(sHTML, pHBInfo, pHBm)) {
		DEBUG_LOG(<< "GetPicture(): couldn't render HTML");
		return SPI_ERR_INTERNAL_ERROR;
	}

	return SPI_ERR_NO_ERROR;
}

INT PASCAL GetPreview(LPSTR buf, LONG len, UINT flag, HANDLE *pHBInfo, HANDLE *pHBm, FARPROC lpPrgressCallback, LONG lData)
{
	DEBUG_LOG(<< "GetPreview(" << std::string(buf, std::min<DWORD>(len, 1024)) << ',' << len << ',' << std::hex << std::setw(8) << std::setfill('0') << flag << ')' << std::endl);
	return SPI_ERR_NOT_IMPLEMENTED;
}

static LRESULT CALLBACK AboutDlgProc(HWND hDlgWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
		case WM_INITDIALOG:
			return FALSE;
		case WM_COMMAND:
			switch (LOWORD(wp)) {
				case IDOK:
					EndDialog(hDlgWnd, IDOK);
					break;
				case IDCANCEL:
					EndDialog(hDlgWnd, IDCANCEL);
					break;
				default:
					return FALSE;
			}
		default:
			return FALSE;
	}
	return TRUE;
}

static std::string g_sIniFileName; // ini ファイル名

void LoadFromIni()
{
	g_nImages = GetPrivateProfileInt("axffmpeg", "imagenum", 1, g_sIniFileName.c_str());
	g_nInterval = GetPrivateProfileInt("axffmpeg", "interval", 1, g_sIniFileName.c_str());
	g_fImages = GetPrivateProfileInt("axffmpeg", "images", 1, g_sIniFileName.c_str());
	std::vector<char> vBuf(1024);
	DWORD dwSize;
	for(dwSize = vBuf.size() - 1; dwSize == vBuf.size() - 1; vBuf.resize(vBuf.size() * 2)) {
		dwSize = GetPrivateProfileString("axffmpeg", "ffprobe", "", &vBuf[0], vBuf.size(), g_sIniFileName.c_str());
	}
	g_sFFprobePath = std::string(&vBuf[0]);
	for(dwSize = vBuf.size() - 1; dwSize == vBuf.size() - 1; vBuf.resize(vBuf.size() * 2)) {
		dwSize = GetPrivateProfileString("axffmpeg", "ffmpeg", "", &vBuf[0], vBuf.size(), g_sIniFileName.c_str());
	}
	g_sFFmpegPath = std::string(&vBuf[0]);
	for(dwSize = vBuf.size() - 1; dwSize == vBuf.size() - 1; vBuf.resize(vBuf.size() * 2)) {
		dwSize = GetPrivateProfileString("axffmpeg", "extension", table[2], &vBuf[0], vBuf.size(), g_sIniFileName.c_str());
	}
	g_sExtension = std::string(&vBuf[0]);
}

void SaveToIni()
{
	char buf[1024];
	WritePrivateProfileString("axffmpeg", "images", g_fImages ? "1" : "0", g_sIniFileName.c_str());
	wsprintf(buf, "%d", g_nImages);
	WritePrivateProfileString("axffmpeg", "imagenum", buf, g_sIniFileName.c_str());
	wsprintf(buf, "%d", g_nInterval);
	WritePrivateProfileString("axffmpeg", "interval", buf, g_sIniFileName.c_str());
	WritePrivateProfileString("axffmpeg", "ffprobe", g_sFFprobePath.c_str(), g_sIniFileName.c_str());
	WritePrivateProfileString("axffmpeg", "ffmpeg", g_sFFmpegPath.c_str(), g_sIniFileName.c_str());
	WritePrivateProfileString("axffmpeg", "extension", g_sExtension.c_str(), g_sIniFileName.c_str());
}

void SetIniFileName(HANDLE hModule)
{
    std::vector<char> vModulePath(1024);
    size_t nLen = GetModuleFileName((HMODULE)hModule, &vModulePath[0], (DWORD)vModulePath.size());
    vModulePath.resize(nLen + 1);
    // 本来は2バイト文字対策が必要だが、プラグイン名に日本語はないと前提として手抜き
    while (!vModulePath.empty() && vModulePath.back() != '\\') {
        vModulePath.pop_back();
    }

    g_sIniFileName = &vModulePath[0];
    g_sIniFileName +=".ini";
}

void UpdateDialogItem(HWND hDlgWnd)
{
	SendDlgItemMessage(hDlgWnd, IDC_SPIN_IMAGES, UDM_SETRANGE32, 0, 0x7FFFFFFF);
	SendDlgItemMessage(hDlgWnd, IDC_SPIN_IMAGES, UDM_SETPOS32, 0, g_nImages);
	SendDlgItemMessage(hDlgWnd, g_fImages ? IDC_RADIO_IMAGES : IDC_RADIO_INTERVAL, BM_SETCHECK, BST_CHECKED, 0);
	EnableWindow(GetDlgItem(hDlgWnd, IDC_EDIT_IMAGES), g_fImages);
	EnableWindow(GetDlgItem(hDlgWnd, IDC_SPIN_IMAGES), g_fImages);
	SendDlgItemMessage(hDlgWnd, IDC_SPIN_INTERVAL, UDM_SETRANGE32, 0, 0x7FFFFFFF);
	SendDlgItemMessage(hDlgWnd, IDC_SPIN_INTERVAL, UDM_SETPOS32, 0, g_nInterval);
	EnableWindow(GetDlgItem(hDlgWnd, IDC_EDIT_INTERVAL), !g_fImages);
	EnableWindow(GetDlgItem(hDlgWnd, IDC_SPIN_INTERVAL), !g_fImages);
	SendDlgItemMessage(hDlgWnd, IDC_EDIT_FFPROBE_PATH, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(g_sFFprobePath.c_str()));
	SendDlgItemMessage(hDlgWnd, IDC_EDIT_FFMPEG_PATH, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(g_sFFmpegPath.c_str()));
	SendDlgItemMessage(hDlgWnd, IDC_EDIT_EXTENSION, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(g_sExtension.c_str()));
}

bool UpdateValue(HWND hDlgWnd)
{
	g_nImages = SendDlgItemMessage(hDlgWnd, IDC_SPIN_IMAGES, UDM_GETPOS32, 0, 0);
	g_nInterval = SendDlgItemMessage(hDlgWnd, IDC_SPIN_INTERVAL, UDM_GETPOS32, 0, 0);
	g_fImages = (SendDlgItemMessage(hDlgWnd, IDC_RADIO_IMAGES, BM_GETCHECK, 0, 0) == BST_CHECKED);

	LRESULT lLen = SendDlgItemMessage(hDlgWnd, IDC_EDIT_FFPROBE_PATH, WM_GETTEXTLENGTH, 0, 0);
	std::vector<char> vBuf(lLen+1);
	SendDlgItemMessage(hDlgWnd, IDC_EDIT_FFPROBE_PATH, WM_GETTEXT, lLen+1, reinterpret_cast<LPARAM>(&vBuf[0]));
	g_sFFprobePath = std::string(&vBuf[0]);

	lLen = SendDlgItemMessage(hDlgWnd, IDC_EDIT_FFMPEG_PATH, WM_GETTEXTLENGTH, 0, 0);
	vBuf.resize(lLen+1);
	SendDlgItemMessage(hDlgWnd, IDC_EDIT_FFMPEG_PATH, WM_GETTEXT, lLen+1, reinterpret_cast<LPARAM>(&vBuf[0]));
	g_sFFmpegPath = std::string(&vBuf[0]);

	lLen = SendDlgItemMessage(hDlgWnd, IDC_EDIT_EXTENSION, WM_GETTEXTLENGTH, 0, 0);
	vBuf.resize(lLen+1);
	SendDlgItemMessage(hDlgWnd, IDC_EDIT_EXTENSION, WM_GETTEXT, lLen+1, reinterpret_cast<LPARAM>(&vBuf[0]));
	g_sExtension = std::string(&vBuf[0]);

	return true; // TODO: Always update
}

static void BrowseExePath(HWND hDlgWnd, bool fProbe)
{
	std::vector<char> buf(2048);
	int nFile, nExtension;
	if(fProbe) {
		std::copy(g_sFFprobePath.begin(), g_sFFprobePath.end(), buf.begin());
		buf[g_sFFprobePath.size()] = 0;
		nFile = g_sFFprobePath.find_last_of('\\');
		nExtension = g_sFFprobePath.find_last_of('.');
	} else {
		std::copy(g_sFFmpegPath.begin(), g_sFFmpegPath.end(), buf.begin());
		buf[g_sFFmpegPath.size()] = 0;
		nFile = g_sFFmpegPath.find_last_of('\\');
		nExtension = g_sFFmpegPath.find_last_of('.');
	}
	if(nFile < 0) nFile = 0;
	if(nExtension < 0) nExtension = 0;
	OPENFILENAME ofn = {
		sizeof(OPENFILENAME),
		hDlgWnd,
		0,
		fProbe ? "ffprobe.exe\0ffprobe.exe\0\0" : "ffmpeg.exe\0ffmpeg.exe\0\0",
		0,
		0,
		1,
		&buf[0],
		buf.size(),
		0,
		0,
		0,
		fProbe ? "Specify the place of ffprobe.exe" : "Specify the place of ffmpeg.exe",
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_LONGNAMES | OFN_PATHMUSTEXIST,
		nFile,
		nExtension,
		0,
		0,
		0,
		0
	};
	if(GetOpenFileName(&ofn)) {
		SendDlgItemMessage(hDlgWnd, fProbe ? IDC_EDIT_FFPROBE_PATH : IDC_EDIT_FFMPEG_PATH, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(&buf[0]));
	}
}

static LRESULT CALLBACK ConfigDlgProc(HWND hDlgWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
		case WM_INITDIALOG:
			UpdateDialogItem(hDlgWnd);
			return TRUE;
		case WM_COMMAND:
			switch (LOWORD(wp)) {
				case IDOK:
					if (UpdateValue(hDlgWnd)) {
						SaveToIni();
						EndDialog(hDlgWnd, IDOK);
					}
					break;
				case IDCANCEL:
					EndDialog(hDlgWnd, IDCANCEL);
					break;
				case IDC_BROWSE_FFPROBE:
				case IDC_BROWSE_FFMPEG:
					BrowseExePath(hDlgWnd, LOWORD(wp) == IDC_BROWSE_FFPROBE);
					break;
				case IDC_RADIO_IMAGES:
				case IDC_RADIO_INTERVAL:
					EnableWindow(GetDlgItem(hDlgWnd, IDC_EDIT_IMAGES), LOWORD(wp) == IDC_RADIO_IMAGES);
					EnableWindow(GetDlgItem(hDlgWnd, IDC_SPIN_IMAGES), LOWORD(wp) == IDC_RADIO_IMAGES);
					EnableWindow(GetDlgItem(hDlgWnd, IDC_EDIT_INTERVAL), LOWORD(wp) == IDC_RADIO_INTERVAL);
					EnableWindow(GetDlgItem(hDlgWnd, IDC_SPIN_INTERVAL), LOWORD(wp) == IDC_RADIO_INTERVAL);
					break;
				case IDC_SET_DEFAULT:
					SendDlgItemMessage(hDlgWnd, IDC_EDIT_EXTENSION, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(table[2]));
					break;
				default:
					return FALSE;
			}
		default:
			return FALSE;
	}
	return TRUE;
}

INT PASCAL ConfigurationDlg(HWND parent, INT fnc)
{
	DEBUG_LOG(<< "ConfigurationDlg called" << std::endl);
	if (fnc == 0) { // About
		INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_UPDOWN_CLASS };
		InitCommonControlsEx(&icex);
		DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUT_DIALOG), parent, (DLGPROC)AboutDlgProc);
	} else { // Configuration
		DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_CONFIG_DIALOG), parent, (DLGPROC)ConfigDlgProc, 0);
	}
	return 0;
}

extern "C" BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call) {
		case DLL_PROCESS_ATTACH:
			CoInitializeEx(NULL, COINIT_MULTITHREADED);
			g_hInstance = (HINSTANCE)hModule;
			SetIniFileName(hModule);
			LoadFromIni();
			break;
		case DLL_PROCESS_DETACH:
			CoUninitialize();
			break;
	}
	return TRUE;
}
