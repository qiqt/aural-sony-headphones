Unicode True
RequestExecutionLevel user
ManifestDPIAware true
SetCompressor /SOLID zlib
Name "Aural"
OutFile "${OUTPUT_FILE}"
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "Aural"
VIAddVersionKey /LANG=1033 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright (c) 2026 Aural contributors"

!include "LogicLib.nsh"
!include "x64.nsh"

!ifdef PORTABLE
VIAddVersionKey /LANG=1033 "FileDescription" "Aural portable launcher"
SilentInstall silent
AutoCloseWindow true

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "Aural requires 64-bit Windows."
        Abort
    ${EndIf}
FunctionEnd

Section
    InitPluginsDir
    SetOutPath "$PLUGINSDIR\Aural"
    File /r "${PACKAGE_DIR}\*"
    ClearErrors
    ExecWait '"$PLUGINSDIR\Aural\appAural.exe"' $0
    ${If} ${Errors}
        MessageBox MB_ICONSTOP "Aural could not start."
        SetErrorLevel 1
    ${Else}
        SetErrorLevel $0
    ${EndIf}
    # Release the current-directory handle before NSIS removes its temporary folder.
    SetOutPath "$TEMP"
    RMDir /r "$PLUGINSDIR\Aural"
SectionEnd

!else
VIAddVersionKey /LANG=1033 "FileDescription" "Aural setup"
!include "MUI2.nsh"
InstallDir "$LOCALAPPDATA\Programs\Aural"
InstallDirRegKey HKCU "Software\Aural\Installer" "InstallDir"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "Install Aural ${APP_VERSION} for your Windows account.$\r$\n$\r$\nIf Aural is running, choose Quit Aural from its tray menu before continuing."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PACKAGE_DIR}\LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\appAural.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open Aural"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "Aural requires 64-bit Windows."
        Abort
    ${EndIf}
    SetRegView 64
    SetShellVarContext current
FunctionEnd

Section "Aural and Start menu shortcut" AppSection
    SectionIn RO
    SetOutPath "$INSTDIR"
    File /r "${PACKAGE_DIR}\*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateDirectory "$SMPROGRAMS\Aural"
    CreateShortcut "$SMPROGRAMS\Aural\Aural.lnk" "$INSTDIR\appAural.exe"
    WriteRegStr HKCU "Software\Aural\Installer" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Aural" "DisplayName" "Aural"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Aural" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Aural" "DisplayIcon" "$INSTDIR\appAural.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Aural" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Aural" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Aural" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Aural" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Aural" "NoRepair" 1
SectionEnd

Section /o "Desktop shortcut" DesktopSection
    CreateShortcut "$DESKTOP\Aural.lnk" "$INSTDIR\appAural.exe"
SectionEnd

Function un.onInit
    SetRegView 64
    SetShellVarContext current
FunctionEnd

Section "Uninstall"
    !include "${UNINSTALL_MANIFEST}"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    Delete "$SMPROGRAMS\Aural\Aural.lnk"
    RMDir "$SMPROGRAMS\Aural"
    Delete "$DESKTOP\Aural.lnk"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Aural"
    DeleteRegKey HKCU "Software\Aural\Installer"
SectionEnd
!endif
