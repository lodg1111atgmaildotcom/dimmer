# Dimmer Build Instructions

## Prerequisites
- Visual Studio 2022 Build Tools
- Windows SDK 10.0.26100.0 (or compatible version)

## Build Command

To build the project in Release configuration, run the following command from the project root directory:

```cmd
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x86 && cd "C:\Users\lodg\Desktop\github\dimmer\src" && msbuild dimmer.vcxproj /p:Configuration=Release'
```

### Alternative Build Commands

For Debug configuration:
```cmd
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x86 && cd "C:\Users\lodg\Desktop\github\dimmer\src" && msbuild dimmer.vcxproj /p:Configuration=Debug'
```

### Build Output
The compiled executable will be located in:
- Release: `src\Release\dimmer.exe`
- Debug: `src\Debug\dimmer.exe`

### Notes
- The project uses Visual Studio 2022 toolset (v143)
- Target platform: Win32 (x86)
- Windows SDK version: 10.0.26100.0
- The release build includes UPX compression if `z:\upx.exe` exists
