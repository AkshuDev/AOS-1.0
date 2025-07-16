#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <unistd.h>
#include <tchar.h>

#include "../headers/aos.h"
#include "cmdline.h"

static HWND g_hWnd;

void SetWindowBackgroundColor(HWND hwnd, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) {
        SetWindowLongPtr(hwnd, GWL_STYLE, GetWindowLongPtr(hwnd, GWL_STYLE) & ~WS_POPUPWINDOW);
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, GetWindowLongPtr(hwnd, GWL_EXSTYLE) | WS_EX_CLIENTEDGE);
        SetClassLongPtr(GetClassLongPtr(hwnd, 0), GCL_HBRBACKGROUND, (LONG_PTR)brush);
        InvalidateRect(hwnd, NULL, FALSE);
    } else {
        MessageBox(NULL, _T("Error setting the Background Color"), _T("AOS Cmdline"), 0);
    }
}

// Function to display the prompt and user input
void DisplayPrompt(const char* prompt) {
    static TCHAR buffer[256] = {0}; // Buffer for user input
    static int buffer_index = 0;

    // Clear the previous line (if any)
    HDC hdc = GetDC(g_hWnd);
    FillRect(hdc, & (RECT){0, GetSystemMetrics(SM_CYSIZEFRAME) + 1, WINDOW_WIDTH, 20}, (HBRUSH) GetStockObject(WHITE_BRUSH));
    ReleaseDC(g_hWnd, hdc);

    // Display the prompt
    TextOut(GetDC(g_hWnd), 4, GetSystemMetrics(SM_CYSIZEFRAME) + 3, prompt, _tcslen(prompt));

    // Display user input
    TextOut(GetDC(g_hWnd), _tcslen(prompt) * 8, GetSystemMetrics(SM_CYSIZEFRAME) + 3, buffer, buffer_index);
    ReleaseDC(g_hWnd, hdc);
}

// Function to handle user input
void HandleUserInput(TCHAR ch) {
    char buffer[prompt_buffer];
    int buffer_index = 0;

    if (ch == '\r') { // Enter key pressed
        // Process the user input in buffer (replace with your actual command execution logic)
        printf("User input: %s\n", buffer);
        buffer_index = 0; // Reset buffer for new prompt
        DisplayPrompt(base_prompt); // Display new prompt
    } else if (ch == '\b' && buffer_index > 0) { // Backspace key pressed
        buffer_index--;
        DisplayPrompt(base_prompt);
    } else if (ch >= 32 && ch <= 126 && buffer_index < sizeof(buffer) - 1) { // Printable characters
        buffer[buffer_index++] = ch;
        DisplayPrompt(base_prompt);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC hdc;

    switch (message) {
        case WM_PAINT:

        hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        break;
        case WM_DESTROY:
        PostQuitMessage(0);
        break;

        case WM_CHAR:
        HandleUserInput((TCHAR)wParam);
        break;
        default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow){
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPLICATION));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = "AOS Cmdline";
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_APPLICATION));

    if (!RegisterClassEx(&wcex))
    {
        MessageBox(NULL, _T("Call to RegisterClassEx failed!"), _T("AOS Cmdline"), 0);

        return 1;
    }

    hInst = hInstance;

    HWND hWnd = CreateWindow(
        "AOS Cmdline",
        "AOS Cmdline",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        500, 100,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (!hWnd)
    {
        MessageBox(NULL, _T("Call to CreateWindow failed!"), _T("AOS Cmdline"), 0);

        return 1;
    }

    ShowWindow(hWnd,
        nCmdShow);
    UpdateWindow(hWnd);

    // Main message loop:
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int) msg.wParam;
}