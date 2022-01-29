#include <windows.h>

#include "app.h"
#include "constants.h"

namespace {

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProc(hwnd, message, wparam, lparam);
}

}  // namespace

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hinstance, HINSTANCE prev_hinstance, LPSTR cmd_line, int cmd_show) {
  WNDCLASSEX window_class = {};
  window_class.cbSize = sizeof(WNDCLASSEX);
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = hinstance;
  window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
  window_class.lpszClassName = L"DeferredShading";
  RegisterClassEx(&window_class);

  HWND hwnd = CreateWindow(window_class.lpszClassName, L"Ray Tracing", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kWindowHeight, nullptr,
                           nullptr, hinstance, nullptr);
  ShowWindow(hwnd, cmd_show);

  App app(hwnd);

  app.Initialize();

  MSG msg = {};
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    app.RenderFrame();
  }

  app.Cleanup();

  return 0;
}