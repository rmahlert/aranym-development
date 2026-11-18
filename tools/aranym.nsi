; ARAnyM Setup Script for NSIS
Unicode true
;
!searchreplace SCRIPTDIR "${__FILE__}" "aranym.nsi" ""
!addincludedir "${SCRIPTDIR}"
!addincludedir ".."

; !define VER_MAJOR 1
; !define VER_MINOR 0
; !define VER_MICRO 2
!ifndef VER_MAJOR
  !error "VER_MAJOR is not defined"
!endif
!define VER_BUILD 0
!define VERSION ${VER_MAJOR}.${VER_MINOR}.${VER_MICRO}

!define NAME "ARAnyM"
!define NAMESUFFIX " (Unofficial)"
!ifndef RELEASE_LABEL
  !define RELEASE_LABEL " (Unofficial)"
!endif
!ifndef EASY_DATA_VERSION
  !define EASY_DATA_VERSION "EasyMiNT"
!endif
Var LastConfigBackup
Var BeforeUninstallProfile
Var BeforeUninstallApplication
Var ActiveConfig
Var HadUserConfig
Var ActiveShortcut
Var KeepDesktopShortcut
Var KeepMenuShortcut
!ifdef EASY_DRIVES
Var EasyDataDir
Var EasyConfigExisted
!endif

;--------------------------------
;Configuration

; The name of the installer
Name "${NAME}"
Caption "${NAME} ${VERSION}${RELEASE_LABEL} Setup"

!ifndef OUTFILE
  !define OUTFILE "..\aranym-${VERSION}${SDL}-setup.exe"
!endif
OutFile "${OUTFILE}"
SetCompressor /SOLID lzma

InstType "Full"
InstType "Lite"
InstType "Minimal"

;The root registry to write to
!define REG_ROOT "HKLM"
;The registry path to write to
!define REG_APP_PATH "SOFTWARE\${NAME}"
!define REG_UNINST_KEY "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\${NAME}"

; The default installation directory
InstallDir $PROGRAMFILES\${NAME}
InstallDirRegKey "${REG_ROOT}" "${REG_APP_PATH}" "InstallLocation"

; Request application privileges for Windows Vista
RequestExecutionLevel admin

;--------------------------------
;Header Files

!include "MUI2.nsh"
!include "Sections.nsh"
!include "UninstallLog.nsh"
!include "TextFunc.nsh"
!include "FileFunc.nsh"
!include "Sections.nsh"
!include "LogicLib.nsh"
!include "Memento.nsh"
!include "WordFunc.nsh"
!include "nsDialogs.nsh"

;--------------------------------
;Version information

VIProductVersion ${VER_MAJOR}.${VER_MINOR}.${VER_MICRO}.${VER_BUILD}
VIAddVersionKey "ProductName" "${NAME}"
VIAddVersionKey "Comments" "Atari Running on Any Machine"
VIAddVersionKey "ProductVersion" "${VERSION}${RELEASE_LABEL}"
VIAddVersionKey "FileVersion" "${VERSION}${RELEASE_LABEL}"
VIAddVersionKey "FileDescription" "${NAME} Setup"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2000-2015 ${NAME} developer team"

;--------------------------------
;Configuration

;Memento Settings
!define MEMENTO_REGISTRY_ROOT HKLM
!define MEMENTO_REGISTRY_KEY "${REG_UNINST_KEY}"

;Interface Settings
!define MUI_ABORTWARNING

!define MUI_HEADERIMAGE
!define MUI_WELCOMEFINISHPAGE_BITMAP "aranym\installer_welcome.bmp"

!define MUI_COMPONENTSPAGE_SMALLDESC

;--------------------------------
;Pages

!define MUI_WELCOMEPAGE_TITLE "Welcome to the ${NAME} ${VERSION}${RELEASE_LABEL} Setup Wizard"
!define MUI_WELCOMEPAGE_TEXT "This wizard will guide you through the installation of ${NAME} (Atari Running on Any Machine) ${VERSION}${RELEASE_LABEL}, a software virtual machine (similar to VirtualBox or Bochs) designed and developed for running 32-bit Atari ST/TT/Falcon operating systems (TOS, FreeMiNT, MagiC and Linux-m68k) and TOS/GEM applications on any kind of hardware.$\r$\n$\r$\n$_CLICK"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "COPYING.txt"
!ifdef VER_MAJOR
Page custom PageReinstall PageLeaveReinstall
!endif
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
Page custom PageNetwork PageLeaveNetwork
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchInstalled
!define MUI_FINISHPAGE_RUN_TEXT "Start ARAnyM with my configuration"
!insertmacro MUI_PAGE_FINISH

;--------------------------------
;Languages

!insertmacro MUI_LANGUAGE "English"

;--------------------------------
;Installer Sections

Var NetworkType
Var NetworkSetupDrive
Var NetworkSave
Var NetworkAtariIP
Var NetworkGateway
Var NetworkNetmask
Var NetworkMac
Var NetworkSlirpRadio
Var NetworkPtpRadio
Var NetworkNoneRadio
Var NetworkSaveCheck
Var NetworkAtariIPControl
Var NetworkGatewayControl
Var NetworkNetmaskControl
Var NetworkMacControl


Function ".onInit"
  ${If} ${Silent}
    SetAutoClose true
  ${EndIf}
  StrCpy $ActiveConfig "$PROFILE\aranym\config"
  StrCpy $HadUserConfig 0
  StrCpy $NetworkType "slirp"
  StrCpy $NetworkAtariIP "10.0.2.15"
  StrCpy $NetworkGateway "10.0.2.2"
  StrCpy $NetworkNetmask "255.255.255.0"
  StrCpy $NetworkMac "52:54:00:12:34:56"
  StrCpy $NetworkSave 1
  ${If} ${FileExists} "$PROFILE\aranym\config"
    StrCpy $HadUserConfig 1
    StrCpy $NetworkSave 0
  ${ElseIf} ${FileExists} "$INSTDIR\aranym\config"
    StrCpy $HadUserConfig 1
    StrCpy $NetworkSave 0
    StrCpy $ActiveConfig "$INSTDIR\aranym\config"
  ${EndIf}
!ifdef PRESERVE_USER_DATA
  Call DiscoverActiveConfig
!endif
  ${MementoSectionRestore}
FunctionEnd

Function .onVerifyInstDir
!ifdef PRESERVE_USER_DATA
  Call DiscoverActiveConfig
!else
  ${If} $HadUserConfig == 0
  ${AndIf} ${FileExists} "$INSTDIR\aranym\config"
    StrCpy $HadUserConfig 1
    StrCpy $NetworkSave 0
    StrCpy $ActiveConfig "$INSTDIR\aranym\config"
  ${EndIf}
!endif
FunctionEnd

!ifdef PRESERVE_USER_DATA
Function DiscoverActiveConfig
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=config-locator.exe "config-locator.exe"
  Delete "$PLUGINSDIR\config-location.ini"
  ExecWait '$\"$PLUGINSDIR\config-locator.exe$\" $\"$INSTDIR\aranym.exe$\" $\"$PROFILE\aranym\config$\" $\"$DESKTOP$\" $\"$SMPROGRAMS\${NAME}$\" $\"$PLUGINSDIR\config-location.ini$\"' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP|MB_OK "The existing ARAnyM configuration could not be checked. Installation stopped before changing files." /SD IDOK
    Abort
  ${EndIf}
  ReadINIStr $ActiveConfig "$PLUGINSDIR\config-location.ini" "ARAnyM" "Config"
  ReadINIStr $ActiveShortcut "$PLUGINSDIR\config-location.ini" "ARAnyM" "Shortcut"
  ReadINIStr $KeepDesktopShortcut "$PLUGINSDIR\config-location.ini" "ARAnyM" "KeepDesktop"
  ReadINIStr $KeepMenuShortcut "$PLUGINSDIR\config-location.ini" "ARAnyM" "KeepMenu"
  ReadINIStr $0 "$PLUGINSDIR\config-location.ini" "ARAnyM" "Existing"
  StrCpy $HadUserConfig 0
  StrCpy $NetworkSave 1
  ${If} $0 == 1
  ${OrIf} ${FileExists} "$ActiveConfig"
    StrCpy $HadUserConfig 1
    StrCpy $NetworkSave 0
  ${EndIf}
FunctionEnd
!endif

Function ".onInstSuccess"
  ${MementoSectionSave}
FunctionEnd


Function PageNetwork
  !insertmacro MUI_HEADER_TEXT "Network Setup" "Configure ARAnyM networking"
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 0 100% 24u "Choose how ARAnyM should connect the Atari system to Windows. SLIRP requires no TAP adapter and is recommended for Internet access."
  Pop $0
  ; $%VAR% is expanded when the installer is compiled, which would bake in
  ; the build machine's environment.  Read it on the user's machine instead.
  ReadEnvStr $R0 USERNAME
  ReadEnvStr $R1 COMPUTERNAME
  ${NSD_CreateLabel} 0 27u 100% 12u "Detected Windows user: $R0 on $R1"
  Pop $0

  ${NSD_CreateRadioButton} 0 47u 100% 12u "SLIRP (recommended: user-mode NAT)"
  Pop $NetworkSlirpRadio
  SendMessage $NetworkSlirpRadio ${BM_SETCHECK} ${BST_CHECKED} 0
  ${NSD_CreateRadioButton} 0 64u 100% 12u "TAP/bridge (advanced LAN access)"
  Pop $NetworkPtpRadio
  ${NSD_CreateRadioButton} 0 81u 100% 12u "Disabled"
  Pop $NetworkNoneRadio

  ${NSD_CreateLabel} 0 105u 35% 12u "Atari IP address:"
  Pop $0
  ${NSD_CreateText} 38% 103u 58% 14u "10.0.2.15"
  Pop $NetworkAtariIPControl
  ${NSD_CreateLabel} 0 124u 35% 12u "Gateway:"
  Pop $0
  ${NSD_CreateText} 38% 122u 58% 14u "10.0.2.2"
  Pop $NetworkGatewayControl
  ${NSD_CreateLabel} 0 143u 35% 12u "Netmask:"
  Pop $0
  ${NSD_CreateText} 38% 141u 58% 14u "255.255.255.0"
  Pop $NetworkNetmaskControl
  ${NSD_CreateLabel} 0 162u 35% 12u "MAC address:"
  Pop $0
  ${NSD_CreateText} 38% 160u 58% 14u "52:54:00:12:34:56"
  Pop $NetworkMacControl

  ${NSD_CreateCheckbox} 0 184u 100% 12u "Save these settings to my ARAnyM user configuration"
  Pop $NetworkSaveCheck
  ${If} $HadUserConfig == 0
    SendMessage $NetworkSaveCheck ${BM_SETCHECK} ${BST_CHECKED} 0
  ${EndIf}
  ${If} $ActiveConfig == ""
    SendMessage $NetworkSaveCheck ${BM_SETCHECK} ${BST_UNCHECKED} 0
    EnableWindow $NetworkSaveCheck 0
    ${NSD_CreateLabel} 0 198u 100% 18u "Your existing shortcuts are kept. Open your usual configuration before changing network settings."
    Pop $0
  ${EndIf}

  nsDialogs::Show
FunctionEnd

Function PageLeaveNetwork
  ${NSD_GetState} $NetworkSlirpRadio $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $NetworkType "slirp"
  ${Else}
    ${NSD_GetState} $NetworkPtpRadio $0
    ${If} $0 == ${BST_CHECKED}
      StrCpy $NetworkType "ptp"
    ${Else}
      StrCpy $NetworkType "none"
    ${EndIf}
  ${EndIf}

  ${NSD_GetText} $NetworkAtariIPControl $NetworkAtariIP
  ${NSD_GetText} $NetworkGatewayControl $NetworkGateway
  ${NSD_GetText} $NetworkNetmaskControl $NetworkNetmask
  ${NSD_GetText} $NetworkMacControl $NetworkMac
  ${NSD_GetState} $NetworkSaveCheck $NetworkSave
FunctionEnd

; Defer writes until the default configuration has been installed. Otherwise
; a fresh install creates an ETH0-only config and skips all boot settings.
Function SaveNetworkConfig
  ${If} $ActiveConfig == ""
    Return
  ${EndIf}
  ${If} $NetworkSave != ${BST_CHECKED}
    Return
  ${EndIf}
  CreateDirectory "$PROFILE\aranym"
  WriteINIStr "$ActiveConfig" "ETH0" "Type" "$NetworkType"
  ${If} $NetworkType == "slirp"
    WriteINIStr "$ActiveConfig" "ETH0" "HostIP" "$NetworkGateway"
    WriteINIStr "$ActiveConfig" "ETH0" "AtariIP" "$NetworkAtariIP"
    WriteINIStr "$ActiveConfig" "ETH0" "Netmask" "$NetworkNetmask"
    WriteINIStr "$ActiveConfig" "ETH0" "MAC" "$NetworkMac"
  ${EndIf}
FunctionEnd

Function LaunchInstalled
  ${If} $ActiveShortcut != ""
    ExecShell "open" "$ActiveShortcut"
    Return
  ${EndIf}
  ${If} $ActiveConfig == ""
    Exec '$\"$INSTDIR\aranym.exe$\"'
    Return
  ${EndIf}
  ; Both fresh installs and upgrades use the same normal user config.
  ; Selecting a bundled system must not silently switch the active profile.
  SetOutPath "$INSTDIR"
  Exec '$\"$INSTDIR\aranym.exe$\" -c $\"$ActiveConfig$\"'
FunctionEnd

!ifdef VER_MAJOR

Var ReinstallPageCheck

Function PageReinstall
!ifdef PRESERVE_USER_DATA
  ; Upgrade in place; never offer an older potentially destructive uninstaller.
  Abort
!endif

  ReadRegStr $R0 "${REG_ROOT}" "${REG_APP_PATH}" "InstallLocation"
  ReadRegStr $R1 "${REG_ROOT}" "${REG_UNINST_KEY}" "UninstallString"
  ${IfThen} "$R0$R1" == "" ${|} Abort ${|}

  StrCpy $R4 "older"
  ReadRegDWORD $R0 "${REG_ROOT}" "${REG_APP_PATH}" "VersionMajor"
  ReadRegDWORD $R1 "${REG_ROOT}" "${REG_APP_PATH}" "VersionMinor"
  ReadRegDWORD $R2 "${REG_ROOT}" "${REG_APP_PATH}" "VersionRevision"
  ReadRegDWORD $R3 "${REG_ROOT}" "${REG_APP_PATH}" "VersionBuild"
  ${IfThen} $R0 = 0 ${|} StrCpy $R4 "unknown" ${|} ; Anonymous builds have no version number
  StrCpy $R0 $R0.$R1.$R2.$R3

  ${VersionCompare} ${VER_MAJOR}.${VER_MINOR}.${VER_MICRO}.${VER_BUILD} $R0 $R0
  ${If} $R0 == 0
    StrCpy $R1 "${NAME} ${VERSION} is already installed. Select the operation you want to perform and click Next to continue."
    StrCpy $R2 "Upgrade/repair ARAnyM (preserve configuration)"
    StrCpy $R3 "Uninstall ${NAME} before installing"
    !insertmacro MUI_HEADER_TEXT "Already Installed" "Choose the maintenance option to perform."
    StrCpy $R0 "2"
  ${ElseIf} $R0 == 1
    StrCpy $R1 "An $R4 version of ${NAME} is installed on your system. Choose an upgrade to preserve your configuration, or uninstall first for a clean installation."
    StrCpy $R2 "Upgrade ARAnyM (preserve configuration)"
    StrCpy $R3 "Uninstall before installing"
    !insertmacro MUI_HEADER_TEXT "Already Installed" "Choose how you want to install ${NAME}."
    StrCpy $R0 "1"
  ${ElseIf} $R0 == 2
    StrCpy $R1 "A newer version of ${NAME} is already installed. Choose an upgrade only if you intend to reinstall this older build."
    StrCpy $R2 "Reinstall over existing version (preserve configuration)"
    StrCpy $R3 "Uninstall before installing"
    !insertmacro MUI_HEADER_TEXT "Already Installed" "Choose how you want to install ${NAME}."
    StrCpy $R0 "1"
  ${Else}
    Abort
  ${EndIf}

  nsDialogs::Create 1018
  Pop $R4

  ${NSD_CreateLabel} 0 0 100% 24u $R1
  Pop $R1

  ${NSD_CreateRadioButton} 30u 50u -30u 8u $R2
  Pop $R2
  ${NSD_OnClick} $R2 PageReinstallUpdateSelection

  ${NSD_CreateRadioButton} 30u 70u -30u 8u $R3
  Pop $R3
  ${NSD_OnClick} $R3 PageReinstallUpdateSelection

  ${If} $ReinstallPageCheck != 2
    SendMessage $R2 ${BM_SETCHECK} ${BST_CHECKED} 0
  ${Else}
    SendMessage $R3 ${BM_SETCHECK} ${BST_CHECKED} 0
  ${EndIf}

  ${NSD_SetFocus} $R2

  nsDialogs::Show

FunctionEnd

Function PageReinstallUpdateSelection

  Pop $R1

  ${NSD_GetState} $R2 $R1

  ${If} $R1 == ${BST_CHECKED}
    StrCpy $ReinstallPageCheck 1
  ${Else}
    StrCpy $ReinstallPageCheck 2
  ${EndIf}

FunctionEnd

Function PageLeaveReinstall
!ifdef PRESERVE_USER_DATA
  Return
!endif

  ${NSD_GetState} $R2 $R1

  StrCmp $R1 "1" reinst_done ; Upgrade/repair preserves the existing installation.

  ReadRegStr $R1 "${REG_ROOT}" "${REG_UNINST_KEY}" "UninstallString"

  ; Back up before an older uninstaller can remove application-local config.
    Push "$PROFILE\aranym\config"
    Call BackupConfig
    StrCpy $BeforeUninstallProfile "$LastConfigBackup"
    Push "$INSTDIR\aranym\config"
    Call BackupConfig
    StrCpy $BeforeUninstallApplication "$LastConfigBackup"
  ;Run uninstaller
    HideWindow

    ClearErrors
    ExecWait '$R1 _?=$INSTDIR' $0

    BringToFront

    ${IfThen} ${Errors} ${|} StrCpy $0 2 ${|} ; ExecWait failed, set fake exit code

    ${If} $0 <> 0
    ${OrIf} ${FileExists} "$INSTDIR\aranym.exe"
      ${If} $0 = 1 ; User aborted uninstaller?
        StrCmp $R0 "2" 0 +2 ; Is the existing install the same version?
          Quit ; ...yes, already installed, we are done
        Abort
      ${EndIf}
      MessageBox MB_ICONEXCLAMATION "Unable to uninstall!"
      Abort
    ${Else}
      StrCpy $0 $R1 1
      ${IfThen} $0 == '"' ${|} StrCpy $R1 $R1 -1 1 ${|} ; Strip quotes from UninstallString
      Delete $R1
      ${If} $BeforeUninstallProfile != ""
      ${AndIfNot} ${FileExists} "$PROFILE\aranym\config"
        CreateDirectory "$PROFILE\aranym"
        CopyFiles /SILENT "$BeforeUninstallProfile" "$PROFILE\aranym\config"
      ${EndIf}
      ${If} $BeforeUninstallApplication != ""
      ${AndIfNot} ${FileExists} "$INSTDIR\aranym\config"
        CreateDirectory "$INSTDIR\aranym"
        CopyFiles /SILENT "$BeforeUninstallApplication" "$INSTDIR\aranym\config"
      ${EndIf}
      RMDir $INSTDIR
    ${EndIf}

  reinst_done:

FunctionEnd

!endif # VER_MAJOR

Function BackupConfig
  Exch $R9
  Push $0
  Push $1
  Push $R0
  Push $R1
  Push $R2
  Push $R3
  Push $R4
  Push $R5
  Push $R6
  Push $R7
  Push $R8
  StrCpy $LastConfigBackup ""
  ${IfNot} ${FileExists} "$R9"
    Goto backup_done
  ${EndIf}
  CreateDirectory "$PROFILE\aranym\backups"
  ${GetTime} "" L $R0 $R1 $R2 $R3 $R4 $R5 $R6
  StrCpy $R7 "$PROFILE\aranym\backups\config-$R2-$R1-$R0-$R4$R5$R6"
  StrCpy $R2 0
  backup_retry:
    StrCpy $R8 "$R7-$R2.bak"
    ; Fail-if-exists preserves previous backups even on repeated installs.
    System::Call 'kernel32::CopyFileW(w "$R9", w "$R8", i 1) i.r0 ?e'
    Pop $1
    ${If} $0 == 0
      ${If} $1 == 80
      ${OrIf} $1 == 183
        IntOp $R2 $R2 + 1
        Goto backup_retry
      ${EndIf}
      MessageBox MB_ICONSTOP|MB_OK "The existing configuration could not be backed up. Installation stopped before changing it.$\r$\n$R9" /SD IDOK
      SetErrorLevel 1
      Abort
    ${EndIf}
  WriteINIStr "$R8.origin" "Backup" "OriginalPath" "$R9"
  DetailPrint "Configuration backup: $R8"
  StrCpy $LastConfigBackup "$R8"
  backup_done:
  Pop $R8
  Pop $R7
  Pop $R6
  Pop $R5
  Pop $R4
  Pop $R3
  Pop $R2
  Pop $R1
  Pop $R0
  Pop $1
  Pop $0
  Pop $R9
FunctionEnd

!ifdef PRESERVE_USER_DATA
Section -runtimepreflight
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=runtime-verifier.exe "runtime-verifier.exe"
  File /oname=runtime-manifest.txt "runtime-manifest.txt"
  Delete "$PLUGINSDIR\runtime-preflight.txt"
  ExecWait '$\"$PLUGINSDIR\runtime-verifier.exe$\" preflight $\"$INSTDIR$\" $\"$PLUGINSDIR\runtime-preflight.txt$\" $\"$PLUGINSDIR\runtime-manifest.txt$\"' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP|MB_OK "ARAnyM or one of its runtime DLLs is still open, or the installation directory cannot be updated.$\r$\n$\r$\nClose ARAnyM and every capture/benchmark window, then run this installer again.$\r$\n$\r$\nNo program files were changed." /SD IDOK
    Abort
  ${EndIf}
SectionEnd
!endif

Section -backupconfigs
  Push "$ActiveConfig"
  Call BackupConfig
  Push "$PROFILE\aranym\config"
  Call BackupConfig
  Push "$INSTDIR\aranym\config"
  Call BackupConfig
SectionEnd

; The stuff to install
${MementoSection} "${NAME} Core Files (required)" SecCore
  SectionIn 1 2 3 RO
  ${SetOutPath} $INSTDIR
  
  !include "aranym.files"

${MementoSectionEnd}

!ifdef PRESERVE_USER_DATA
Section -verifyruntime
  ; The x64 executable statically links the GCC and C++ runtimes. Remove
  ; stale copies left by older packages so they cannot mask what is running.
  Delete "$INSTDIR\libstdc++-6.dll"
  Delete "$INSTDIR\libgcc_s_seh-1.dll"
  Delete "$PLUGINSDIR\runtime-verification.txt"
  ExecWait '$\"$PLUGINSDIR\runtime-verifier.exe$\" verify $\"$INSTDIR$\" $\"$PLUGINSDIR\runtime-verification.txt$\" $\"$PLUGINSDIR\runtime-manifest.txt$\"' $0
  CopyFiles /SILENT "$PLUGINSDIR\runtime-verification.txt" "$INSTDIR\ARAnyM-runtime-verification.txt"
  ${If} $0 != 0
    MessageBox MB_ICONSTOP|MB_OK "The installed executable or runtime DLLs did not verify after installation.$\r$\n$\r$\nInstallation has stopped. See:$\r$\n$INSTDIR\ARAnyM-runtime-verification.txt$\r$\n$\r$\nDo not launch ARAnyM from this directory until the verification problem is corrected." /SD IDOK
    Abort
  ${EndIf}
  DetailPrint "Installed executable and runtime DLL hashes verified"
SectionEnd
!endif

;
; Keep the bundled EmuTOS image out of the generic file list.  On a new
; installation it goes to the standard data directory.  If a user config
; already exists, resolve [GLOBAL] EmuTOS exactly as ARAnyM does and ask before
; replacing the image that configuration actually uses.
;
Section -emutosimage
!ifdef PRESERVE_USER_DATA
  ${If} $HadUserConfig == 1
    DetailPrint "Keeping the configured EmuTOS image"
    Return
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\aranym\emutos-aranym.img"
    Return
  ${EndIf}
!endif
  SetOutPath "$PLUGINSDIR"
  File /oname=emutos-aranym.img "aranym\emutos-aranym.img"

  ${IfNot} ${FileExists} "$PROFILE\aranym\config"
    CreateDirectory "$INSTDIR\aranym"
    CopyFiles /SILENT "$PLUGINSDIR\emutos-aranym.img" "$INSTDIR\aranym\emutos-aranym.img"
    Return
  ${EndIf}

  ReadINIStr $R8 "$PROFILE\aranym\config" "GLOBAL" "EmuTOS"
  ${If} $R8 == ""
    DetailPrint "Existing config has no [GLOBAL] EmuTOS image; leaving it unchanged"
    Return
  ${EndIf}

  ; ARAnyM expands ~/ from the user profile, */ from its data directory,
  ; and other relative paths from the directory containing the config file.
  StrCpy $R7 $R8
  StrCpy $R6 $R8 2
  ${If} $R6 == "~/"
  ${OrIf} $R6 == "~\"
    StrCpy $R6 $R8 "" 2
    StrCpy $R7 "$PROFILE\$R6"
  ${ElseIf} $R6 == "*/"
  ${OrIf} $R6 == "*\"
    StrCpy $R6 $R8 "" 2
    StrCpy $R7 "$INSTDIR\aranym\$R6"
  ${Else}
    StrCpy $R6 $R8 1 1
    ${If} $R6 != ":"
      StrCpy $R6 $R8 1
      ${If} $R6 != "\"
      ${AndIf} $R6 != "/"
        StrCpy $R7 "$PROFILE\aranym\$R8"
      ${EndIf}
    ${EndIf}
  ${EndIf}

  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 \
    "Your current ARAnyM configuration uses this EmuTOS image:$\r$\n$R7$\r$\n$\r$\nReplace it with the EmuTOS image included with this installer?$\r$\n$\r$\nIf the file exists, it will first be backed up as:$\r$\n$R7.before-aranym-${VERSION}.bak" /SD IDNO \
    IDYES emutos_replace IDNO emutos_keep

  emutos_keep:
    DetailPrint "Keeping configured EmuTOS image: $R7"
    Return

  emutos_replace:
    ${If} ${FileExists} "$R7"
      ClearErrors
      CopyFiles /SILENT "$R7" "$R7.before-aranym-${VERSION}.bak"
      ${If} ${Errors}
        MessageBox MB_ICONEXCLAMATION|MB_OK "The existing EmuTOS image could not be backed up, so it was not replaced:$\r$\n$R7" /SD IDOK
        Return
      ${EndIf}
    ${EndIf}

    ${GetParent} "$R7" $R6
    CreateDirectory "$R6"
    ClearErrors
    CopyFiles /SILENT "$PLUGINSDIR\emutos-aranym.img" "$R7"
    ${If} ${Errors}
      MessageBox MB_ICONSTOP|MB_OK "The configured EmuTOS image could not be updated:$\r$\n$R7$\r$\n$\r$\nYour configuration was not changed." /SD IDOK
    ${Else}
      DetailPrint "Updated configured EmuTOS image: $R7"
    ${EndIf}
SectionEnd

Section -defaultconfig
!ifdef PRESERVE_USER_DATA
  ${If} $HadUserConfig == 1
    Return
  ${EndIf}
!endif
  SetOverwrite off
  SetOutPath "$INSTDIR\aranym"
  File "aranym\config"
  ${IfNot} ${FileExists} "$PROFILE\aranym\config"
    SetOutPath "$PROFILE\aranym"
    File "aranym\config"
  ${EndIf}
  SetOverwrite on
SectionEnd

Section -networkconfig
  Call SaveNetworkConfig
SectionEnd

Section -networkdriver
  ; NETSETUP.PRG supplies SLIRP's guest address, route, and DNS setup.  TAP
  ; users manage those settings as part of their bridge/LAN configuration,
  ; so do not install the SLIRP setup payload for TAP or disabled networking.
  ${If} $NetworkType != "slirp"
    Return
  ${EndIf}

  ; The Atari-side driver and the setup tool that configures the guest from
  ; whatever was chosen on the network page above.  These live in a folder
  ; that gets mapped as a HOSTFS drive below, so the Atari side can reach
  ; them without the disk image being touched.
  SetOutPath "$INSTDIR\setup"
!ifdef PRESERVE_USER_DATA
  ${If} $HadUserConfig == 1
    SetOverwrite off
  ${EndIf}
!endif
  File "aranym\network\nfeth.xif"
  File "aranym\network\nfeth-config"
  File "aranym\network\netsetup.prg"
  File "aranym\network\eth0-config.sh"
  SetOverwrite on
SectionEnd

;
; Map $INSTDIR\setup as a HOSTFS drive so the Atari side can reach the setup
; tools.  On an upgrade the user's own mappings are left alone: we only claim
; a letter that has no value.  Scanning from Z down because AHDI hands out
; partition letters from C: up, and those are not recorded in the config.
;
Section -hostfsmapping
  ${If} $NetworkSave != ${BST_CHECKED}
    Return
  ${EndIf}
  ; The setup directory above exists only for SLIRP.  In particular, do not
  ; create a stale HOSTFS mapping or show the NETSETUP prompt when TAP was
  ; selected.
  ${If} $NetworkType != "slirp"
    Return
  ${EndIf}

  StrCpy $R9 "ZYXWVUTSRQPONMLKJIHGFEDCBA"
  StrCpy $NetworkSetupDrive ""

  ; Reuse our own mapping if a previous install already made one, rather
  ; than claiming another letter on every upgrade.
  StrCpy $R8 0
  reuse_loop:
    StrCpy $R7 $R9 1 $R8
    StrCmp $R7 "" reuse_done
    ReadINIStr $R6 "$ActiveConfig" "HOSTFS" "$R7"
    StrCmp $R6 "$INSTDIR\setup" 0 reuse_next
      StrCpy $NetworkSetupDrive $R7
      Goto reuse_done
    reuse_next:
    IntOp $R8 $R8 + 1
    Goto reuse_loop
  reuse_done:
  StrCmp $NetworkSetupDrive "" 0 mapping_done

  StrCpy $R8 0
  free_loop:
    StrCpy $R7 $R9 1 $R8
    StrCmp $R7 "" no_free_letter
    ReadINIStr $R6 "$ActiveConfig" "HOSTFS" "$R7"
    StrCmp $R6 "" 0 free_next
      StrCpy $NetworkSetupDrive $R7
      WriteINIStr "$ActiveConfig" "HOSTFS" "$R7" "$INSTDIR\setup"
      Goto mapping_done
    free_next:
    IntOp $R8 $R8 + 1
    Goto free_loop

  no_free_letter:
    MessageBox MB_ICONEXCLAMATION|MB_OK "Every HOSTFS drive letter is already in use, so the network setup folder could not be mapped.$\r$\n$\r$\nMap $INSTDIR\setup to a free letter in the ARAnyM setup GUI, then run NETSETUP.PRG from that drive." /SD IDOK
    Return

  mapping_done:
    MessageBox MB_ICONINFORMATION|MB_OK "Network setup files were installed and mapped as Atari drive $NetworkSetupDrive:$\r$\n$\r$\nTo finish, start ARAnyM and run NETSETUP.PRG from drive $NetworkSetupDrive: on the Atari side.$\r$\n$\r$\nThat configures the Atari network from the settings chosen here. The disk image is not modified." /SD IDOK
SectionEnd

!ifdef EASY_DRIVES
Section "EasyMiNT with updated fVDI (separate system)" SecAtariDrives
  SectionIn 1
  StrCpy $EasyDataDir "$PROFILE\aranym\systems\${EASY_DATA_VERSION}"
  StrCpy $EasyConfigExisted 0
  ${If} ${FileExists} "$EasyDataDir\config"
    StrCpy $EasyConfigExisted 1
  ${EndIf}
  ; User-owned systems are deliberately absent from uninstall.log. Never
  ; overwrite existing data, even when installing the same build again.
  SetOverwrite off
  SetOutPath "$EasyDataDir"
  File /r "aranym\atari\*"
  SetOverwrite on
  ${If} $EasyConfigExisted == 0
    WriteINIStr "$EasyDataDir\config" "ETH0" "Type" "$NetworkType"
    ${If} $NetworkType == "slirp"
      WriteINIStr "$EasyDataDir\config" "ETH0" "HostIP" "$NetworkGateway"
      WriteINIStr "$EasyDataDir\config" "ETH0" "AtariIP" "$NetworkAtariIP"
      WriteINIStr "$EasyDataDir\config" "ETH0" "Netmask" "$NetworkNetmask"
      WriteINIStr "$EasyDataDir\config" "ETH0" "MAC" "$NetworkMac"
      WriteINIStr "$EasyDataDir\config" "HOSTFS" "Z" "$INSTDIR\setup"
    ${EndIf}
  ${EndIf}
  ${If} $HadUserConfig == 0
    CopyFiles /SILENT "$EasyDataDir\config" "$PROFILE\aranym\config"
    WriteINIStr "$PROFILE\aranym\config" "GLOBAL" "EmuTOS" "$EasyDataDir\emutos-aranym.img"
    WriteINIStr "$PROFILE\aranym\config" "IDE0" "Path" "$EasyDataDir\EasyMint\aranym-ide-1.57G.img"
    WriteINIStr "$PROFILE\aranym\config" "HOSTFS" "H" "$EasyDataDir\host_fs"
  ${EndIf}
  ${CreateDirectory} "$SMPROGRAMS\${NAME}"
  ${CreateShortcut} "$SMPROGRAMS\${NAME}\ARAnyM - Separate EasyMiNT system.lnk" "$INSTDIR\aranym.exe" '-c $\"$EasyDataDir\config$\"' "" "" "Separate EasyMiNT system with its own configuration"
  ${CreateShortcut} "$DESKTOP\ARAnyM - Separate EasyMiNT system.lnk" "$INSTDIR\aranym.exe" '-c $\"$EasyDataDir\config$\"' "" "" "Separate EasyMiNT system with its own configuration"
SectionEnd
!endif

!ifndef NO_STARTMENUSHORTCUTS
!define STARTMENU 
${MementoSection} "Start Menu Shortcuts" SecMenuShortcuts
  SetOutPath "$INSTDIR"
  DetailPrint "Installing Start Menu Shortcuts..."
  SectionIn 1 2
  StrCpy $0 "$SMPROGRAMS\${NAME}"
  ${CreateDirectory} "$0"
  ${If} $KeepMenuShortcut != 1
    ${CreateShortcut} "$0\${NAME}${NAMESUFFIX}.lnk" "$INSTDIR\aranym.exe" '-c $\"$ActiveConfig$\"' "" "" "${NAME}"
  ${EndIf}
!ifdef TAP_SETUP
  ${CreateShortcut} "$0\Network adapter setup.lnk" "$INSTDIR\aranym\network\Atari-TAP-Setup.cmd" "" "" "" "Standalone signed TAP setup; no OpenVPN application"
!endif
!if /FileExists "aranym-mmu.exe"
  ${CreateShortcut} "$0\${NAME}${NAMESUFFIX} - MMU.lnk" "$INSTDIR\aranym-mmu.exe" "" "" "" "${NAME} MMU"
!endif
!if /FileExists "aranym-jit.exe"
  ${CreateShortcut} "$0\${NAME}${NAMESUFFIX} - JIT.lnk" "$INSTDIR\aranym-jit.exe" "" "" "" "${NAME} JIT"
!endif
${MementoSectionEnd}
!endif

!ifndef NO_DESKTOPSHORTCUTS
${MementoSection} "Desktop Shortcuts" SecDesktopShortcuts
  SetOutPath "$INSTDIR"
  DetailPrint "Installing Desktop Shortcuts..."
  SectionIn 1 2
  ${If} $KeepDesktopShortcut != 1
    ${CreateShortcut} "$DESKTOP\${NAME}${NAMESUFFIX}.lnk" "$INSTDIR\aranym.exe" '-c $\"$ActiveConfig$\"' "" "" "${NAME}"
  ${EndIf}
${MementoSectionEnd}
!endif

${MementoSectionDone}

Section -post
  SetOutPath "$INSTDIR"
  ${If} ${FileExists} "$DESKTOP\ARAnyM - Updated EasyMiNT.lnk"
    ${CreateShortcut} "$DESKTOP\ARAnyM - Updated EasyMiNT.lnk" "$INSTDIR\aranym.exe" '-c $\"$ActiveConfig$\"' "" "" "ARAnyM with existing user configuration"
  ${EndIf}
  ${If} ${FileExists} "$SMPROGRAMS\${NAME}\ARAnyM - Updated EasyMiNT.lnk"
    ${CreateShortcut} "$SMPROGRAMS\${NAME}\ARAnyM - Updated EasyMiNT.lnk" "$INSTDIR\aranym.exe" '-c $\"$ActiveConfig$\"' "" "" "ARAnyM with existing user configuration"
  ${EndIf}
  ;Write the installation path into the registry
  ${WriteRegExpandStr} "${REG_ROOT}" "${REG_APP_PATH}" "InstallLocation" "$INSTDIR"
!ifdef VER_MAJOR
  ${WriteRegStr} "${REG_ROOT}" "${REG_APP_PATH}" "VersionMajor" "${VER_MAJOR}"
  ${WriteRegStr} "${REG_ROOT}" "${REG_APP_PATH}" "VersionMinor" "${VER_MINOR}"
  ${WriteRegStr} "${REG_ROOT}" "${REG_APP_PATH}" "VersionRevision" "${VER_MICRO}"
  ${WriteRegStr} "${REG_ROOT}" "${REG_APP_PATH}" "VersionBuild" "${VER_BUILD}"
!endif
  ;Write the Uninstall information into the registry
  ${WriteRegExpandStr} "${REG_ROOT}" "${REG_UNINST_KEY}" "UninstallString" "$\"$INSTDIR\uninstall-aranym.exe$\""
  ${WriteRegExpandStr} "${REG_ROOT}" "${REG_UNINST_KEY}" "QuietUninstallString" "$\"$INSTDIR\uninstall-aranym.exe$\" /S"
  ${WriteRegExpandStr} "${REG_ROOT}" "${REG_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  ${WriteRegStr} "${REG_ROOT}" "${REG_UNINST_KEY}" "DisplayName" "${NAME}${NAMESUFFIX}"
  ${WriteRegStr} "${REG_ROOT}" "${REG_UNINST_KEY}" "DisplayIcon" "$INSTDIR\aranym.exe,0"
  ${WriteRegStr} "${REG_ROOT}" "${REG_UNINST_KEY}" "DisplayVersion" "${VERSION}${RELEASE_LABEL}"
  ${WriteRegStr} "${REG_ROOT}" "${REG_UNINST_KEY}" "Publisher" "ARAnyM developer team"

  ${WriteRegStr} "${REG_ROOT}" "${REG_UNINST_KEY}" "URLInfoAbout" "http://aranym.sourceforge.net/"
  ${WriteRegStr} "${REG_ROOT}" "${REG_UNINST_KEY}" "HelpLink" "http://aranym.sourceforge.net"
  ${WriteRegDWORD} "${REG_ROOT}" "${REG_UNINST_KEY}" "NoModify" "1"
  ${WriteRegDWORD} "${REG_ROOT}" "${REG_UNINST_KEY}" "NoRepair" "1"

  ${WriteUninstaller} "$INSTDIR\uninstall-aranym.exe"

SectionEnd


;--------------------------------
;Descriptions

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} "The core files required to use ${NAME} (compiler etc.)"
!ifdef EASY_DRIVES
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAtariDrives} "Installs a separate ready-to-boot EasyMiNT system with updated fVDI. Existing images and settings are kept."
!endif
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMenuShortcuts} "Adds icons to your start menu for easy access"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktopShortcuts} "Adds icons to your desktop for easy access"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
; Uninstaller
;--------------------------------

Function "un.onInit"
  ${If} ${Silent}
    SetAutoClose true
  ${EndIf}
  Call un.InstallLogInit
FunctionEnd

Section "Uninstall"
  ${IfNot} ${FileExists} "$INSTDIR\aranym.exe"
    ${Unless} ${Cmd} `MessageBox MB_YESNO "It does not appear that ${NAME} is installed in the directory '$INSTDIR'.$\r$\nContinue anyway (not recommended)?" IDYES`
      Abort "Uninstall aborted by user"
    ${EndUnless}
  ${EndIf}

  Call un.InstallLog
SectionEnd
