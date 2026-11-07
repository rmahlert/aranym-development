; ARAnyM Setup Script for NSIS
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
!ifndef EASY_DATA_VERSION
  !define EASY_DATA_VERSION "EasyMiNT"
!endif
Var HadUserConfig
Var EasyDataDir
Var EasyConfigExisted

;--------------------------------
;Configuration

; The name of the installer
Name "${NAME}"
Caption "${NAME} ${VERSION}${NAMESUFFIX} Setup"

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
VIAddVersionKey "ProductVersion" "${VER_MAJOR}.${VER_MINOR}.${VER_MICRO}.${VER_BUILD}"
VIAddVersionKey "FileVersion" "${VERSION}"
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

!define MUI_WELCOMEPAGE_TITLE "Welcome to the ${NAME} ${VERSION}${NAMESUFFIX} Setup Wizard"
!define MUI_WELCOMEPAGE_TEXT "This wizard will guide you through the installation of ${NAME} (Atari Running on Any Machine) ${VERSION}${NAMESUFFIX}, a software virtual machine (similar to VirtualBox or Bochs) designed and developed for running 32-bit Atari ST/TT/Falcon operating systems (TOS, FreeMiNT, MagiC and Linux-m68k) and TOS/GEM applications on any kind of hardware.$\r$\n$\r$\n$_CLICK"

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
!define MUI_FINISHPAGE_RUN_TEXT "Start ARAnyM"
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
  ${EndIf}
  ${MementoSectionRestore}
FunctionEnd

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
  ${If} $NetworkSave != ${BST_CHECKED}
    Return
  ${EndIf}
  CreateDirectory "$PROFILE\aranym"
  WriteINIStr "$PROFILE\aranym\config" "ETH0" "Type" "$NetworkType"
  ${If} $NetworkType == "slirp"
    WriteINIStr "$PROFILE\aranym\config" "ETH0" "HostIP" "$NetworkGateway"
    WriteINIStr "$PROFILE\aranym\config" "ETH0" "AtariIP" "$NetworkAtariIP"
    WriteINIStr "$PROFILE\aranym\config" "ETH0" "Netmask" "$NetworkNetmask"
    WriteINIStr "$PROFILE\aranym\config" "ETH0" "MAC" "$NetworkMac"
  ${EndIf}
FunctionEnd

Function LaunchInstalled
  ${If} $EasyDataDir != ""
    Exec '$\"$INSTDIR\aranym.exe$\" -c $\"$EasyDataDir\config$\"'
  ${Else}
    Exec '$\"$INSTDIR\aranym.exe$\"'
  ${EndIf}
FunctionEnd

!ifdef VER_MAJOR

Var ReinstallPageCheck

Function PageReinstall

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

  ${NSD_GetState} $R2 $R1

  StrCmp $R1 "1" reinst_done ; Upgrade/repair preserves the existing installation.

  ReadRegStr $R1 "${REG_ROOT}" "${REG_UNINST_KEY}" "UninstallString"

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
      RMDir $INSTDIR
    ${EndIf}

  reinst_done:

FunctionEnd

!endif # VER_MAJOR

; The stuff to install
${MementoSection} "${NAME} Core Files (required)" SecCore
  SectionIn 1 2 3 RO
  ${SetOutPath} $INSTDIR
  
  !include "aranym.files"

${MementoSectionEnd}

;
; Keep the bundled EmuTOS image out of the generic file list.  On a new
; installation it goes to the standard data directory.  If a user config
; already exists, resolve [GLOBAL] EmuTOS exactly as ARAnyM does and ask before
; replacing the image that configuration actually uses.
;
Section -emutosimage
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
  ${IfNot} ${FileExists} "$PROFILE\aranym\config"
    SetOutPath "$PROFILE\aranym"
    File "aranym\config"
  ${EndIf}
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
  File "aranym\network\nfeth.xif"
  File "aranym\network\nfeth-config"
  File "aranym\network\netsetup.prg"
  File "aranym\network\eth0-config.sh"
  File "aranym\network\README-setup.txt"
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
    ReadINIStr $R6 "$PROFILE\aranym\config" "HOSTFS" "$R7"
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
    ReadINIStr $R6 "$PROFILE\aranym\config" "HOSTFS" "$R7"
    StrCmp $R6 "" 0 free_next
      StrCpy $NetworkSetupDrive $R7
      WriteINIStr "$PROFILE\aranym\config" "HOSTFS" "$R7" "$INSTDIR\setup"
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
  ${CreateShortcut} "$SMPROGRAMS\${NAME}\ARAnyM - Updated EasyMiNT.lnk" "$INSTDIR\aranym.exe" '-c $\"$EasyDataDir\config$\"' "" "" "Updated EasyMiNT and fVDI"
  ${CreateShortcut} "$DESKTOP\ARAnyM - Updated EasyMiNT.lnk" "$INSTDIR\aranym.exe" '-c $\"$EasyDataDir\config$\"' "" "" "Updated EasyMiNT and fVDI"
SectionEnd
!endif

!ifndef NO_STARTMENUSHORTCUTS
!define STARTMENU 
${MementoSection} "Start Menu Shortcuts" SecMenuShortcuts
  DetailPrint "Installing Start Menu Shortcuts..."
  SectionIn 1 2
  StrCpy $0 "$SMPROGRAMS\${NAME}"
  ${CreateDirectory} "$0"
  ${CreateShortcut} "$0\${NAME}${NAMESUFFIX}.lnk" "$INSTDIR\aranym.exe" "" "" "" "${NAME}"
!ifdef TAP_SETUP
  ${CreateShortcut} "$0\Network adapter setup.lnk" "$INSTDIR\aranym\network\Atari-TAP-Setup.cmd" "" "" "" "Standalone signed TAP setup; no OpenVPN application"
!endif
  ${CreateShortcut} "$0\${NAME}${NAMESUFFIX} - MMU.lnk" "$INSTDIR\aranym-mmu.exe" "" "" "" "${NAME} MMU"
  ${CreateShortcut} "$0\${NAME}${NAMESUFFIX} - JIT.lnk" "$INSTDIR\aranym-jit.exe" "" "" "" "${NAME} JIT"
${MementoSectionEnd}
!endif

!ifndef NO_DESKTOPSHORTCUTS
${MementoSection} "Desktop Shortcuts" SecDesktopShortcuts
  DetailPrint "Installing Desktop Shortcuts..."
  SectionIn 1 2
  ${CreateShortcut} "$DESKTOP\${NAME}${NAMESUFFIX}.lnk" "$INSTDIR\aranym.exe" "" "" "" "${NAME}"
${MementoSectionEnd}
!endif

${MementoSectionDone}

Section -post
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
  ${WriteRegStr} "${REG_ROOT}" "${REG_UNINST_KEY}" "DisplayVersion" "${VERSION}"
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
