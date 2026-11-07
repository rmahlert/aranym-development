The test includes the production file-selector and path code. It stubs only
Dialog drawing/event dispatch and uses real Windows directory/drive APIs.
Run in a disposable Wine/Windows profile with C:, D: and empty E: mappings,
and D:\Images\boot.img present. Provide the emulator's SDL/C++ runtime DLLs.
The expected checks cover Computer/PC navigation, C-to-D image selection,
Windows backslash/absolute paths, empty drives and cancellation.
FILE_SELECTOR_SOURCE can select a previous dlgFileSelect.cpp/header pair
for a before/after regression comparison. The previous selector fails the
cross-drive and path tests; the fixed selector passes.
