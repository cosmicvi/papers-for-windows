; Enable Modern User Interface (MUI2)
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "InstallOptions.nsh"

!define APP_NAME "Document Viewer"
!define PUBLISHER "GNOME Project"
!define APP_VERSION "51.beta"

Name "${APP_NAME}"
OutFile "..\..\papers-${APP_VERSION}-installer-x64.exe"
; Default to admin install dir, will be changed for user mode.
InstallDir "$PROGRAMFILES64\GNOME\Papers"
InstallDirRegKey HKCU "Software\GNOME\Papers" ""
RequestExecutionLevel user

; UI Configuration
!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP_NOBUILDING

!define MUI_ICON "icons\papers.ico"

; Installer Pages
!insertmacro MUI_PAGE_WELCOME
Page custom WelcomePre
!define MUI_PAGE_CUSTOMFUNCTION_PRE DirectoryPre
!define MUI_PAGE_CUSTOMFUNCTION_SHOW DirectoryShow
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\papers.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Document Viewer"
!insertmacro MUI_PAGE_FINISH

; Uninstaller Pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "English"

Var InstallMode

Function .onInit
  ; Default to current user install unless specified otherwise
  StrCpy $InstallMode "JustMe"

  ; Check for /ALLUSERS or /CURRENTUSER command line switches
  ${GetOptions} $CMDLINE "/ALLUSERS" $R0
  ${If} $R0 != ""
    StrCpy $InstallMode "AllUsers"
  ${EndIf}
  ${GetOptions} $CMDLINE "/CURRENTUSER" $R0
  ${If} $R0 != ""
    StrCpy $InstallMode "JustMe"
  ${EndIf}
FunctionEnd

Function WelcomePre
  ; If running a silent install, skip all UI pages
  IfSilent +3

  ; If already running as admin, skip the choice page
  ClearErrors
  FileOpen $0 "$WINDIR\temp.tmp" w
  IfErrors +2 0
    FileClose $0
    Delete "$WINDIR\temp.tmp"
    ; We are admin. If launched for all users, skip the choice page.
    Abort

  !insertmacro INSTALLOPTIONS_EXTRACT "papers-installmode.ini"
  !insertmacro INSTALLOPTIONS_DISPLAY "papers-installmode.ini"
FunctionEnd

Function DirectoryPre
  ; If InstallMode is not set, read it from the custom page
  IfSilent +5 ; Skip UI interaction if silent
  ${If} $InstallMode == "JustMe" ; Check if user changed the mode on the custom page
    !insertmacro INSTALLOPTIONS_READ $0 "papers-installmode.ini" "Field 3" "State"
    ${If} $0 == 1 ; "Just Me" is checked
        StrCpy $InstallMode "JustMe"
    ${Else} ; "All Users" must be checked
        StrCpy $InstallMode "AllUsers"
    ${EndIf}
  ${EndIf}


  ${If} $InstallMode == "AllUsers"
    ; Check if we are already admin
    ClearErrors
    FileOpen $R0 "$WINDIR\temp.tmp" w
    ${If} ${Errors}
      ; Not admin, so re-launch with elevation request
      ExecShell "runas" "$EXEPATH" '/S /ALLUSERS' ; Preserve silent switch
      Quit
    ${Else}
      ; We are admin, clean up the temp file
      FileClose $R0
      Delete "$WINDIR\temp.tmp"
    ${EndIf}
    ; We are admin, set system-wide paths
    SetShellVarContext all
    StrCpy $InstDir "$PROGRAMFILES64\GNOME\Papers"
  ${Else}
    ; "JustMe" mode, set user-specific paths
    SetShellVarContext current
    StrCpy $InstDir "$LOCALAPPDATA\GNOME\Papers"
  ${EndIf}
FunctionEnd

Function DirectoryShow
  ; Hide the directory selection page if we are in "JustMe" mode
  ; to provide a simpler install experience.
  IfSilent +2
  ${If} $InstallMode == "JustMe"
    Abort
  ${EndIf}
FunctionEnd

Section "Document Viewer Core Application" SEC01
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "..\..\dist\*.*"

  DetailPrint "Generating Fontconfig cache (this may take a few seconds)..."
  nsExec::ExecToLog '"$INSTDIR\bin\fc-cache.exe" -f -v'

  File "icons\papers.ico"

  CreateDirectory "$SMPROGRAMS\Document Viewer"
  SetOutPath "$INSTDIR\bin"
  CreateShortcut "$SMPROGRAMS\Document Viewer\Document Viewer.lnk" "$INSTDIR\bin\papers.exe" "" "$INSTDIR\papers.ico" 0 "" "" "Document Viewer" "$INSTDIR\bin"
  CreateShortcut "$DESKTOP\Document Viewer.lnk" "$INSTDIR\bin\papers.exe" "" "$INSTDIR\papers.ico" 0 "" "" "Document Viewer" "$INSTDIR\bin"
  SetOutPath "$INSTDIR"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  ${If} $InstallMode == "AllUsers"
    WriteRegStr HKLM "Software\GNOME\Papers" "InstallDir" $INSTDIR
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "DisplayName" "Document Viewer"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "DisplayIcon" "$INSTDIR\papers.ico"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "Publisher" "${PUBLISHER}"
  ${Else}
    WriteRegStr HKCU "Software\GNOME\Papers" "InstallDir" $INSTDIR
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "DisplayName" "Document Viewer"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "DisplayIcon" "$INSTDIR\papers.ico"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "Publisher" "${PUBLISHER}"
  ${EndIf}
SectionEnd

Section "Register as System PDF & Document Viewer" SEC02
  ${If} $InstallMode == "AllUsers"
    ; PDF Association
    WriteRegStr HKLM "Software\Classes\.pdf" "" "Papers.Document.PDF"
    WriteRegStr HKLM "Software\Classes\Papers.Document.PDF" "" "PDF Document"
    WriteRegStr HKLM "Software\Classes\Papers.Document.PDF\DefaultIcon" "" "$INSTDIR\papers.ico,0"
    WriteRegStr HKLM "Software\Classes\Papers.Document.PDF\shell\open\command" "" '"$INSTDIR\bin\papers.exe" "%1"'
    ; DjVu Association
    WriteRegStr HKLM "Software\Classes\.djvu" "" "Papers.Document.DjVu"
    WriteRegStr HKLM "Software\Classes\Papers.Document.DjVu" "" "DjVu Document"
    WriteRegStr HKLM "Software\Classes\Papers.Document.DjVu\DefaultIcon" "" "$INSTDIR\papers.ico,0"
    WriteRegStr HKLM "Software\Classes\Papers.Document.DjVu\shell\open\command" "" '"$INSTDIR\bin\papers.exe" "%1"'
    ; Comic Book CBR/CBZ Association
    WriteRegStr HKLM "Software\Classes\.cbr" "" "Papers.Document.CBR"
    WriteRegStr HKLM "Software\Classes\Papers.Document.CBR" "" "Comic Book Archive"
    WriteRegStr HKLM "Software\Classes\Papers.Document.CBR\DefaultIcon" "" "$INSTDIR\papers.ico,0"
    WriteRegStr HKLM "Software\Classes\Papers.Document.CBR\shell\open\command" "" '"$INSTDIR\bin\papers.exe" "%1"'
    ; Registered Application Capabilities
    WriteRegStr HKLM "Software\GNOME\Papers\Capabilities" "ApplicationDescription" "Document Viewer for Windows"
    WriteRegStr HKLM "Software\GNOME\Papers\Capabilities" "ApplicationName" "Document Viewer"
    WriteRegStr HKLM "Software\GNOME\Papers\Capabilities\FileAssociations" ".pdf" "Papers.Document.PDF"
    WriteRegStr HKLM "Software\GNOME\Papers\Capabilities\FileAssociations" ".djvu" "Papers.Document.DjVu"
    WriteRegStr HKLM "Software\GNOME\Papers\Capabilities\FileAssociations" ".cbr" "Papers.Document.CBR"
    WriteRegStr HKLM "Software\RegisteredApplications" "Document Viewer" "Software\GNOME\Papers\Capabilities"
  ${Else}
    ; PDF Association
    WriteRegStr HKCU "Software\Classes\.pdf" "" "Papers.Document.PDF"
    WriteRegStr HKCU "Software\Classes\Papers.Document.PDF" "" "PDF Document"
    WriteRegStr HKCU "Software\Classes\Papers.Document.PDF\DefaultIcon" "" "$INSTDIR\papers.ico,0"
    WriteRegStr HKCU "Software\Classes\Papers.Document.PDF\shell\open\command" "" '"$INSTDIR\bin\papers.exe" "%1"'
    ; DjVu Association
    WriteRegStr HKCU "Software\Classes\.djvu" "" "Papers.Document.DjVu"
    WriteRegStr HKCU "Software\Classes\Papers.Document.DjVu" "" "DjVu Document"
    WriteRegStr HKCU "Software\Classes\Papers.Document.DjVu\DefaultIcon" "" "$INSTDIR\papers.ico,0"
    WriteRegStr HKCU "Software\Classes\Papers.Document.DjVu\shell\open\command" "" '"$INSTDIR\bin\papers.exe" "%1"'
    ; Comic Book CBR/CBZ Association
    WriteRegStr HKCU "Software\Classes\.cbr" "" "Papers.Document.CBR"
    WriteRegStr HKCU "Software\Classes\Papers.Document.CBR" "" "Comic Book Archive"
    WriteRegStr HKCU "Software\Classes\Papers.Document.CBR\DefaultIcon" "" "$INSTDIR\papers.ico,0"
    WriteRegStr HKCU "Software\Classes\Papers.Document.CBR\shell\open\command" "" '"$INSTDIR\bin\papers.exe" "%1"'
    ; Registered Application Capabilities
    WriteRegStr HKCU "Software\GNOME\Papers\Capabilities" "ApplicationDescription" "Document Viewer for Windows"
    WriteRegStr HKCU "Software\GNOME\Papers\Capabilities" "ApplicationName" "Document Viewer"
    WriteRegStr HKCU "Software\GNOME\Papers\Capabilities\FileAssociations" ".pdf" "Papers.Document.PDF"
    WriteRegStr HKCU "Software\GNOME\Papers\Capabilities\FileAssociations" ".djvu" "Papers.Document.DjVu"
    WriteRegStr HKCU "Software\GNOME\Papers\Capabilities\FileAssociations" ".cbr" "Papers.Document.CBR"
    WriteRegStr HKCU "Software\RegisteredApplications" "Document Viewer" "Software\GNOME\Papers\Capabilities"
  ${EndIf}

  ; Notify Windows Shell of File Association Change
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section "Uninstall"
  ; Determine if it was an AllUsers or JustMe install by checking the HKLM registry first.
  ClearErrors
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "UninstallString"
  IfErrors 0 +3
    ; HKLM key not found, must be a user install
    StrCpy $InstallMode "JustMe"
    Goto +2
  StrCpy $InstallMode "AllUsers"

  ${If} $InstallMode == "AllUsers"
    SetShellVarContext all
  ${Else}
    SetShellVarContext current
  ${EndIf}

  RMDir /r "$INSTDIR"
  Delete "$INSTDIR\papers.ico"
  Delete "$SMPROGRAMS\Document Viewer\Document Viewer.lnk"
  RMDir "$SMPROGRAMS\Document Viewer"
  Delete "$DESKTOP\Document Viewer.lnk"

  ${If} $InstallMode == "AllUsers"
    DeleteRegKey HKLM "Software\GNOME\Papers"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers"
    DeleteRegKey HKLM "Software\Classes\Papers.Document.PDF"
    DeleteRegKey HKLM "Software\Classes\Papers.Document.DjVu"
    DeleteRegKey HKLM "Software\Classes\Papers.Document.CBR"
    DeleteRegValue HKLM "Software\RegisteredApplications" "Document Viewer"
  ${Else}
    DeleteRegKey HKCU "Software\GNOME\Papers"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers"
    DeleteRegKey HKCU "Software\Classes\Papers.Document.PDF"
    DeleteRegKey HKCU "Software\Classes\Papers.Document.DjVu"
    DeleteRegKey HKCU "Software\Classes\Papers.Document.CBR"
    DeleteRegValue HKCU "Software\RegisteredApplications" "Document Viewer"
  ${EndIf}

  ; Refresh Shell
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
