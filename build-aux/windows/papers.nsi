; Enable Modern User Interface (MUI2)
!include "MUI2.nsh"
!include "UAC.nsh"

!define APP_NAME "Papers"
!define PUBLISHER "GNOME Project"
!ifndef APP_VERSION
  !define APP_VERSION "51.alpha"
!endif

Name "${APP_NAME}"
OutFile "..\..\papers-${APP_VERSION}-installer-x64.exe"
; Default to admin install dir, will be changed for user mode.
InstallDir "$PROGRAMFILES64\GNOME\Papers"
InstallDirRegKey HKLM "Software\GNOME\Papers" ""
RequestExecutionLevel admin

; UI Configuration
!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP_NOBUILDING

; Installer Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\papers.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Papers"
!insertmacro MUI_PAGE_FINISH

; Uninstaller Pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "English"

Var IsAdmin

Function .onInit
  # If the /U flag is passed, we are in user mode.
  ${GetParameters} $R0
  ${If} $R0 == "/U"
    StrCpy $IsAdmin 0
  ${Else}
    # Otherwise, check for admin privileges.
    ${UAC.IsAdmin} $IsAdmin
  ${EndIf}

  ${If} $IsAdmin == 0
    # Not running as admin. This happens if UAC was cancelled or if we re-launched.
    # If /U is not set, it means UAC was cancelled, so we ask the user.
    ${If} $R0 != "/U"
      MessageBox MB_YESNO|MB_ICONQUESTION \
        "Administrator privileges are required for system-wide installation.$\n$\nWould you like to install for the current user only?" \
        /SD=IDYES IDYES relaunch_user IDNO abort_install

      abort_install:
        Quit

      relaunch_user:
        # Re-launch ourselves with the /U flag to force user mode.
        ${UAC.ExecUser} "$EXEPATH" "/U"
        Quit ; Quit the current (non-elevated) admin-mode installer.
    ${EndIf}

    # We are in user mode. Set user-specific paths.
    SetShellVarContext current
    StrCpy $InstDir "$LOCALAPPDATA\GNOME\Papers"
    InstallDirRegKey HKCU "Software\GNOME\Papers" ""
  ${Else}
    # We are in admin mode. Set system-wide paths.
    SetShellVarContext all
    StrCpy $InstDir "$PROGRAMFILES64\GNOME\Papers"
    InstallDirRegKey HKLM "Software\GNOME\Papers" ""
  ${EndIf}
FunctionEnd

Section "Papers Core Application" SEC01
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "..\..\dist\*.*"

  DetailPrint "Generating Fontconfig cache (this may take a few seconds)..."
  nsExec::ExecToLog '"$INSTDIR\bin\fc-cache.exe" -f -v'

  CreateDirectory "$SMPROGRAMS\Papers"
  SetOutPath "$INSTDIR\bin"
  CreateShortcut "$SMPROGRAMS\Papers\Papers.lnk" "$INSTDIR\bin\papers.exe" "" "$INSTDIR\bin\papers.exe" 0 "" "" "GNOME Papers Document Viewer" "$INSTDIR\bin"
  CreateShortcut "$DESKTOP\Papers.lnk" "$INSTDIR\bin\papers.exe" "" "$INSTDIR\bin\papers.exe" 0 "" "" "GNOME Papers Document Viewer" "$INSTDIR\bin"
  SetOutPath "$INSTDIR"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  ${If} $IsAdmin == 1
    WriteRegStr HKLM "Software\Papers" "InstallDir" $INSTDIR
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "DisplayName" "Papers Document Viewer"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "DisplayIcon" "$INSTDIR\bin\papers.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "Publisher" "${PUBLISHER}"
  ${Else}
    WriteRegStr HKCU "Software\Papers" "InstallDir" $INSTDIR
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "DisplayName" "Papers Document Viewer"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "DisplayIcon" "$INSTDIR\bin\papers.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "Publisher" "${PUBLISHER}"
  ${EndIf}
SectionEnd

Section "Register as System PDF & Document Viewer" SEC02
  ${If} $IsAdmin == 1
    !define REG_ROOT HKLM
  ${Else}
    !define REG_ROOT HKCU
  ${EndIf}

  ; PDF Association
  WriteRegStr ${REG_ROOT} "Software\Classes\.pdf" "" "Papers.Document.PDF"
  WriteRegStr ${REG_ROOT} "Software\Classes\Papers.Document.PDF" "" "PDF Document (Papers)"
  WriteRegStr ${REG_ROOT} "Software\Classes\Papers.Document.PDF\DefaultIcon" "" "$INSTDIR\bin\papers.exe,0"
  WriteRegStr ${REG_ROOT} "Software\Classes\Papers.Document.PDF\shell\open\command" "" '"$INSTDIR\bin\papers.exe" "%1"'

  ; DjVu Association
  WriteRegStr ${REG_ROOT} "Software\Classes\.djvu" "" "Papers.Document.DjVu"
  WriteRegStr ${REG_ROOT} "Software\Classes\Papers.Document.DjVu" "" "DjVu Document (Papers)"
  WriteRegStr ${REG_ROOT} "Software\Classes\Papers.Document.DjVu\DefaultIcon" "" "$INSTDIR\bin\papers.exe,0"
  WriteRegStr ${REG_ROOT} "Software\Classes\Papers.Document.DjVu\shell\open\command" "" '"$INSTDIR\bin\papers.exe" "%1"'

  ; Comic Book CBR/CBZ Association
  WriteRegStr ${REG_ROOT} "Software\Classes\.cbr" "" "Papers.Document.CBR"
  WriteRegStr ${REG_ROOT} "Software\Classes\Papers.Document.CBR" "" "Comic Book Archive (Papers)"
  WriteRegStr ${REG_ROOT} "Software\Classes\Papers.Document.CBR\DefaultIcon" "" "$INSTDIR\bin\papers.exe,0"
  WriteRegStr ${REG_ROOT} "Software\Classes\Papers.Document.CBR\shell\open\command" "" '"$INSTDIR\bin\papers.exe" "%1"'

  ; Registered Application Capabilities
  WriteRegStr ${REG_ROOT} "Software\Papers\Capabilities" "ApplicationDescription" "GNOME Papers Document Viewer for Windows"
  WriteRegStr ${REG_ROOT} "Software\Papers\Capabilities" "ApplicationName" "Papers"
  WriteRegStr ${REG_ROOT} "Software\Papers\Capabilities\FileAssociations" ".pdf" "Papers.Document.PDF"
  WriteRegStr ${REG_ROOT} "Software\Papers\Capabilities\FileAssociations" ".djvu" "Papers.Document.DjVu"
  WriteRegStr ${REG_ROOT} "Software\Papers\Capabilities\FileAssociations" ".cbr" "Papers.Document.CBR"
  WriteRegStr ${REG_ROOT} "Software\RegisteredApplications" "Papers" "Software\Papers\Capabilities"

  ; Notify Windows Shell of File Association Change
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section "Uninstall"
  ; Read install path from registry to determine if it was admin or user install
  ReadRegStr $0 HKLM "Software\Papers" "InstallDir"
  IfErrors 0 +2
  ReadRegStr $0 HKCU "Software\Papers" "InstallDir"

  ; If the path contains Program Files, it was an admin install
  StrCpy $IsAdmin 0
  IfFileExists "$PROGRAMFILES64\*.*" 0 +2
    StrCmp $0 "$PROGRAMFILES64\Papers" 0 +2
    StrCpy $IsAdmin 1

  ${If} $IsAdmin == 1
    SetShellVarContext all
  ${Else}
    SetShellVarContext current
  ${EndIf}

  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\Papers\Papers.lnk"
  RMDir "$SMPROGRAMS\Papers"
  Delete "$DESKTOP\Papers.lnk"

  ${If} $IsAdmin == 1
    DeleteRegKey HKLM "Software\Papers"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers"
    DeleteRegKey HKLM "Software\Classes\Papers.Document.PDF"
    DeleteRegKey HKLM "Software\Classes\Papers.Document.DjVu"
    DeleteRegKey HKLM "Software\Classes\Papers.Document.CBR"
    DeleteRegValue HKLM "Software\RegisteredApplications" "Papers"
  ${Else}
    DeleteRegKey HKCU "Software\Papers"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers"
    DeleteRegKey HKCU "Software\Classes\Papers.Document.PDF"
    DeleteRegKey HKCU "Software\Classes\Papers.Document.DjVu"
    DeleteRegKey HKCU "Software\Classes\Papers.Document.CBR"
    DeleteRegValue HKCU "Software\RegisteredApplications" "Papers"
  ${EndIf}

  ; Refresh Shell
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
