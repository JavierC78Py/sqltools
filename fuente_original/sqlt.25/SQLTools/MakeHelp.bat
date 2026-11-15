@echo off
REM -- First make map file from Microsoft Visual C++ generated resource.h
echo // MAKEHELP.BAT generated Help Map file.  Used by SQLTools.HPJ. >"Help\SQLTools.hm"
echo. >>"Help\SQLTools.hm"
echo // Commands (ID_* and IDM_*) >>"Help\SQLTools.hm"
makehm ID_,HID_,0x10000 IDM_,HIDM_,0x10000 resource.h >>"Help\SQLTools.hm"

echo. >>"Help\SQLTools.hm"
echo // Prompts (IDP_*) >>"Help\SQLTools.hm"
makehm IDP_,HIDP_,0x30000 resource.h >>"Help\SQLTools.hm"

echo. >>"Help\SQLTools.hm"
echo // Resources (IDR_*) >>"Help\SQLTools.hm"
makehm IDR_,HIDR_,0x20000 resource.h >>"Help\SQLTools.hm"

echo. >>"Help\SQLTools.hm"
echo // Dialogs (IDD_*) >>"Help\SQLTools.hm"
makehm IDD_,HIDD_,0x20000 resource.h >>"Help\SQLTools.hm"
makehm IDD_,HIDD_,0x20000 "\WORK\CPP\COMMON2\INCLUDE\COMMON\AboutDlgRes.h" >>"Help\SQLTools.hm"

echo. >>"Help\SQLTools.hm"
echo // Frame Controls (IDW_*) >>"Help\SQLTools.hm"
makehm IDW_,HIDW_,0x50000 resource.h >>"Help\SQLTools.hm"
REM -- Make help for Project SQLTools


echo Building Win32 Help files
rem start /wait "E:\Program Files\DevStudio\VC\bin\hcw" /C /E /M "Help\SQLTools.hpj"
hcw /C /E /M "Help\SQLTools.hpj"
if errorlevel 1 goto :Error
if not exist "Help\SQLTools.hlp" goto :Error
if not exist "Help\SQLTools.cnt" goto :Error
echo.
if exist Debug\nul copy "Help\SQLTools.hlp" Debug
if exist Debug\nul copy "Help\SQLTools.cnt" Debug
if exist Release\nul copy "Help\SQLTools.hlp" Release
if exist Release\nul copy "Help\SQLTools.cnt" Release
echo.
goto :done

:Error
echo Help\SQLTools.hpj(1) : error: Problem encountered creating help file

:done
echo.
