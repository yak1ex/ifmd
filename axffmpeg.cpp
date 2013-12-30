/***********************************************************************/
/*                                                                     */
/* axffmpeg.cpp: Source file for axffmpeg                              */
/*                                                                     */
/*     Copyright (C) 2012,2013 Yak! / Yasutaka ATARASHI                */
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
	"00AM",
	"Plugin to handle a movie as an image container - v0.03 (2013/02/05) Written by Yak!",
	"*.3g2;*.3gp;*.avi;*.f4v;*.flv;*.m4v;*.mkv;*.mov;*.mp4;*.mpeg;*.mpg;*.ogg;*.ogm;*.webm;*.wmv",
	"動画ファイル"
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

static std::pair<HANDLE, HANDLE> InvokeProcess(const std::string &sCommandLine)
{
	STARTUPINFO si = { sizeof(STARTUPINFO) };
	HANDLE hRead1, hWrite1, hRead2, hWrite2;
	SECURITY_ATTRIBUTES saAttr; 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL; 
	CreatePipe(&hRead1, &hWrite1, &saAttr, 0);
	SetHandleInformation(hRead1, HANDLE_FLAG_INHERIT, 0);
	CreatePipe(&hRead2, &hWrite2, &saAttr, 0);
	SetHandleInformation(hWrite2, HANDLE_FLAG_INHERIT, 0);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = hRead2;
	si.hStdOutput = hWrite1;
	si.hStdError = hWrite1;
	PROCESS_INFORMATION pi;
	std::vector<char> vCommandLine(sCommandLine.begin(), sCommandLine.end());
	vCommandLine.push_back(0);
	if(CreateProcess(0, &vCommandLine[0], 0, 0, TRUE, CREATE_NO_WINDOW, 0, 0, &si, &pi)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		CloseHandle(hWrite1);
		CloseHandle(hRead2);
		return std::make_pair(hRead1, hWrite2);
	}
	return std::pair<HANDLE, HANDLE>(0, 0);
}

struct MetaInfo
{
	DWORD duration, width, height;
};

// Currently, 24 bits BMP is forced
static unsigned long bmpsize(DWORD dwWidth, DWORD dwHeight)
{
	return ((dwWidth * 3 + 3) / 4 * 4) * dwHeight + 54;
}

static void GetMetaInfo(LPSTR filename, MetaInfo &mi)
{
	if(g_sFFprobePath.empty() && !g_fWarned) {
		g_fWarned = true;
		MessageBox(NULL, "Path of ffprobe.exe is not specified.", "axffmpeg.spi", MB_ICONEXCLAMATION | MB_OK);
	}
	if(g_sFFmpegPath.empty() && !g_fWarned) {
		g_fWarned = true;
		MessageBox(NULL, "Path of ffmpeg.exe is not specified.", "axffmpeg.spi", MB_ICONEXCLAMATION | MB_OK);
	}
	std::string s = std::string("\"") + g_sFFprobePath + "\" \"" + filename + "\" -v warning -show_entries format=duration:stream=width,height -of csv";
	std::pair<HANDLE, HANDLE> handle_pair = InvokeProcess(s);
	mi.duration = mi.width = mi.height = 0;
	if(handle_pair.first) {
		std::string s;
		DWORD dwLen;
		std::vector<char> buf(2048);
		while(ReadFile(handle_pair.first, &buf[0], buf.size(), &dwLen, 0)) {
			s += std::string(&buf[0], &buf[0] + dwLen);
		}
		if(dwLen) s += std::string(&buf[0], &buf[0] + dwLen);
		const char* p = s.c_str();
		DEBUG_LOG(<< "GetMetaInfo(" << filename << ") : " << p << std::endl);
		while(*p) {
			switch(*p) {
			case 's': // stream
				while(*p && *p != ',') ++p;
				if(*p) ++p;
				if('0' <= *p && *p <= '9') {
					mi.width = std::max(mi.width, std::strtoul(p, 0, 10));
					while(*p && *p != ',') ++p;
					if(*p) ++p;
					mi.height = std::max(mi.height, std::strtoul(p, 0, 10));
				}
				break;
			case 'f': // format
				while(*p && *p != ',') ++p;
				if(*p) ++p;
				mi.duration = std::strtoul(p, 0, 10);
				break;
			}
			while(*p && *p != '\n') ++p;
			if(*p) ++p;
		}
		CloseHandle(handle_pair.first);
		CloseHandle(handle_pair.second);
	}
	DEBUG_LOG(<< "GetMetaInfo(" << filename << ") : " << mi.duration << ',' << mi.width << ',' << mi.height << std::endl);
	DEBUG_LOG(<< bmpsize(mi.width, mi.height) << std::endl);
}

static unsigned long filesize(const char* filename)
{
	struct stat st;
	stat(filename, &st);
	return st.st_size;
}

static Key make_key(const char* filename)
{
	return std::make_pair(std::string(filename), filesize(filename));
}

static INT SetArchiveInfo(const std::vector<SPI_FILEINFO> &v, HLOCAL *lphInf)
{
	std::size_t size = v.size() * sizeof(SPI_FILEINFO);
	std::size_t tsize = size + sizeof(SPI_FILEINFO); // for terminator
	*lphInf = LocalAlloc(LMEM_MOVEABLE, tsize);
	LPVOID pv = LocalLock(*lphInf);
	ZeroMemory(pv, tsize);
	CopyMemory(pv, &v[0], size);
	LocalUnlock(*lphInf);
	return SPI_ERR_NO_ERROR;
}

static time_t filetime(const char* filename)
{
	struct stat st;
	stat(filename, &st);
	return st.st_mtime;
}

static void SetErrorImage(std::vector<char> &v2)
{
	DEBUG_LOG(<< "SetErrorImage(" << v2.size() << ')' << std::endl);
	HRSRC hResource = FindResource(g_hInstance, MAKEINTRESOURCE(IDR_ERROR_IMAGE), RT_RCDATA);
	DWORD dwSize = SizeofResource(g_hInstance, hResource);
	HGLOBAL handle = LoadResource(g_hInstance, hResource);
	char* pErrorImage = static_cast<char*>(LockResource(handle));
	v2.assign(pErrorImage, pErrorImage + dwSize);
}

static void GetPictureAtPos(std::vector<char> &v2, DWORD dwPos, LPSTR filename)
{
	DEBUG_LOG(<< "GetPictureAtPos(" << v2.size() << ',' << dwPos << ',' << filename << ')' << std::endl);
	char szBuf[20];
	wsprintf(szBuf, "%d:%02d:%02d", dwPos / 3600, dwPos / 60 % 60, dwPos % 60);
// Force 24bits BMP
	std::string s = std::string("\"") + g_sFFmpegPath + "\" -ss " + szBuf + " -i \"" + filename + "\" -v quiet -f image2pipe -frames 1 -pix_fmt rgb24 -vcodec bmp -";
	std::pair<HANDLE, HANDLE> handle_pair = InvokeProcess(s);
	if(handle_pair.first) {
		DWORD dwLen;
		std::vector<char> buf(32768);
		while(ReadFile(handle_pair.first, &buf[0], buf.size(), &dwLen, 0)) {
			v2.insert(v2.end(), &buf[0], &buf[0] + dwLen);
		}
		if(dwLen) v2.insert(v2.end(), &buf[0], &buf[0] + dwLen);
		CloseHandle(handle_pair.first);
		CloseHandle(handle_pair.second);
	}
	DEBUG_LOG(<< "GetPictureAtPos(" << filename << ") : " << v2.size() << std::endl);
	if(v2.size() == 0) {
		SetErrorImage(v2);
	}
}

static void SetArchiveInfo(std::vector<SPI_FILEINFO> &v1, DWORD dwSize, DWORD dwPos, DWORD timestamp)
{
	SPI_FILEINFO sfi = {
		{ 'F', 'F', 'M', 'P', 'E', 'G', '\0', '\0' },
		dwPos,
		dwSize,
		dwSize,
		timestamp,
	};
	wsprintf(sfi.filename, "%08d.bmp", v1.size());
	v1.push_back(sfi);
}

static void GetArchiveInfoImp(std::vector<SPI_FILEINFO> &v1, std::vector<std::vector<char> > &v2, LPSTR filename)
{
	DEBUG_LOG(<< "GetArchiveInfoImp(" << v1.size() << ',' << v2.size() << ',' << filename << ')' << std::endl);

	DWORD timestamp = filetime(filename);
	MetaInfo mi;
	GetMetaInfo(filename, mi);
	DWORD dwDuration = mi.duration;
	if(dwDuration == 0) {
		v2.push_back(std::vector<char>());
		SetErrorImage(v2.back());
		SetArchiveInfo(v1, v2.back().size(), 0, timestamp);
		return;
	}
	DWORD dwDenom = g_fImages  ? g_nImages : 1;
	DWORD dwDiv = g_fImages ? dwDuration : std::min<DWORD>(g_nInterval, dwDuration);
	DWORD dwPos = dwDuration * dwDenom - (dwDuration * dwDenom / dwDiv - 1) * dwDiv;
	dwDenom *= 2;
	dwDiv *= 2;

	const DWORD dwSize = bmpsize(mi.width, mi.height);

	while(dwPos < dwDuration * dwDenom) {
		v2.push_back(std::vector<char>());
		SetArchiveInfo(v1, dwSize, dwPos / dwDenom, timestamp);
		dwPos += dwDiv;
	}
}

static INT GetArchiveInfoImp(HLOCAL *lphInf, LPSTR filename)
{
	DEBUG_LOG(<< "GetArchiveInfoImp(" << filename << ')' << std::endl);

	if(filename != NULL) {
		DEBUG_LOG(<< "GetArchiveInfoImp - Filename specified: check cache" << std::endl);
		Key key(make_key(filename));
		if(g_cache.count(key) != 0) {
			if(lphInf) SetArchiveInfo(g_cache[key].first, lphInf);
			return SPI_ERR_NO_ERROR;
		}
	}

	try {
		std::vector<SPI_FILEINFO> v1;
		std::vector<std::vector<char> > v2;
		GetArchiveInfoImp(v1, v2, filename);

		if(lphInf) SetArchiveInfo(v1, lphInf);

		if(filename != NULL) {
			DEBUG_LOG(<< "GetArchiveInfoImp - Filename specified: set cache" << std::endl);
			Key key(make_key(filename));
			g_cache[key].first.swap(v1);
			g_cache[key].second.swap(v2);
		}
	} catch(std::exception &e) {
		MessageBox(NULL, e.what(), "axffmpeg.spi", MB_OK);
		return SPI_ERR_BROKEN_DATA;
	} catch(...) {
		return SPI_ERR_BROKEN_DATA;
	}

	return SPI_ERR_NO_ERROR;
}

INT PASCAL GetArchiveInfo(LPSTR buf, LONG len, UINT flag, HLOCAL *lphInf)
{
	DEBUG_LOG(<< "GetArchiveInfo(" << std::string(buf, std::min<DWORD>(len, 1024)) << ',' << len << ',' << std::hex << std::setw(8) << std::setfill('0') << flag << ',' << lphInf << ')' << std::endl);
	switch(flag & 7) {
	case 0:
		DEBUG_LOG(<< "File" << std::endl); // By afx
		return GetArchiveInfoImp(lphInf, buf);
	case 1:
		DEBUG_LOG(<< "Memory" << std::endl);
		return SPI_ERR_NOT_IMPLEMENTED;
	}
	return SPI_ERR_INTERNAL_ERROR;
}

INT PASCAL GetFileInfo(LPSTR buf, LONG len, LPSTR filename, UINT flag, SPI_FILEINFO *lpInfo)
{
	DEBUG_LOG(<< "GetFileInfo(" << std::string(buf, std::min<DWORD>(len, 1024)) << ',' << len << ',' << filename << ','<< std::hex << std::setw(8) << std::setfill('0') << flag << ',' << lpInfo << ')' << std::endl);
	if(flag & 128) {
		DEBUG_LOG(<< "Case-insensitive" << std::endl); // By afx
	} else {
		DEBUG_LOG(<< "Case-sensitive" << std::endl);
	}
	switch(flag & 7) {
	case 0:
		DEBUG_LOG(<< "File" << std::endl); // By afx
		break;
	case 1:
		DEBUG_LOG(<< "Memory" << std::endl);
		break;
	}
	HLOCAL hInfo;
	if(GetArchiveInfo(buf, len, flag&7, &hInfo) == SPI_ERR_NO_ERROR) {
		int (WINAPI *compare)(LPCSTR, LPCSTR) = flag&7 ? lstrcmpi : lstrcmp;
		std::size_t size = LocalSize(hInfo);
		SPI_FILEINFO *p = static_cast<SPI_FILEINFO*>(LocalLock(hInfo)), *cur = p;
		std::size_t count = size / sizeof(SPI_FILEINFO);
		while(cur < p + count && *static_cast<char*>(static_cast<void*>(cur)) != '\0') {
			if(!compare(cur->filename, filename)) {
				CopyMemory(lpInfo, cur, sizeof(SPI_FILEINFO));
				LocalUnlock(hInfo);
				LocalFree(hInfo);
				return SPI_ERR_NO_ERROR;
			}
			++cur;
		}
		LocalUnlock(hInfo);
		LocalFree(hInfo);
	}
	return SPI_ERR_INTERNAL_ERROR;
}

INT PASCAL GetFile(LPSTR buf, LONG len, LPSTR dest, UINT flag, FARPROC prgressCallback, LONG lData)
{
	DEBUG_LOG(<< "GetFile(" << ',' << len << ',' << ',' << flag << ',' << prgressCallback << ',' << lData << ')' << std::endl);
	switch(flag & 7) {
	case 0:
	  {
		DEBUG_LOG(<< "Source is file" << std::endl); // By afx
		break;
	  }
	case 1:
		DEBUG_LOG(<< "Source is memory" << std::endl);
		return SPI_ERR_NOT_IMPLEMENTED; // We cannot handle this case.
	}
	switch((flag>>8) & 7) {
	case 0:
		DEBUG_LOG(<< "Destination is file: " << dest << std::endl);
		break;
	case 1:
		DEBUG_LOG(<< "Destination is memory: " << static_cast<void*>(dest) << std::endl); // By afx
		break;
	}
	if(GetArchiveInfo(buf, len, flag&7, 0) == SPI_ERR_NO_ERROR) {
		DEBUG_LOG(<< "GetFile(): GetArvhiveInfo() returned" << std::endl);
		Key key(make_key(buf));
		if(g_cache.count(key) == 0) {
			DEBUG_LOG(<< "GetFile(): " << buf << " not cached" << std::endl);
			return SPI_ERR_INTERNAL_ERROR;
		}
		Value &value = g_cache[key];
		std::size_t size = value.first.size();
		for(std::size_t i = 0; i < size; ++i) {
			if(value.first[i].position == std::size_t(len)) {
				DEBUG_LOG(<< "GetFile(): position found" << std::endl);
				if(value.second[i].size() == 0) {
					GetPictureAtPos(value.second[i], value.first[i].position, buf);
				}
				if((flag>>8) & 7) { // memory
					if(dest) {
						DEBUG_LOG(<< "GetFile(): size: " << value.second[i].size() << " head: " << value.second[i][0] << value.second[i][1] << value.second[i][2] << value.second[i][3] << std::endl);
						HANDLE *phResult = static_cast<HANDLE*>(static_cast<void*>(dest));
						*phResult = LocalAlloc(LMEM_MOVEABLE, value.second[i].size());
						void* p = LocalLock(*phResult);
						CopyMemory(p, &value.second[i][0], value.second[i].size());
						LocalUnlock(*phResult);
						return SPI_ERR_NO_ERROR;
					}
					return SPI_ERR_INTERNAL_ERROR;
				} else { // file
					std::string s(dest);
					s += '\\';
					s += value.first[i].filename;
					FILE *fp = std::fopen(s.c_str(), "wb");
					fwrite(&value.second[i][0], value.second[i].size(), 1, fp);
					fclose(fp);
					return SPI_ERR_NO_ERROR;
				}
			}
		}
	}
	DEBUG_LOG(<< "GetFile(): position not found" << std::endl);
	return SPI_ERR_INTERNAL_ERROR;
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
			g_hInstance = (HINSTANCE)hModule;
			SetIniFileName(hModule);
			LoadFromIni();
			break;
	}
	return TRUE;
}
