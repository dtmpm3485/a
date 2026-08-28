using System.Runtime.InteropServices;
using System.Text;

namespace PrivacyPic;

internal static class NativeCore
{
    [DllImport("privacypic_core.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern uint pp_scan_file(string path);

    [DllImport("privacypic_core.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int pp_sanitize_file(string input, string output);

    [DllImport("privacypic_core.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int pp_license_status();

    [DllImport("privacypic_core.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern uint pp_get_security_flags();

    [DllImport("privacypic_core.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern uint pp_get_batch_limit();

    [DllImport("privacypic_core.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int pp_install_license(string path);

    [DllImport("privacypic_core.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int pp_get_device_id([Out] StringBuilder output, nuint capacity);

    internal const uint Metadata = 1 << 0;
    internal const uint Gps = 1 << 1;
    internal const uint DateTime = 1 << 2;
    internal const uint Device = 1 << 3;
    internal const uint Software = 1 << 4;
    internal const uint XmpIptc = 1 << 5;

    internal static uint Scan(string path) => pp_scan_file(path);
    internal static int Sanitize(string input, string output) => pp_sanitize_file(input, output);
    internal static bool LicenseStatus() => pp_license_status() == 1;
    internal static uint SecurityFlags() => pp_get_security_flags();
    internal static uint BatchLimit() => pp_get_batch_limit();

    internal static int InstallLicense(string path) => pp_install_license(path);

    internal static string GetDeviceId()
    {
        var sb = new StringBuilder(128);
        var rc = pp_get_device_id(sb, 128);
        return rc > 0 ? sb.ToString() : "UNAVAILABLE";
    }

    internal static int SecurityStageCount()
    {
        var bits = SecurityFlags();
        var count = 0;
        for (var i = 0; i < 10; i++)
            if ((bits & (1u << i)) != 0) count++;
        return count;
    }
}
