// Screenshot.cpp
//
// GetRenderTargetData needs a single-sampled source, so a multisampled back
// buffer is resolved with StretchRect first. The pixels are converted to
// 8-bit BGRA here and encoded by WIC, which ships with Windows.

#include "Screenshot.h"
#include "Log.h"

#include <windows.h>
#include <d3d9.h>
#include <wincodec.h>
#include <shlobj.h>

#include <vector>

namespace
{
	std::string			s_folder;
	CRITICAL_SECTION	s_resultLock;
	bool				s_lockReady = false;
	bool				s_hasResult = false;
	bool				s_resultOk = false;
	std::string			s_resultMessage;
	UInt32				s_counter = 0;

	struct Job
	{
		UInt32				width;
		UInt32				height;
		std::vector<UInt8>	pixels;		// BGRA, top-down
		std::string			path;
	};

	void EnsureLock()
	{
		if(!s_lockReady) { InitializeCriticalSection(&s_resultLock); s_lockReady = true; }
	}

	void SetResult(bool ok, const std::string & message)
	{
		EnterCriticalSection(&s_resultLock);
		s_hasResult = true;
		s_resultOk = ok;
		s_resultMessage = message;
		LeaveCriticalSection(&s_resultLock);
	}

	std::string PhotoFolder()
	{
		if(!s_folder.empty()) return s_folder;

		char docs[MAX_PATH];
		if(FAILED(SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, docs)))
			return "";
		return std::string(docs) + "\\My Games\\Skyrim\\StudioMode";
	}

	bool MakeFolder(const std::string & folder)
	{
		// creates every missing level; ERROR_ALREADY_EXISTS is fine
		const int r = SHCreateDirectoryExA(NULL, folder.c_str(), NULL);
		return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
	}

	std::string NextPath(const std::string & folder)
	{
		SYSTEMTIME t;
		GetLocalTime(&t);
		char name[96];
		sprintf_s(name, "\\StudioMode_%04u%02u%02u_%02u%02u%02u_%03u.png",
			t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, (s_counter++) % 1000);
		return folder + name;
	}

	// The PNG encoder has no 32bpp BGRX format: asked for one, it offers
	// 24bpp BGR instead (MSDN, "PNG Format Overview"). Take what it offers.
	HRESULT WritePng(const Job & job, const char *& step)
	{
		IWICImagingFactory *	factory = NULL;
		IWICStream *			stream = NULL;
		IWICBitmapEncoder *		encoder = NULL;
		IWICBitmapFrameEncode *	frame = NULL;

		wchar_t wpath[MAX_PATH];
		MultiByteToWideChar(CP_ACP, 0, job.path.c_str(), -1, wpath, MAX_PATH);

		HRESULT hr;
		step = "create the WIC factory";	hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
		if(SUCCEEDED(hr)) { step = "create the stream";		hr = factory->CreateStream(&stream); }
		if(SUCCEEDED(hr)) { step = "open the file";			hr = stream->InitializeFromFilename(wpath, GENERIC_WRITE); }
		if(SUCCEEDED(hr)) { step = "create the PNG encoder";	hr = factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder); }
		if(SUCCEEDED(hr)) { step = "start the encoder";		hr = encoder->Initialize(stream, WICBitmapEncoderNoCache); }
		if(SUCCEEDED(hr)) { step = "create the frame";		hr = encoder->CreateNewFrame(&frame, NULL); }
		if(SUCCEEDED(hr)) { step = "start the frame";			hr = frame->Initialize(NULL); }
		if(SUCCEEDED(hr)) { step = "set the size";			hr = frame->SetSize(job.width, job.height); }

		WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
		if(SUCCEEDED(hr)) { step = "set the pixel format";	hr = frame->SetPixelFormat(&format); }

		if(SUCCEEDED(hr))
		{
			step = "write the pixels";
			if(format == GUID_WICPixelFormat24bppBGR)
			{
				// drop the X byte of each BGRX pixel
				const UInt32 stride = job.width * 3;
				std::vector<UInt8> packed(stride * job.height);
				const UInt8 * src = &job.pixels[0];
				UInt8 * dst = &packed[0];
				for(UInt32 i = 0, n = job.width * job.height; i < n; i++, src += 4, dst += 3)
				{
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
				}
				hr = frame->WritePixels(job.height, stride, (UINT)packed.size(), &packed[0]);
			}
			else if(format == GUID_WICPixelFormat32bppBGRA || format == GUID_WICPixelFormat32bppBGR)
				hr = frame->WritePixels(job.height, job.width * 4, (UINT)job.pixels.size(), (BYTE *)&job.pixels[0]);
			else
				hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
		}

		if(SUCCEEDED(hr)) { step = "finish the frame";		hr = frame->Commit(); }
		if(SUCCEEDED(hr)) { step = "finish the file";		hr = encoder->Commit(); }

		if(frame) frame->Release();
		if(encoder) encoder->Release();
		if(stream) stream->Release();
		if(factory) factory->Release();
		return hr;
	}

	DWORD WINAPI WriteThread(void * param)
	{
		Job * job = (Job *)param;

		const HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
		const char * step = "";
		const HRESULT hr = WritePng(*job, step);
		if(SUCCEEDED(co)) CoUninitialize();

		if(SUCCEEDED(hr))
		{
			_MESSAGE("screenshot: wrote %s (%ux%u)", job->path.c_str(), job->width, job->height);
			SetResult(true, job->path);
		}
		else
		{
			_ERROR("screenshot: writing %s failed: could not %s, HRESULT %08X", job->path.c_str(), step, hr);
			DeleteFileA(job->path.c_str());
			SetResult(false, "Could not write the photo");
		}

		delete job;
		return 0;
	}

	bool Convert(const D3DLOCKED_RECT & locked, D3DFORMAT format, Job & job)
	{
		job.pixels.resize(job.width * job.height * 4);
		UInt8 * out = &job.pixels[0];

		for(UInt32 y = 0; y < job.height; y++)
		{
			const UInt8 * row = (const UInt8 *)locked.pBits + y * locked.Pitch;
			UInt8 * dst = out + y * job.width * 4;

			switch(format)
			{
				case D3DFMT_X8R8G8B8:
				case D3DFMT_A8R8G8B8:
					memcpy(dst, row, job.width * 4);
					for(UInt32 x = 0; x < job.width; x++) dst[x * 4 + 3] = 0xFF;
					break;

				case D3DFMT_A2R10G10B10:
					for(UInt32 x = 0; x < job.width; x++)
					{
						const UInt32 p = ((const UInt32 *)row)[x];
						dst[x * 4 + 0] = (UInt8)((p >> 2) & 0xFF);		// B
						dst[x * 4 + 1] = (UInt8)((p >> 12) & 0xFF);		// G
						dst[x * 4 + 2] = (UInt8)((p >> 22) & 0xFF);		// R
						dst[x * 4 + 3] = 0xFF;
					}
					break;

				default:
					return false;
			}
		}
		return true;
	}
}

void Screenshot::SetFolder(const std::string & folder)
{
	s_folder = folder;
	while(!s_folder.empty() && (s_folder[s_folder.size() - 1] == '\\' || s_folder[s_folder.size() - 1] == '/'))
		s_folder.erase(s_folder.size() - 1);
}

bool Screenshot::Capture(IDirect3DDevice9 * device)
{
	EnsureLock();

	const std::string folder = PhotoFolder();
	if(folder.empty() || !MakeFolder(folder))
	{
		_ERROR("screenshot: cannot create the folder '%s'", folder.c_str());
		SetResult(false, "Could not create the photo folder");
		return false;
	}

	IDirect3DSurface9 * backBuffer = NULL;
	IDirect3DSurface9 * resolved = NULL;
	IDirect3DSurface9 * system = NULL;
	bool ok = false;
	Job * job = NULL;

	do
	{
		if(FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))) break;

		D3DSURFACE_DESC desc;
		if(FAILED(backBuffer->GetDesc(&desc))) break;

		IDirect3DSurface9 * source = backBuffer;
		if(desc.MultiSampleType != D3DMULTISAMPLE_NONE)
		{
			if(FAILED(device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, NULL))) break;
			if(FAILED(device->StretchRect(backBuffer, NULL, resolved, NULL, D3DTEXF_NONE))) break;
			source = resolved;
		}

		if(FAILED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &system, NULL))) break;
		if(FAILED(device->GetRenderTargetData(source, system))) break;

		D3DLOCKED_RECT locked;
		if(FAILED(system->LockRect(&locked, NULL, D3DLOCK_READONLY))) break;

		job = new Job;
		job->width = desc.Width;
		job->height = desc.Height;
		job->path = NextPath(folder);
		ok = Convert(locked, desc.Format, *job);
		system->UnlockRect();

		if(!ok) _ERROR("screenshot: back buffer format %u is not supported", (UInt32)desc.Format);
	}
	while(false);

	if(system) system->Release();
	if(resolved) resolved->Release();
	if(backBuffer) backBuffer->Release();

	if(!ok)
	{
		delete job;
		SetResult(false, "Could not read the screen");
		return false;
	}

	HANDLE thread = CreateThread(NULL, 0, WriteThread, job, 0, NULL);
	if(!thread)
	{
		delete job;
		SetResult(false, "Could not start the photo writer");
		return false;
	}
	CloseHandle(thread);
	return true;
}

bool Screenshot::PollResult(bool & ok, std::string & message)
{
	EnsureLock();
	EnterCriticalSection(&s_resultLock);
	const bool has = s_hasResult;
	if(has)
	{
		ok = s_resultOk;
		message = s_resultMessage;
		s_hasResult = false;
	}
	LeaveCriticalSection(&s_resultLock);
	return has;
}
