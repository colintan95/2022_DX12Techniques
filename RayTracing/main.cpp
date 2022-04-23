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
int WINAPI WinMain(HINSTANCE hinstance, HINSTANCE prevHinstance, LPSTR cmdLine, int cmdShow) {
  WNDCLASSEX windowClass{};
  windowClass.cbSize = sizeof(WNDCLASSEX);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = WindowProc;
  windowClass.hInstance = hinstance;
  windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
  windowClass.lpszClassName = L"RayTracing";
  RegisterClassEx(&windowClass);

  HWND hwnd = CreateWindow(windowClass.lpszClassName, L"Ray Tracing", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, k_windowWidth, k_windowHeight, nullptr,
                           nullptr, hinstance, nullptr);
  ShowWindow(hwnd, cmdShow);

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