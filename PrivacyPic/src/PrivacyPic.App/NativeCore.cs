using System.Runtime.InteropServices;

namespace PrivacyPic;

internal static class NativeCore
{
    [DllImport("privacypic_core.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern uint pp_scan_file(string path);

    [DllImport("privacypic_core.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int pp_sanitize_file(string input, string output);

    internal const uint Metadata = 1 << 0;
    internal const uint Gps = 1 << 1;
    internal const uint DateTime = 1 << 2;
    internal const uint Device = 1 << 3;
    internal const uint Software = 1 << 4;
    internal const uint XmpIptc = 1 << 5;

    internal static uint Scan(string path) => pp_scan_file(path);
    internal static int Sanitize(string input, string output) => pp_sanitize_file(input, output);
}
