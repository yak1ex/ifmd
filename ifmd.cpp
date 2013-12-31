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
	"Markdown�t�@�C��"
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

INT PASCAL GetPictureInfo(LPSTR buf, LONG len, UINT flag, SPI_PICTINFO *lpInfo)
{
	return SPI_ERR_NOT_IMPLEMENTED;
}

INT PASCAL GetPicture(LPSTR buf, LONG len, UINT flag, HANDLE *pHBInfo, HANDLE *pHBm, FARPROC lpPrgressCallback, LONG lData)
{
	return SPI_ERR_NOT_IMPLEMENTED;
}

INT PASCAL GetPreview(LPSTR buf, LONG len, UINT flag, HANDLE *pHBInfo, HANDLE *pHBm, FARPROC lpPrgressCallback, LONG lData)
{
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

static std::string g_sIniFileName; // ini �t�@�C����

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
    // �{����2�o�C�g�����΍􂪕K�v�����A�v���O�C�����ɓ��{��͂Ȃ��ƑO��Ƃ��Ď蔲��
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
			g_hInstance = (HINSTANCE)hModule;
			SetIniFileName(hModule);
			LoadFromIni();
			break;
	}
	return TRUE;
}