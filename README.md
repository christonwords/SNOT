# SNOT — Interdimensional Multi-FX

> VST3 / AU plugin for glo trap production. Portal reverb, spectral warp chorus, plasma distortion, 808 inflator, and 7 more alien DSP modules. HTML portal UI runs live inside your DAW via WebView2.

## Download

1. Go to the **Actions** tab above
2. Click the latest green ✅ build  
3. Scroll to **Artifacts** at the bottom
4. Download `SNOT-Windows-VST3` or `SNOT-macOS-VST3`

## Install

**Windows:** Extract → copy `SNOT.vst3` to `C:\Program Files\Common Files\VST3\`  
**macOS:** Extract → copy to `/Library/Audio/Plug-Ins/VST3/`

Scan for new plugins in your DAW.

## Build Locally

```bash
git clone --recursive https://github.com/YOUR_USERNAME/SNOT.git
cd SNOT
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
