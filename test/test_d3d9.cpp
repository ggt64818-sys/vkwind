// vkwind test app — Step 1: Load DLL, create device, Clear+Present
// Uses our custom d3d9_types.h so the vtable layout matches our DLL exactly.
// Build as a standalone EXE that runs alongside d3d9.dll in build/.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstdint>

// Include our custom D3D9 types — this gives us the exact vtable layout
#include "../src/d3d9/d3d9_types.h"

// ---- Dynamically loaded function pointers ----
static IDirect3D9* (WINAPIV *pDirect3DCreate9)(uint32_t SDKVersion) = nullptr;

// ---- Global state ----
static IDirect3D9*       g_d3d   = nullptr;
static IDirect3DDevice9* g_dev   = nullptr;
static bool              g_running = true;

// ---- Forward decls ----
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---- Helpers ----
static void log_msg(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  OutputDebugStringA(buf);
  printf("%s", buf);
}

// ---- WinMain ----
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
  log_msg("[test] vkwind test app starting...\n");

  // ---- Step 1: Load d3d9.dll ----
  log_msg("[test] About to LoadLibraryA...\n"); fflush(stdout);
  HMODULE hD3D = LoadLibraryA("d3d9.dll");
  log_msg("[test] LoadLibraryA returned %p (GetLastError=%u)\n", (void*)hD3D, GetLastError()); fflush(stdout);
  if (!hD3D) {
    log_msg("[test] FAILED to load d3d9.dll (error %u)\n", GetLastError());
    return 1;
  }
  log_msg("[test] d3d9.dll loaded at %p\n", (void*)hD3D); fflush(stdout);

  // ---- Step 2: Resolve Direct3DCreate9 ----
  pDirect3DCreate9 = (decltype(pDirect3DCreate9))GetProcAddress(hD3D, "Direct3DCreate9");
  if (!pDirect3DCreate9) {
    log_msg("[test] FAILED to find Direct3DCreate9 export\n");
    FreeLibrary(hD3D);
    return 1;
  }
  log_msg("[test] Direct3DCreate9 resolved at %p\n", (void*)pDirect3DCreate9);

  // ---- Step 3: Create Win32 window ----
  WNDCLASSEXA wc = {};
  wc.cbSize        = sizeof(wc);
  wc.style         = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc   = WndProc;
  wc.hInstance     = hInstance;
  wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = "VKWIND_TEST";
  RegisterClassExA(&wc);

  HWND hWnd = CreateWindowExA(
    0, "VKWIND_TEST", "vkwind Test Window",
    WS_OVERLAPPEDWINDOW | WS_VISIBLE,
    100, 100, 800, 600,
    nullptr, nullptr, hInstance, nullptr);

  if (!hWnd) {
    log_msg("[test] FAILED to create window\n");
    FreeLibrary(hD3D);
    return 1;
  }
  log_msg("[test] Window created\n");

  // ---- Step 4: Call Direct3DCreate9 ----
  g_d3d = pDirect3DCreate9(D3D_SDK_VERSION);
  if (!g_d3d) {
    log_msg("[test] FAILED: Direct3DCreate9 returned NULL\n");
    DestroyWindow(hWnd);
    FreeLibrary(hD3D);
    return 1;
  }
  log_msg("[test] IDirect3D9 created at %p\n", (void*)g_d3d);

  // ---- Step 5: Query adapter info ----
  UINT adapterCount = g_d3d->GetAdapterCount();
  log_msg("[test] Adapter count: %u\n", adapterCount);

  for (UINT i = 0; i < adapterCount; i++) {
    D3DADAPTER_IDENTIFIER9 ident = {};
    HRESULT hr = g_d3d->GetAdapterIdentifier(i, 0, &ident);
    if (SUCCEEDED(hr)) {
      log_msg("[test]   Adapter %u: %s (vendor=0x%x, device=0x%x)\n",
              i, ident.Description, ident.VendorId, ident.DeviceId);
    }
  }

  // ---- Step 6: Get display mode ----
  D3DDISPLAYMODE mode = {};
  HRESULT hr = g_d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);
  if (SUCCEEDED(hr)) {
    log_msg("[test] Display mode: %ux%u fmt=%u refresh=%uHz\n",
            mode.Width, mode.Height, (int)mode.Format, mode.RefreshRate);
  }

  // ---- Step 7: Create device ----
  D3DPRESENT_PARAMETERS pp = {};
  pp.Windowed               = TRUE;
  pp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
  pp.hDeviceWindow          = hWnd;
  pp.BackBufferWidth        = 800;
  pp.BackBufferHeight       = 600;
  pp.BackBufferFormat       = D3DFMT_X8R8G8B8;
  pp.EnableAutoDepthStencil = TRUE;
  pp.AutoDepthStencilFormat = D3DFMT_D16;
  pp.PresentationInterval   = D3DPRESENT_INTERVAL_ONE;

  hr = g_d3d->CreateDevice(
    D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
    D3DCREATE_HARDWARE_VERTEXPROCESSING,
    &pp, &g_dev);

  if (FAILED(hr)) {
    log_msg("[test] FAILED: CreateDevice returned 0x%08x\n", (uint32_t)hr);
    g_d3d->Release();
    DestroyWindow(hWnd);
    FreeLibrary(hD3D);
    return 1;
  }
  log_msg("[test] IDirect3DDevice9 created at %p\n", (void*)g_dev);

  // ---- Step 8: Main loop — Clear + DrawTriangle + Present ----
  log_msg("[test] Entering main loop (Clear+Draw+Present)...\n");

  // Hardcoded triangle: position(float3) + color(float4) = 28 bytes stride
  struct Vertex { float x, y, z; float r, g, b, a; };
  Vertex triangle[3] = {
    {  0.0f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f, 1.0f },  // top — red
    { -0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f, 1.0f },  // bottom-left — green
    {  0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f, 1.0f },  // bottom-right — blue
  };

  MSG msg = {};
  while (g_running) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        g_running = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    if (!g_running) break;

    // Clear to cornflower blue (D3D classic)
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                 D3DCOLOR_XRGB(100, 149, 237), 1.0f, 0);

    // Draw hardcoded triangle
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, sizeof(Vertex));

    hr = g_dev->Present(nullptr, nullptr, nullptr, nullptr);
    if (FAILED(hr)) {
      log_msg("[test] Present failed: 0x%08x\n", (uint32_t)hr);
      // Don't exit — try again next frame
    }
  }

  // ---- Step 9: Cleanup ----
  log_msg("[test] Cleaning up...\n");

  UINT refs = g_dev->Release();
  log_msg("[test] Device released (refs=%u)\n", refs);

  refs = g_d3d->Release();
  log_msg("[test] D3D9 released (refs=%u)\n", refs);

  DestroyWindow(hWnd);
  FreeLibrary(hD3D);

  log_msg("[test] Done.\n");
  return 0;
}

// ---- Window proc ----
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_DESTROY:
      g_running = false;
      PostQuitMessage(0);
      return 0;
    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE) {
        g_running = false;
        PostQuitMessage(0);
        return 0;
      }
      break;
  }
  return DefWindowProcA(hWnd, msg, wParam, lParam);
}
