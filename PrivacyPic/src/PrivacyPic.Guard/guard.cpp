#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cwchar>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

static const char* EXPECTED_APP_SHA256 = "__APP_SHA256__";
static const char* EXPECTED_CORE_SHA256 = "__CORE_SHA256__";

static std::wstring current_exe() {
    wchar_t buf[32768] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n == 0 || n >= 32768) return L"";
    return std::wstring(buf, n);
}

static std::wstring exe_dir() {
    auto p = current_exe();
    const auto pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"" : p.substr(0, pos);
}

static bool sha256_bytes(const unsigned char* data, size_t len, std::vector<unsigned char>& digest) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_len = 0, cb = 0, hash_len = 0;
    std::vector<UCHAR> object;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) goto cleanup;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_len),
                          sizeof(object_len), &cb, 0) != 0) goto cleanup;
    if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_len),
                          sizeof(hash_len), &cb, 0) != 0) goto cleanup;

    object.resize(object_len);
    digest.resize(hash_len);
    if (BCryptCreateHash(alg, &hash, object.data(), object_len, nullptr, 0, 0) != 0) goto cleanup;
    if (len > 0 && BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(len), 0) != 0) goto cleanup;
    if (BCryptFinishHash(hash, digest.data(), hash_len, 0) != 0) goto cleanup;
    ok = true;

cleanup:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
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
    if (!b || a.size() != std::strlen(b)) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
        diff |= static_cast<unsigned char>(x ^ y);
    }
    return diff == 0;
}

static bool launched_by_privacypic() {
    const wchar_t* cmd = GetCommandLineW();
    return cmd && wcsstr(cmd, L"--pp-launch-v2") != nullptr;
}

static bool read_machine_guid(std::wstring& out) {
    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Cryptography",
        0,
        KEY_READ | KEY_WOW64_64KEY,
        &key);
    if (rc != ERROR_SUCCESS) return false;

    wchar_t buf[512] = {};
    DWORD type = 0;
    DWORD size = sizeof(buf);
    rc = RegQueryValueExW(key, L"MachineGuid", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;
    out.assign(buf);
    return !out.empty();
}

static bool make_device_id(std::wstring& out) {
    std::wstring guid;
    if (!read_machine_guid(guid)) return false;

    wchar_t windows_dir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windows_dir, MAX_PATH) == 0) return false;
    wchar_t root[] = L"C:\\";
    root[0] = windows_dir[0];

    DWORD serial = 0;
    GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);

    std::wstring material = L"PrivacyPic.Device.v2|";
    material += guid;
    material += L"|";
    material += std::to_wstring(serial);

    std::vector<unsigned char> digest;
    const auto* bytes = reinterpret_cast<const unsigned char*>(material.data());
    if (!sha256_bytes(bytes, material.size() * sizeof(wchar_t), digest) || digest.size() < 8) return false;

    std::ostringstream ss;
    ss << std::uppercase << std::hex << std::setfill('0');
    for (size_t i = 0; i < 8; ++i) ss << std::setw(2) << static_cast<int>(digest[i]);
    const std::string hex = ss.str();

    std::string id = "PC-" + hex.substr(0,4) + "-" + hex.substr(4,4) + "-" +
                     hex.substr(8,4) + "-" + hex.substr(12,4);
    out.assign(id.begin(), id.end());
    return true;
}

extern "C" __declspec(dllexport) int __cdecl pp_guard_check() {
    if (!launched_by_privacypic()) return 11;

    std::string app_hash;
    if (!sha256_file(current_exe(), app_hash)) return 21;
    if (!equal_ascii_ci(app_hash, EXPECTED_APP_SHA256)) return 22;

    const std::wstring dir = exe_dir();
    if (dir.empty()) return 31;

    const std::wstring core = dir + L"\\privacypic_core.dll";
    std::string core_hash;
    if (!sha256_file(core, core_hash)) return 32;
    if (!equal_ascii_ci(core_hash, EXPECTED_CORE_SHA256)) return 33;

    return 0;
}

extern "C" __declspec(dllexport) int __cdecl pp_guard_launcher_ok() {
    return pp_guard_check() == 0 ? 1 : 0;
}

extern "C" __declspec(dllexport) int __cdecl pp_guard_get_device_id(wchar_t* output, size_t capacity) {
    if (!output || capacity == 0) return -1;
    std::wstring id;
    if (!make_device_id(id)) return -2;
    if (id.size() + 1 > capacity) return -3;
    std::wmemcpy(output, id.c_str(), id.size() + 1);
    return static_cast<int>(id.size());
}
