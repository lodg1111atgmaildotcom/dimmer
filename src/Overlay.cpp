//////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2007-2017 Casey Langen
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the author nor the names of other contributors may
//      be used to endorse or promote products derived from this software
//      without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////////

#include "Overlay.h"
#include "Monitor.h"
#include <algorithm>
#include <map>
#include <vector>
#include <magnification.h>
#include <CommCtrl.h>
#include <dwmapi.h>

#pragma comment(lib, "Magnification.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace dimmer;

#define TIMER_ID 0xdeadbeef
#define AGGRESSIVE_TIMER_ID 0xdeadc0de

constexpr int timerTickMs = 10;
constexpr int aggressiveTimerMs = 5; // More frequent updates for aggressive mode
constexpr wchar_t className[] = L"DimmerOverlayClass";
constexpr wchar_t windowTitle[] = L"DimmerOverlayWindow";
constexpr wchar_t magnificationHostClass[] = L"DimmerMagnificationHost";
constexpr wchar_t magnificationHostTitle[] = L"DimmerMagnificationHost";

static ATOM overlayClass = 0;
static std::map<HWND, Overlay*> hwndToOverlay;
static WORD gammaRamp[3][256];

// Static members for aggressive mode
HHOOK Overlay::shellHook = nullptr;
std::vector<HWND> Overlay::overlayWindows;
HHOOK Overlay::mouseHook = nullptr;
bool Overlay::magnificationInitialized = false;

static void registerClass(HINSTANCE instance, WNDPROC wndProc) {
    if (!overlayClass) {
        WNDCLASS wc = {};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = instance;
        wc.lpszClassName = className;
        overlayClass = RegisterClass(&wc);
    }
}

static bool enabled(Monitor& monitor) {
    return isDimmerEnabled() && isMonitorEnabled(monitor);
}

Overlay::Overlay(HINSTANCE instance, Monitor monitor)
: instance(instance)
, monitor(monitor)
, timerId(0)
, aggressiveTimerId(0)
, bgBrush(CreateSolidBrush(RGB(0, 0, 0)))
, hwnd(nullptr)
, magnificationHost(nullptr)
, magnificationControl(nullptr)
, useMagnification(false) {
    
    // Initialize magnification API if not already done
    if (!magnificationInitialized) {
        if (MagInitialize()) {
            magnificationInitialized = true;
        }
    }
    
    registerClass(instance, &Overlay::windowProc);
    this->update(monitor);
    installShellHook();
    installMouseHook();
}

Overlay::~Overlay() {
    this->disableColorTemperature();
    this->disableBrigthnessOverlay();
    this->destroyMagnificationOverlay();
    DeleteObject(this->bgBrush);
    
    // Remove from overlay windows list
    if (this->hwnd) {
        auto it = std::find(overlayWindows.begin(), overlayWindows.end(), this->hwnd);
        if (it != overlayWindows.end()) {
            overlayWindows.erase(it);
        }
    }
    
    // Uninstall hook if no more overlays
    if (overlayWindows.empty()) {
        uninstallShellHook();
        uninstallMouseHook();
        
        // Uninitialize magnification if all overlays are gone
        if (magnificationInitialized) {
            MagUninitialize();
            magnificationInitialized = false;
        }
    }
}

static void colorTemperatureToRgb(int kelvin, float& red, float& green, float& blue) {
    kelvin /= 100;

    if (kelvin <= 66) {
        red = 255;
    }
    else {
        red = kelvin - 60.0f;
        red = (float)(329.698727446 * (pow(red, -0.1332047592)));
        red = std::max(0.0f, std::min(255.0f, red));
    }

    if (kelvin <= 66) {
        green = (float) kelvin;
        green = (float)(99.4708025861 * log(green) - 161.1195681661);
        green = std::max(0.0f, std::min(255.0f, green));
    }
    else {
        green = kelvin - 60.0f;
        green = (float)(288.1221695283 * (pow(green, -0.0755148492)));
        green = std::max(0.0f, std::min(255.0f, green));
    }

    if (kelvin >= 66) {
        blue = 255.0f;
    }
    else {
        blue = kelvin - 10.0f;
        blue = (float)(138.5177312231 * log(blue) - 305.0447927307);
        blue = std::max(0.0f, std::min(255.0f, blue));
    }

    red /= 255.0f;
    green /= 255.0f;
    blue /= 255.0f;
}

void Overlay::disableColorTemperature() {
    HDC dc = CreateDC(nullptr, monitor.info.szDevice, nullptr, nullptr);
    if (dc) {
        for (int i = 0; i < 256; i++) {
            gammaRamp[0][i] = gammaRamp[1][i] = gammaRamp[2][i] = i * 256;
        }
        SetDeviceGammaRamp(dc, gammaRamp);
        DeleteDC(dc);
    }
}

void Overlay::updateColorTemperature() {
    int temperature = getMonitorTemperature(monitor);

    if (!enabled(monitor) || temperature == -1) {
        disableColorTemperature();
    }
    else {
        HDC dc = CreateDC(nullptr, monitor.info.szDevice, nullptr, nullptr);
        if (dc) {
            float red = 1.0f;
            float green = 1.0f;
            float blue = 1.0f;

            temperature = std::min(6000, std::max(4500, temperature));
            colorTemperatureToRgb(temperature, red, green, blue);

            for (int i = 0; i < 256; i++) {
                float brightness = i * (256.0f);
                gammaRamp[0][i] = (short)std::max(0.0f, std::min(65535.0f, brightness * red));
                gammaRamp[1][i] = (short)std::max(0.0f, std::min(65535.0f, brightness * green));
                gammaRamp[2][i] = (short)std::max(0.0f, std::min(65535.0f, brightness * blue));
            }

            SetDeviceGammaRamp(dc, gammaRamp);

            DeleteDC(dc);
        }
    }
}

void Overlay::disableBrigthnessOverlay() {
    this->killTimer();
    this->destroyMagnificationOverlay();
    
    if (this->hwnd) {
        // Remove from overlay windows list
        auto it = std::find(overlayWindows.begin(), overlayWindows.end(), this->hwnd);
        if (it != overlayWindows.end()) {
            overlayWindows.erase(it);
        }
        
        DestroyWindow(this->hwnd);
        hwndToOverlay.erase(hwndToOverlay.find(this->hwnd));
        this->hwnd = nullptr;
    }
}

void Overlay::updateBrightnessOverlay() {
    if (!enabled(monitor) || getMonitorOpacity(monitor) == 0.0f) {
        disableBrigthnessOverlay();
    }
    else {
        if (!this->hwnd) {
            this->hwnd =
                CreateWindowEx(
                    WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                    className,
                    windowTitle,
                    WS_POPUP,
                    0, 0, 0, 0, /* dimens */
                    nullptr,
                    nullptr,
                    instance,
                    this);

            hwndToOverlay[this->hwnd] = this;
            overlayWindows.push_back(this->hwnd);

            SetWindowLong(this->hwnd, GWL_STYLE, 0); /* removes title, borders. */
        }

        float red, green, blue;
        colorTemperatureToRgb(3000, red, green, blue);
        red = (1.0f - red) * 256.0f;
        green = (1.0f - green) * 256.0f;
        blue = (1.0f - blue) * 256.0f;

        int x = monitor.info.rcMonitor.left;
        int y = monitor.info.rcMonitor.top;
        int width = monitor.info.rcMonitor.right - x;
        int height = monitor.info.rcMonitor.bottom - y;

        float value = getMonitorOpacity(this->monitor);
        value = std::min(1.0f, std::max(0.0f, value));
        BYTE opacity = std::min((BYTE)240, (BYTE)(value * 255.0f));

        SetLayeredWindowAttributes(this->hwnd, 0, opacity, LWA_ALPHA);
        
        // More aggressive positioning
        SetWindowPos(this->hwnd, HWND_TOPMOST, x, y, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
        
        // Force to front again after a brief moment
        SetWindowPos(this->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER);

        UpdateWindow(this->hwnd);
        this->aggressiveTopMost();
        this->startTimer();
    }
}

void Overlay::update(Monitor& monitor) {
    this->monitor = monitor;
    this->updateColorTemperature();
    this->updateBrightnessOverlay();
    
    if (useMagnification) {
        this->updateMagnificationOverlay();
    }
}

void Overlay::startTimer() {
    this->killTimer();

    if (isPollingEnabled()) {
        this->timerId = SetTimer(this->hwnd, TIMER_ID, timerTickMs, nullptr);
        // Also start aggressive timer for more frequent z-order enforcement
        this->aggressiveTimerId = SetTimer(this->hwnd, AGGRESSIVE_TIMER_ID, aggressiveTimerMs, nullptr);
    }
}

void Overlay::killTimer() {
    if (this->timerId) {
        KillTimer(this->hwnd, this->timerId);
        this->timerId = 0;
    }
    if (this->aggressiveTimerId) {
        KillTimer(this->hwnd, this->aggressiveTimerId);
        this->aggressiveTimerId = 0;
    }
}

LRESULT CALLBACK Overlay::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto overlay = hwndToOverlay.find(hwnd);

    if (overlay != hwndToOverlay.end()) {
        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                FillRect(hdc, &ps.rcPaint, overlay->second->bgBrush);
                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_TIMER: {
                if (wParam == overlay->second->timerId) {
                    BringWindowToTop(hwnd);
                    return 0;
                }
                else if (wParam == overlay->second->aggressiveTimerId) {
                    // More aggressive z-order enforcement
                    overlay->second->aggressiveTopMost();
                    return 0;
                }
            }
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void Overlay::forceToTop() {
    if (this->hwnd) {
        aggressiveTopMost();
    }
}

void Overlay::aggressiveTopMost() {
    if (!this->hwnd) return;
    
    // Multiple attempts with different approaches
    SetWindowPos(this->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER);
    BringWindowToTop(this->hwnd);
    SetWindowPos(this->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    
    // Use DWM to ensure window is above composition
    BOOL compositionEnabled = FALSE;
    if (SUCCEEDED(DwmIsCompositionEnabled(&compositionEnabled)) && compositionEnabled) {
        // Force window to be rendered above DWM composition
        DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
        DwmSetWindowAttribute(this->hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
        
        // Exclude from DWM peek
        BOOL exclude = TRUE;
        DwmSetWindowAttribute(this->hwnd, DWMWA_EXCLUDED_FROM_PEEK, &exclude, sizeof(exclude));
        
        // Disable DWM transitions
        BOOL disallow = TRUE;
        DwmSetWindowAttribute(this->hwnd, DWMWA_DISALLOW_PEEK, &disallow, sizeof(disallow));
    }
    
    // Force redraw to ensure visibility
    InvalidateRect(this->hwnd, nullptr, FALSE);
    UpdateWindow(this->hwnd);
    
    // Try magnification overlay if traditional overlay fails
    if (magnificationInitialized) {
        this->createMagnificationOverlay();
    }
}

void Overlay::installShellHook() {
    if (!shellHook) {
        shellHook = SetWindowsHookEx(WH_SHELL, shellHookProc, GetModuleHandle(nullptr), 0);
    }
}

void Overlay::uninstallShellHook() {
    if (shellHook) {
        UnhookWindowsHookEx(shellHook);
        shellHook = nullptr;
    }
}

void Overlay::installMouseHook() {
    if (!mouseHook) {
        mouseHook = SetWindowsHookEx(WH_MOUSE_LL, mouseHookProc, GetModuleHandle(nullptr), 0);
    }
}

void Overlay::uninstallMouseHook() {
    if (mouseHook) {
        UnhookWindowsHookEx(mouseHook);
        mouseHook = nullptr;
    }
}

LRESULT CALLBACK Overlay::shellHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        switch (wParam) {
            case HSHELL_WINDOWCREATED:
            case HSHELL_WINDOWACTIVATED:
            case HSHELL_REDRAW: {
                // Force all overlay windows to top when system creates/activates windows
                for (HWND overlayHwnd : overlayWindows) {
                    if (IsWindow(overlayHwnd)) {
                        SetWindowPos(overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0, 
                                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
                        SetWindowPos(overlayHwnd, HWND_TOP, 0, 0, 0, 0, 
                                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
                    }
                }
                
                // Additional aggressive approach: enumerate all windows and force overlays above them
                EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                    wchar_t className[256];
                    GetClassName(hwnd, className, sizeof(className) / sizeof(wchar_t));
                    
                    // Check for special window classes that might be system UI
                    std::wstring classStr(className);
                    if (classStr.find(L"TaskListThumbnailWnd") != std::wstring::npos ||
                        classStr.find(L"Chrome") != std::wstring::npos ||
                        classStr.find(L"Thumbnail") != std::wstring::npos ||
                        classStr.find(L"Preview") != std::wstring::npos ||
                        classStr.find(L"Popup") != std::wstring::npos) {
                        
                        // Force overlays above these special windows
                        for (HWND overlayHwnd : overlayWindows) {
                            if (IsWindow(overlayHwnd)) {
                                SetWindowPos(overlayHwnd, hwnd, 0, 0, 0, 0, 
                                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                                SetWindowPos(overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0, 
                                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                            }
                        }
                    }
                    return TRUE;
                }, 0);
                
                break;
            }
        }
    }
    
    return CallNextHookEx(shellHook, nCode, wParam, lParam);
}

LRESULT CALLBACK Overlay::mouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* mouseData = (MSLLHOOKSTRUCT*)lParam;
        
        // Get taskbar position
        HWND taskbar = FindWindow(L"Shell_TrayWnd", nullptr);
        if (taskbar) {
            RECT taskbarRect;
            GetWindowRect(taskbar, &taskbarRect);
            
            // Check if mouse is over taskbar area
            POINT mousePos = mouseData->pt;
            if (PtInRect(&taskbarRect, mousePos)) {
                // Mouse is over taskbar, aggressively bring overlays to front
                for (HWND overlayHwnd : overlayWindows) {
                    if (IsWindow(overlayHwnd)) {
                        SetWindowPos(overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0, 
                                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
                        SetWindowPos(overlayHwnd, HWND_TOP, 0, 0, 0, 0, 
                                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
                        
                        // Additional forced redraw
                        InvalidateRect(overlayHwnd, nullptr, FALSE);
                        UpdateWindow(overlayHwnd);
                    }
                }
            }
        }
    }
    
    return CallNextHookEx(mouseHook, nCode, wParam, lParam);
}

void Overlay::createMagnificationOverlay() {
    if (!magnificationInitialized || magnificationHost) {
        return;
    }
    
    // Register magnification host window class
    static bool hostClassRegistered = false;
    if (!hostClassRegistered) {
        WNDCLASS wc = {};
        wc.lpfnWndProc = DefWindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = magnificationHostClass;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClass(&wc);
        hostClassRegistered = true;
    }
    
    int x = monitor.info.rcMonitor.left;
    int y = monitor.info.rcMonitor.top;
    int width = monitor.info.rcMonitor.right - x;
    int height = monitor.info.rcMonitor.bottom - y;
    
    // Create magnification host window
    magnificationHost = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        magnificationHostClass,
        magnificationHostTitle,
        WS_POPUP | WS_VISIBLE,
        x, y, width, height,
        nullptr, nullptr, instance, nullptr
    );
    
    if (magnificationHost) {
        // Set transparency for dimming effect
        float opacity = getMonitorOpacity(this->monitor);
        BYTE alpha = std::min((BYTE)240, (BYTE)(opacity * 255.0f));
        SetLayeredWindowAttributes(magnificationHost, RGB(0, 0, 0), alpha, LWA_COLORKEY | LWA_ALPHA);
        
        // Create magnification control
        magnificationControl = CreateWindow(
            WC_MAGNIFIER,
            L"MagnifierControl",
            WS_CHILD | WS_VISIBLE,
            0, 0, width, height,
            magnificationHost, nullptr, instance, nullptr
        );
        
        if (magnificationControl) {
            // Set magnification properties for dimming
            MAGTRANSFORM matrix;
            memset(&matrix, 0, sizeof(matrix));
            matrix.v[0][0] = 1.0f;  // No horizontal scaling
            matrix.v[1][1] = 1.0f;  // No vertical scaling
            matrix.v[2][2] = 1.0f;  // No z scaling
            
            MagSetWindowTransform(magnificationControl, &matrix);
            
            // Set source rectangle to cover the entire monitor
            RECT sourceRect = { x, y, x + width, y + height };
            MagSetWindowSource(magnificationControl, sourceRect);
            
            // Apply color transformation for dimming
            MAGCOLOREFFECT colorEffect;
            memset(&colorEffect, 0, sizeof(colorEffect));
            
            float dimFactor = 1.0f - opacity;
            colorEffect.transform[0][0] = dimFactor;  // Red
            colorEffect.transform[1][1] = dimFactor;  // Green  
            colorEffect.transform[2][2] = dimFactor;  // Blue
            colorEffect.transform[3][3] = 1.0f;       // Alpha
            colorEffect.transform[4][4] = 1.0f;       // Reserved
            
            MagSetColorEffect(magnificationControl, &colorEffect);
            
            // Position above everything else
            SetWindowPos(magnificationHost, HWND_TOPMOST, 0, 0, 0, 0, 
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                        
            useMagnification = true;
            overlayWindows.push_back(magnificationHost);
        }
    }
}

void Overlay::destroyMagnificationOverlay() {
    if (magnificationControl) {
        DestroyWindow(magnificationControl);
        magnificationControl = nullptr;
    }
    
    if (magnificationHost) {
        auto it = std::find(overlayWindows.begin(), overlayWindows.end(), magnificationHost);
        if (it != overlayWindows.end()) {
            overlayWindows.erase(it);
        }
        
        DestroyWindow(magnificationHost);
        magnificationHost = nullptr;
    }
    
    useMagnification = false;
}

void Overlay::updateMagnificationOverlay() {
    if (!useMagnification || !magnificationControl) {
        return;
    }
    
    // Update magnification overlay properties
    int x = monitor.info.rcMonitor.left;
    int y = monitor.info.rcMonitor.top;
    int width = monitor.info.rcMonitor.right - x;
    int height = monitor.info.rcMonitor.bottom - y;
    
    // Update position and size
    SetWindowPos(magnificationHost, HWND_TOPMOST, x, y, width, height, 
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowPos(magnificationControl, nullptr, 0, 0, width, height, 
                SWP_NOZORDER | SWP_NOACTIVATE);
    
    // Update source rectangle
    RECT sourceRect = { x, y, x + width, y + height };
    MagSetWindowSource(magnificationControl, sourceRect);
    
    // Update color effect for current opacity
    MAGCOLOREFFECT colorEffect;
    memset(&colorEffect, 0, sizeof(colorEffect));
    
    float opacity = getMonitorOpacity(this->monitor);
    float dimFactor = 1.0f - opacity;
    colorEffect.transform[0][0] = dimFactor;  // Red
    colorEffect.transform[1][1] = dimFactor;  // Green  
    colorEffect.transform[2][2] = dimFactor;  // Blue
    colorEffect.transform[3][3] = 1.0f;       // Alpha
    colorEffect.transform[4][4] = 1.0f;       // Reserved
    
    MagSetColorEffect(magnificationControl, &colorEffect);
    
    // Force to top again
    SetWindowPos(magnificationHost, HWND_TOP, 0, 0, 0, 0, 
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}