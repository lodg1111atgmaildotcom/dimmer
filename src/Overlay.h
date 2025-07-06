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

#pragma once

#include <Windows.h>
#include <magnification.h>
#include "Monitor.h"

namespace dimmer {
    class Overlay {
        public:
            Overlay(HINSTANCE instance, Monitor monitor);
            ~Overlay();

            void update(Monitor& monitor);
            void startTimer();
            void killTimer();
            void forceToTop();

        private:
            static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
            static LRESULT CALLBACK shellHookProc(int nCode, WPARAM wParam, LPARAM lParam);
            static void installShellHook();
            static void uninstallShellHook();

            void disableColorTemperature();
            void updateColorTemperature();
            void disableBrigthnessOverlay();
            void updateBrightnessOverlay();
            void aggressiveTopMost();
            
            // Magnification API methods
            void createMagnificationOverlay();
            void destroyMagnificationOverlay();
            void updateMagnificationOverlay();
            static BOOL CALLBACK magnificationHostWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

            Monitor monitor;
            HINSTANCE instance;
            HBRUSH bgBrush;
            UINT_PTR timerId;
            UINT_PTR aggressiveTimerId;
            HWND hwnd;
            
            // Magnification overlay
            HWND magnificationHost;
            HWND magnificationControl;
            bool useMagnification;
            
            static HHOOK shellHook;
            static HHOOK keyboardHook;
            static std::vector<HWND> overlayWindows;
            static HHOOK mouseHook;
            static void installMouseHook();
            static void uninstallMouseHook();
            static void installKeyboardHook();
            static void uninstallKeyboardHook();
            static LRESULT CALLBACK mouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
            static LRESULT CALLBACK keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
            static bool magnificationInitialized;
    };
}