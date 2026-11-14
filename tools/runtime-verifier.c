/* Windows installer preflight and post-install runtime verification. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

struct expected_file { const wchar_t *name; };

static const struct expected_file expected[] = {
    { L"aranym.exe" }, { L"SDL2.dll" }, { L"libstdc++-6.dll" },
    { L"libgcc_s_seh-1.dll" }, { L"libwinpthread-1.dll" },
    { L"libslirp-0.dll" }, { L"libglib-2.0-0.dll" },
    { L"libintl-8.dll" }, { L"libiconv-2.dll" },
    { L"libpcre2-8-0.dll" }, { L"libjpeg-8.dll" }
};

static int make_path(wchar_t *out, size_t count, const wchar_t *dir, const wchar_t *name)
{
    size_t length = wcslen(dir);
    int separator = length && dir[length - 1] != L'\\' && dir[length - 1] != L'/';
    return swprintf(out, count, separator ? L"%ls\\%ls" : L"%ls%ls", dir, name) > 0;
}

static int sha256_file(const wchar_t *path, char output[65])
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH digest = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    BYTE buffer[1024 * 1024], value[32];
    DWORD bytes, value_size = sizeof(value);
    int ok = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) goto done;
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &digest)) goto done;
    for (;;) {
        if (!ReadFile(file, buffer, sizeof(buffer), &bytes, NULL)) goto done;
        if (!bytes) break;
        if (!CryptHashData(digest, buffer, bytes, 0)) goto done;
    }
    if (!CryptGetHashParam(digest, HP_HASHVAL, value, &value_size, 0)) goto done;
    for (bytes = 0; bytes < sizeof(value); ++bytes) sprintf(output + bytes * 2, "%02x", value[bytes]);
    output[64] = 0;
    ok = 1;
done:
    if (digest) CryptDestroyHash(digest);
    if (provider) CryptReleaseContext(provider, 0);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return ok;
}

static int preflight(const wchar_t *directory, FILE *report)
{
    size_t index;
    int failures = 0;
    wchar_t path[32768];
    fprintf(report, "ARAnyM runtime replacement preflight\r\n");
    for (index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        HANDLE file;
        if (!make_path(path, sizeof(path) / sizeof(path[0]), directory, expected[index].name)) return 90;
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) continue;
        file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE | DELETE, 0, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) {
            fprintf(report, "LOCKED  %ls  windows-error=%lu\r\n", expected[index].name,
                     (unsigned long)GetLastError());
            ++failures;
        } else {
            fprintf(report, "READY   %ls\r\n", expected[index].name);
            CloseHandle(file);
        }
    }
    fprintf(report, failures ? "PREFLIGHT FAIL failures=%d\r\n" : "PREFLIGHT PASS\r\n", failures);
    return failures ? 20 : 0;
}

static int manifest_hash(FILE *manifest, const wchar_t *wide_name, char output[65])
{
    char hash[65], name[256], wanted[256];
    if (wcstombs(wanted, wide_name, sizeof(wanted)) == (size_t)-1) return 0;
    rewind(manifest);
    while (fscanf(manifest, "%64s %255s", hash, name) == 2) {
        if (!_stricmp(name, wanted)) {
            memcpy(output, hash, 65);
            return 1;
        }
    }
    return 0;
}

static int verify(const wchar_t *directory, FILE *report, FILE *manifest)
{
    size_t index;
    int failures = 0;
    wchar_t path[32768];
    char actual[65], wanted[65];
    fprintf(report, "ARAnyM final RC3 runtime verification\r\n");
    for (index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        if (!make_path(path, sizeof(path) / sizeof(path[0]), directory, expected[index].name)) return 90;
        if (!manifest_hash(manifest, expected[index].name, wanted)) {
            fprintf(report, "MANIFEST-MISSING    %ls\r\n", expected[index].name);
            ++failures;
        } else if (!sha256_file(path, actual)) {
            fprintf(report, "MISSING/UNREADABLE  %ls  windows-error=%lu\r\n", expected[index].name,
                     (unsigned long)GetLastError());
            ++failures;
        } else if (_stricmp(actual, wanted) != 0) {
            fprintf(report, "STALE/WRONG         %ls\r\n", expected[index].name);
            fprintf(report, "  expected %s\r\n  actual   %s\r\n", wanted, actual);
            ++failures;
        } else {
            fprintf(report, "FINAL-RC3           %ls\r\n", expected[index].name);
        }
    }
    fprintf(report, failures ? "VERIFICATION FAIL failures=%d\r\n" : "VERIFICATION PASS\r\n", failures);
    return failures ? 30 : 0;
}

int wmain(int argc, wchar_t **argv)
{
    FILE *report, *manifest;
    int result;
    if (argc != 5 || (wcscmp(argv[1], L"preflight") && wcscmp(argv[1], L"verify"))) return 2;
    report = _wfopen(argv[3], L"wb");
    if (!report) return 3;
    manifest = _wfopen(argv[4], L"rb");
    if (!manifest) { fclose(report); return 4; }
    result = !wcscmp(argv[1], L"preflight") ? preflight(argv[2], report) : verify(argv[2], report, manifest);
    fclose(manifest);
    fclose(report);
    return result;
}
