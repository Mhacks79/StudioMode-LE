// Filters.cpp
//
// The shader is compiled at run time with d3dcompiler_47.dll, which ships with
// Windows 8.1 and later, for ps_2_0 so it pairs with the fixed-function
// pre-transformed quad. D3D9 state blocks do not hold render targets, so
// those are saved and put back by hand.

#include "Filters.h"
#include "Forms.h"
#include "Offsets.h"
#include "Log.h"

#include "imgui.h"

#include <windows.h>
#include <shlwapi.h>
#include <d3d9.h>
#include <d3dcommon.h>
#include <string.h>

namespace
{
	struct Grade
	{
		float	brightness;
		float	saturation;
		float	contrast;
		float	vignette;
		float	tint[3];
		float	tintAmount;
	};

	const Grade kNeutral = { 1.0f, 1.0f, 1.0f, 0.0f, { 1.0f, 1.0f, 1.0f }, 0.0f };

	Grade						s_grade = kNeutral;

	IDirect3DPixelShader9 *		s_shader = NULL;
	IDirect3DTexture9 *			s_copy = NULL;
	IDirect3DStateBlock9 *		s_state = NULL;
	UINT						s_copyW = 0, s_copyH = 0;
	D3DFORMAT					s_copyFormat = D3DFMT_UNKNOWN;
	bool						s_shaderFailed = false;

	// image space modifier: wanted (interface) and playing (engine)
	int		s_imodIndex = -1;
	int		s_imodRequest = -1;
	float	s_imodStrength = 1.0f;
	void *	s_imodPlaying = NULL;
	bool	s_stopRequest = false;
	bool	s_resetRequest = false;
	char	s_imodFilter[64] = "";

	typedef void (__cdecl * _ImodTrigger)(void * imod, float strength, void * target);
	typedef void (__cdecl * _ImodStop)(void * imod);

	const char kShader[] =
		"sampler2D scene : register(s0);\n"
		"float4 grade : register(c0);\n"		// brightness, saturation, contrast, vignette
		"float4 tint : register(c1);\n"			// colour, amount
		"float4 main(float2 uv : TEXCOORD0) : COLOR0\n"
		"{\n"
		"	float3 c = tex2D(scene, uv).rgb * grade.x;\n"
		"	float l = dot(c, float3(0.2126, 0.7152, 0.0722));\n"
		"	c = lerp(float3(l, l, l), c, grade.y);\n"
		"	c = (c - 0.5) * grade.z + 0.5;\n"
		"	c = lerp(c, l * tint.rgb, tint.a);\n"
		"	float2 d = uv - 0.5;\n"
		"	c *= saturate(1.0 - grade.w * dot(d, d) * 2.0);\n"
		"	return float4(saturate(c), 1.0);\n"
		"}\n";

	typedef HRESULT (WINAPI * _D3DCompile)(LPCVOID src, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO * defines,
		ID3DInclude * include, LPCSTR entry, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code, ID3DBlob ** errors);

	bool IsNeutral()
	{
		return memcmp(&s_grade, &kNeutral, sizeof(Grade)) == 0;
	}

	bool CreateShader(IDirect3DDevice9 * device)
	{
		const char * const dlls[] = { "d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_43.dll" };
		_D3DCompile compile = NULL;
		for(int i = 0; i < 3 && !compile; i++)
			if(HMODULE dll = LoadLibraryA(dlls[i]))
				compile = (_D3DCompile)GetProcAddress(dll, "D3DCompile");
		if(!compile)
		{
			_ERROR("filters: no d3dcompiler DLL found, colour filters are disabled");
			return false;
		}

		ID3DBlob * code = NULL;
		ID3DBlob * errors = NULL;
		HRESULT hr = compile(kShader, sizeof(kShader) - 1, "StudioMode", NULL, NULL, "main", "ps_2_0", 0, 0, &code, &errors);
		if(FAILED(hr))
		{
			_ERROR("filters: shader compile failed %08X: %s", hr, errors ? (const char *)errors->GetBufferPointer() : "");
			if(errors) errors->Release();
			return false;
		}
		if(errors) errors->Release();

		hr = device->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &s_shader);
		code->Release();
		if(FAILED(hr))
		{
			_ERROR("filters: CreatePixelShader failed %08X", hr);
			return false;
		}
		_MESSAGE("filters: colour shader ready");
		return true;
	}

	bool EnsureResources(IDirect3DDevice9 * device, const D3DSURFACE_DESC & desc)
	{
		if(!s_shader && !s_shaderFailed && !CreateShader(device))
			s_shaderFailed = true;
		if(!s_shader) return false;

		if(s_copy && (s_copyW != desc.Width || s_copyH != desc.Height || s_copyFormat != desc.Format))
		{
			s_copy->Release();
			s_copy = NULL;
		}
		if(!s_copy)
		{
			if(FAILED(device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &s_copy, NULL)))
			{
				_ERROR("filters: cannot create a %ux%u copy of the back buffer (format %u)", desc.Width, desc.Height, (UInt32)desc.Format);
				s_shaderFailed = true;
				return false;
			}
			s_copyW = desc.Width;
			s_copyH = desc.Height;
			s_copyFormat = desc.Format;
		}
		if(!s_state && FAILED(device->CreateStateBlock(D3DSBT_ALL, &s_state)))
			return false;
		return true;
	}

	struct Vertex { float x, y, z, rhw, u, v; };
}

void Filters::Apply(IDirect3DDevice9 * device)
{
	if(IsNeutral() || s_shaderFailed) return;

	IDirect3DSurface9 * backBuffer = NULL;
	if(FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))) return;

	D3DSURFACE_DESC desc;
	if(FAILED(backBuffer->GetDesc(&desc)) || !EnsureResources(device, desc))
	{
		backBuffer->Release();
		return;
	}

	IDirect3DSurface9 * copy = NULL;
	IDirect3DSurface9 * oldTarget = NULL;
	IDirect3DSurface9 * oldDepth = NULL;
	s_copy->GetSurfaceLevel(0, &copy);
	device->GetRenderTarget(0, &oldTarget);
	device->GetDepthStencilSurface(&oldDepth);

	if(copy && SUCCEEDED(device->StretchRect(backBuffer, NULL, copy, NULL, D3DTEXF_NONE)))
	{
		s_state->Capture();

		device->SetRenderTarget(0, backBuffer);
		device->SetDepthStencilSurface(NULL);
		D3DVIEWPORT9 vp = { 0, 0, desc.Width, desc.Height, 0.0f, 1.0f };
		device->SetViewport(&vp);

		device->SetVertexShader(NULL);
		device->SetPixelShader(s_shader);
		device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
		device->SetTexture(0, s_copy);
		device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
		device->SetRenderState(D3DRS_ZENABLE, FALSE);
		device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		device->SetRenderState(D3DRS_FOGENABLE, FALSE);
		device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
		device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);

		const float constants[8] =
		{
			s_grade.brightness, s_grade.saturation, s_grade.contrast, s_grade.vignette,
			s_grade.tint[0], s_grade.tint[1], s_grade.tint[2], s_grade.tintAmount,
		};
		device->SetPixelShaderConstantF(0, constants, 2);

		// pixel centres: D3D9 needs the half-pixel offset
		const float w = (float)desc.Width - 0.5f, h = (float)desc.Height - 0.5f;
		const Vertex quad[4] =
		{
			{ -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
			{ w,     -0.5f, 0.0f, 1.0f, 1.0f, 0.0f },
			{ -0.5f, h,     0.0f, 1.0f, 0.0f, 1.0f },
			{ w,     h,     0.0f, 1.0f, 1.0f, 1.0f },
		};

		if(SUCCEEDED(device->BeginScene()))
		{
			device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
			device->EndScene();
		}

		s_state->Apply();
	}

	device->SetRenderTarget(0, oldTarget ? oldTarget : backBuffer);
	device->SetDepthStencilSurface(oldDepth);

	if(oldDepth) oldDepth->Release();
	if(oldTarget) oldTarget->Release();
	if(copy) copy->Release();
	backBuffer->Release();
}

void Filters::OnDeviceLost()
{
	if(s_copy) { s_copy->Release(); s_copy = NULL; }
	if(s_state) { s_state->Release(); s_state = NULL; }
	// the pixel shader is a managed resource and survives a reset
}

void Filters::Update()
{
	const std::vector<Forms::Entry> & imods = Forms::ImageSpaceModifiers();

	if((s_stopRequest || s_resetRequest || s_imodRequest >= 0) && s_imodPlaying)
	{
		((_ImodStop)Addr::kImodStop)(s_imodPlaying);
		s_imodPlaying = NULL;
	}
	s_stopRequest = false;

	if(s_resetRequest)
	{
		s_resetRequest = false;
		s_imodRequest = -1;
		s_imodIndex = -1;
		s_grade = kNeutral;
		return;
	}

	if(s_imodRequest >= 0 && s_imodRequest < (int)imods.size())
	{
		void * imod = imods[s_imodRequest].form;
		((_ImodTrigger)Addr::kImodTrigger)(imod, s_imodStrength, NULL);
		s_imodPlaying = imod;
		s_imodIndex = s_imodRequest;
	}
	s_imodRequest = -1;
}

void Filters::OnDeactivate()
{
	s_resetRequest = true;
	Update();
}

void Filters::RequestReset()
{
	s_resetRequest = true;
}

void Filters::Draw()
{
	const ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput;

	if(s_shaderFailed)
		ImGui::TextDisabled("Colour filters are unavailable on this system (no d3dcompiler_47.dll).");

	ImGui::BeginDisabled(s_shaderFailed);
	ImGui::SliderFloat("Brightness", &s_grade.brightness, 0.0f, 3.0f, "%.2f", flags);
	ImGui::SliderFloat("Saturation", &s_grade.saturation, 0.0f, 3.0f, "%.2f", flags);
	ImGui::SliderFloat("Contrast", &s_grade.contrast, 0.0f, 3.0f, "%.2f", flags);
	ImGui::SliderFloat("Vignette", &s_grade.vignette, 0.0f, 2.0f, "%.2f", flags);
	ImGui::SliderFloat("Tint", &s_grade.tintAmount, 0.0f, 1.0f, "%.2f", flags);
	ImGui::BeginDisabled(s_grade.tintAmount == 0.0f);
	ImGui::ColorEdit3("Tint colour", s_grade.tint, ImGuiColorEditFlags_NoInputs);
	ImGui::EndDisabled();
	ImGui::EndDisabled();

	ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));

	const std::vector<Forms::Entry> & imods = Forms::ImageSpaceModifiers();
	ImGui::InputTextWithHint("##imodfilter", "Filter effects", s_imodFilter, sizeof(s_imodFilter));
	const char * preview = (s_imodIndex >= 0 && s_imodIndex < (int)imods.size()) ? imods[s_imodIndex].editorID : "None";
	if(ImGui::BeginCombo("Game effect", preview, ImGuiComboFlags_HeightLarge))
	{
		for(int i = 0; i < (int)imods.size(); i++)
		{
			if(s_imodFilter[0] && !StrStrIA(imods[i].editorID, s_imodFilter)) continue;
			ImGui::PushID(i);
			if(ImGui::Selectable(imods[i].editorID, i == s_imodIndex))
				s_imodRequest = i;
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	ImGui::SliderFloat("Effect strength", &s_imodStrength, 0.1f, 1.0f, "%.2f", flags);
	if(ImGui::Button("Stop effect"))
	{
		s_stopRequest = true;
		s_imodIndex = -1;
	}
	ImGui::TextDisabled("Game effects (image space modifiers) may fade out on their own.");
}
