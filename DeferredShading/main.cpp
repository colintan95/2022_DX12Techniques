#include <windows.h>

namespace {

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProc(hwnd, message, wparam, lparam);
}

constexpr int kWindowWidth = 1024;
constexpr int kWindowHeight = 768;

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
  ::RegisterClassEx(&window_class);

  HWND hwnd = CreateWindow(window_class.lpszClassName, L"Deferred Shading", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kWindowHeight, nullptr,
                           nullptr, hinstance, nullptr);
  ShowWindow(hwnd, cmd_show);

  MSG msg = {};
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return 0;
}