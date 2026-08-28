using System.Runtime.InteropServices;

namespace PrivacyPic;

internal static class NativeGuard
{
    [DllImport("privacypic_guard.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int pp_guard_check();

    internal static int Check() => pp_guard_check();
}
