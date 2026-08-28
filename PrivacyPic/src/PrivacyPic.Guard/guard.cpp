#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

static std::wstring read_machine_guid() {
    wchar_t buffer[256] = {};
    DWORD size = sizeof(buffer);
    LONG rc = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Cryptography",
        L"MachineGuid",
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
        nullptr,
        buffer,
        &size
    );
    return rc == ERROR_SUCCESS ? std::wstring(buffer) : L"";
}

static DWORD system_volume_serial() {
    wchar_t winDir[MAX_PATH] = {};
    if (!GetWindowsDirectoryW(winDir, MAX_PATH)) return 0;
    wchar_t root[4] = L"C:\\";
    if (winDir[1] == L':') root[0] = winDir[0];
    DWORD serial = 0;
    GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    return serial;
}

static std::wstring computer_name() {
    wchar_t name[256] = {};
    DWORD len = 256;
    if (!GetComputerNameW(name, &len)) return L"";
    return std::wstring(name, len);
}

static bool sha256(const BYTE* data, ULONG len, BYTE out[32]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLen = 0, cb = 0;
    std::vector<BYTE> object;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLen), sizeof(objectLen), &cb, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }
    object.resize(objectLen);
    if (BCryptCreateHash(alg, &hash, object.data(), objectLen, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }
    bool ok =
        BCryptHashData(hash, const_cast<PUCHAR>(data), len, 0) == 0 &&
        BCryptFinishHash(hash, out, 32, 0) == 0;

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

extern "C" __declspec(dllexport)
int pp_guard_launcher_ok() {
    wchar_t token[128] = {};
    DWORD len = GetEnvironmentVariableW(L"PRIVACYPIC_LAUNCH_TOKEN", token, 128);
    if (len == 0 || len >= 128) return 0;
    std::wstring value(token, len);
    if (value.rfind(L"PP2-", 0) != 0 || value.size() < 20) return 0;
    return 1;
}

extern "C" __declspec(dllexport)
int pp_guard_get_device_id(wchar_t* output, size_t capacity) {
    if (!output || capacity < 38) return -1;

    const std::wstring guid = read_machine_guid();
    const std::wstring name = computer_name();
    const DWORD serial = system_volume_serial();
    if (guid.empty() || name.empty() || serial == 0) return -2;

    std::wstringstream material;
    material << guid << L"|" << name << L"|" << std::hex << serial;
    const std::wstring raw = material.str();

    BYTE digest[32] = {};
    const BYTE* bytes = reinterpret_cast<const BYTE*>(raw.data());
    const ULONG byteLen = static_cast<ULONG>(raw.size() * sizeof(wchar_t));
    if (!sha256(bytes, byteLen, digest)) return -3;

    std::wstringstream id;
    id << L"PP2-";
    for (int i = 0; i < 16; ++i) {
        id << std::hex << std::setw(2) << std::setfill(L'0') << static_cast<int>(digest[i]);
    }

    const std::wstring value = id.str();
    if (value.size() + 1 > capacity) return -4;
    wcsncpy_s(output, capacity, value.c_str(), _TRUNCATE);
    return static_cast<int>(value.size());
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
    return TRUE;
}
