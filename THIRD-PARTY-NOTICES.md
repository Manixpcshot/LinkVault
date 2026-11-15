# LinkVault distribution notices

LinkVault uses the Qt 6.8.3 libraries (Qt Core, GUI, Widgets, Network and deployment plugins). Qt is copyright The Qt Company Ltd. and other contributors. Qt is available under LGPL-3.0. This distribution includes the LGPL-3.0 and GPL-3.0 license texts under `licenses/Qt`. The Windows package also includes upstream QtBase and Qt SVG software bills of materials with component license and copyright information.

Corresponding Qt source, including QtBase, Qt SVG, and Qt Wayland, is available from the [Qt 6.8.3 source archive](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/). QtBase and Qt SVG source mirrors are attached to the [v3.0 release](https://github.com/Manixpcshot/LinkVault/releases/tag/v3.0). You can build LinkVault against a compatible modified Qt kit using the steps in the [build instructions](https://github.com/Manixpcshot/LinkVault/tree/v4#build-from-source). On Windows, you can also extract the portable distribution to a writable folder and replace the Qt DLLs and plugins with compatible Windows x64 MinGW builds. Qt may be modified and reverse engineering for debugging such modifications is permitted under the LGPL.

## Linux

Linux Qt packages may include ICU 73.2, copyright Unicode, Inc. and others. The [ICU 73.2 release](https://github.com/unicode-org/icu/releases/tag/release-73-2) provides the source. The package includes the ICU license under `licenses`.

## Windows

GCC 13.1.0 and MinGW-w64 11.0.0 provide the GCC, C++ and winpthreads runtime DLLs. The Windows package includes their license files, including the GCC Runtime Library Exception, under `licenses/`. Sources are available from [GCC 13.1.0](https://ftp.gnu.org/gnu/gcc/gcc-13.1.0/) and [MinGW-w64 11.0.0](https://sourceforge.net/projects/mingw-w64/files/mingw-w64/mingw-w64-release/).

The software OpenGL fallback, `opengl32sw.dll`, is Mesa llvmpipe 11.2.2 from the Qt toolchain. Refer to the included [Mesa and LLVM attributions](https://doc.qt.io/qtcreator/qtcreator-binary-attribution-llvmpipe.html) and the [Qt build instructions and source links](https://wiki.qt.io/MesaLlvmpipe). The deployment also includes Microsoft's Direct3D shader compiler runtime, `D3Dcompiler_47.dll`.
