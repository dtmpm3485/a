#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "bcrypt.lib")

static const char* EXPECTED_APP_SHA256 = "__APP_SHA256__";
static const char* EXPECTED_CORE_SHA256 = "__CORE_SHA256__";

static std::wstring exe_dir() {
    wchar_t buf[32768] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return L"";
    std::wstring p(buf, n);
    const auto pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"" : p.substr(0, pos);
}

static std::wstring current_exe() {
    wchar_t buf[32768] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return L"";
    return std::wstring(buf, n);
}

static bool sha256_file(const std::wstring& path, std::string& out_hex) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_len = 0, cb = 0, hash_len = 0;
    std::vector<UCHAR> object;
    std::vector<UCHAR> digest;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) goto cleanup;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_len),
                          sizeof(object_len), &cb, 0) != 0) goto cleanup;
    if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_len),
                          sizeof(hash_len), &cb, 0) != 0) goto cleanup;

    object.resize(object_len);
    digest.resize(hash_len);

    if (BCryptCreateHash(alg, &hash, object.data(), object_len, nullptr, 0, 0) != 0) goto cleanup;

    {
        std::vector<UCHAR> buf(64 * 1024);
        DWORD read = 0;
        while (ReadFile(file, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr) && read > 0) {
            if (BCryptHashData(hash, buf.data(), read, 0) != 0) goto cleanup;
        }
    }

    if (BCryptFinishHash(hash, digest.data(), hash_len, 0) != 0) goto cleanup;

    {
        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        for (UCHAR b : digest) ss << std::setw(2) << static_cast<int>(b);
        out_hex = ss.str();
    }
    ok = true;

cleanup:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(file);
    return ok;
}

static bool equal_ascii_ci(const std::string& a, const char* b) {
    if (!b || a.size() != strlen(b)) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
        diff |= static_cast<unsigned char>(x ^ y);
    }
    return diff == 0;
}

extern "C" __declspec(dllexport) int __cdecl pp_guard_check() {
    // Gate A: require the Zig launcher handoff argument.
    const wchar_t* cmd = GetCommandLineW();
    if (!cmd || wcsstr(cmd, L"--pp-launch-v2") == nullptr) return 11;

    // Gate B: verify the C# application binary has not changed.
    std::string app_hash;
    if (!sha256_file(current_exe(), app_hash)) return 21;
    if (!equal_ascii_ci(app_hash, EXPECTED_APP_SHA256)) return 22;

    // Gate C: verify the Rust core has not changed.
    const std::wstring dir = exe_dir();
    if (dir.empty()) return 31;
    const std::wstring core = dir + L"\\privacypic_core.dll";

    std::string core_hash;
    if (!sha256_file(core, core_hash)) return 32;
    if (!equal_ascii_ci(core_hash, EXPECTED_CORE_SHA256)) return 33;

    return 0;
}
