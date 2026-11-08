Run test_stage_fvdi.py with Python 3.11+ to check staged binary integrity
and rejection of corrupted or unlisted archive content.

Build lifecycle.c with the MinGW-w64 compiler, -municode, -lole32 -luuid
-lshell32. Run it ONLY in a disposable Windows/Wine profile without an
existing ARAnyM config:
  lifecycle.exe setup.exe EasyMiNT-fvdi-6a641acd
It installs into C:\ARAnyM Installer QA, resolves the desktop shortcut,
modifies the test profile's config/image, reinstalls, and uninstalls.
The image test byte is restored after checking preservation.
boot.c starts an installed emulator/config, captures a Print Screen key
in its own window after boot, then terminates that disposable emulator.

To prepare a full system, tools/stage-easyaramint.py takes a release ZIP
and a separately verified updated image plus its SHA256. It extracts only
release host files/ROM/config, avoiding an existing personal host folder.
Set EASYARAMINT_DIR to that staging directory, FVDI_UPDATE_ZIP to the
checked fVDI archive (with .sha256), EASY_DATA_VERSION to a versioned system
name, and INSTALLER_ONLY=1 when invoking tools/build-windows-dist.sh.

Guest systems live in the Windows user profile, separate from application
files. Reinstall must keep changed images/configs. Uninstall removes the
emulator/shortcuts but leaves guest files and configuration in place.
