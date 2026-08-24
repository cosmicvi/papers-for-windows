; Enable Modern User Interface (MUI2)
!include "MUI2.nsh"

!define APP_NAME "Papers"
!define PUBLISHER "GNOME Project"
!ifndef APP_VERSION
  !define APP_VERSION "51.beta.1"
!endif

Name "${APP_NAME}"
OutFile "..\..\papers-${APP_VERSION}-installer-x64.exe"
InstallDir "$PROGRAMFILES64\Papers"
InstallDirRegKey HKCU "Software\Papers" ""
RequestExecutionLevel admin

; UI Configuration
!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP_NOBUILDING

; Installer Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\papers.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Papers"
!insertmacro MUI_PAGE_FINISH

; Uninstaller Pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "English"

Section "Papers Core Application" SEC01
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "..\..\dist\*.*"

  CreateDirectory "$SMPROGRAMS\Papers"
  SetOutPath "$INSTDIR"
  CreateShortcut "$SMPROGRAMS\Papers\Papers.lnk" "$INSTDIR\papers.exe" "" "$INSTDIR\papers.exe" 0 "" "" "GNOME Papers Document Viewer" "$INSTDIR"
  CreateShortcut "$DESKTOP\Papers.lnk" "$INSTDIR\papers.exe" "" "$INSTDIR\papers.exe" 0 "" "" "GNOME Papers Document Viewer" "$INSTDIR"
  SetOutPath "$INSTDIR"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\Papers" "InstallDir" $INSTDIR
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "DisplayName" "Papers Document Viewer"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "DisplayIcon" "$INSTDIR\papers.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers" "Publisher" "${PUBLISHER}"
SectionEnd

Section "Register as System PDF & Document Viewer" SEC02
  ; PDF Association
  WriteRegStr HKCU "Software\Classes\.pdf" "" "Papers.Document.PDF"
  WriteRegStr HKCU "Software\Classes\Papers.Document.PDF" "" "PDF Document (Papers)"
  WriteRegStr HKCU "Software\Classes\Papers.Document.PDF\DefaultIcon" "" "$INSTDIR\papers.exe,0"
  WriteRegStr HKCU "Software\Classes\Papers.Document.PDF\shell\open\command" "" '"$INSTDIR\papers.exe" "%1"'

  ; DjVu Association
  WriteRegStr HKCU "Software\Classes\.djvu" "" "Papers.Document.DjVu"
  WriteRegStr HKCU "Software\Classes\Papers.Document.DjVu" "" "DjVu Document (Papers)"
  WriteRegStr HKCU "Software\Classes\Papers.Document.DjVu\DefaultIcon" "" "$INSTDIR\papers.exe,0"
  WriteRegStr HKCU "Software\Classes\Papers.Document.DjVu\shell\open\command" "" '"$INSTDIR\papers.exe" "%1"'

  ; Comic Book CBR/CBZ Association
  WriteRegStr HKCU "Software\Classes\.cbr" "" "Papers.Document.CBR"
  WriteRegStr HKCU "Software\Classes\Papers.Document.CBR" "" "Comic Book Archive (Papers)"
  WriteRegStr HKCU "Software\Classes\Papers.Document.CBR\DefaultIcon" "" "$INSTDIR\papers.exe,0"
  WriteRegStr HKCU "Software\Classes\Papers.Document.CBR\shell\open\command" "" '"$INSTDIR\papers.exe" "%1"'

  ; Registered Application Capabilities
  WriteRegStr HKCU "Software\Papers\Capabilities" "ApplicationDescription" "GNOME Papers Document Viewer for Windows"
  WriteRegStr HKCU "Software\Papers\Capabilities" "ApplicationName" "Papers"
  WriteRegStr HKCU "Software\Papers\Capabilities\FileAssociations" ".pdf" "Papers.Document.PDF"
  WriteRegStr HKCU "Software\Papers\Capabilities\FileAssociations" ".djvu" "Papers.Document.DjVu"
  WriteRegStr HKCU "Software\Papers\Capabilities\FileAssociations" ".cbr" "Papers.Document.CBR"
  WriteRegStr HKCU "Software\RegisteredApplications" "Papers" "Software\Papers\Capabilities"

  ; Notify Windows Shell of File Association Change
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section "Uninstall"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\Papers\Papers.lnk"
  RMDir "$SMPROGRAMS\Papers"
  Delete "$DESKTOP\Papers.lnk"
  DeleteRegKey HKCU "Software\Papers"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Papers"
  DeleteRegKey HKCU "Software\Classes\Papers.Document.PDF"
  DeleteRegKey HKCU "Software\Classes\Papers.Document.DjVu"
  DeleteRegKey HKCU "Software\Classes\Papers.Document.CBR"
  DeleteRegValue HKCU "Software\RegisteredApplications" "Papers"

  ; Refresh Shell
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
