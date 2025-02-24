@echo off
chcp 65001 > nul
echo ===== Compilation de SLASH =====

set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

call "%VS_PATH%"

echo Compilation du programme...
cl /nologo /EHsc /W4 slash.cpp /Fe:slash.exe ^
    /I "C:\NVAPI_SDK" ^
    user32.lib ^
    kernel32.lib ^
    psapi.lib ^
    advapi32.lib ^
    wbemuuid.lib ^
    iphlpapi.lib ^
    ws2_32.lib ^
    pdh.lib

if %errorlevel% equ 0 (
    echo Compilation réussie !
    echo Le programme a été créé : slash.exe
) else (
    echo Erreur lors de la compilation !
)

pause