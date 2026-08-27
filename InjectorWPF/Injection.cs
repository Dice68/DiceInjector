using System.Runtime.InteropServices;
using System.Text;

namespace InjectorWPF;

internal static class Injection
{
    private const uint ProcessAllAccess = 0x001F0FFF;
    private const uint MemCommit = 0x1000;
    private const uint MemReserve = 0x2000;
    private const uint MemRelease = 0x8000;
    private const uint PageReadWrite = 0x04;
    private const uint Infinite = 0xFFFFFFFF;

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inherit, int pid);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, nuint size, uint type, uint protect);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] buffer, nuint size, out nuint written);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetModuleHandle(string name);

    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
    private static extern IntPtr GetProcAddress(IntPtr module, string proc);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr attributes, nuint stackSize,
        IntPtr startAddress, IntPtr parameter, uint flags, out uint threadId);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetExitCodeThread(IntPtr handle, out uint code);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool VirtualFreeEx(IntPtr process, IntPtr address, nuint size, uint type);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    public static (bool Ok, string Error) Inject(int pid, string dllPath)
    {
        IntPtr process = OpenProcess(ProcessAllAccess, false, pid);
        if (process == IntPtr.Zero)
            return (false, $"can't open process (error {Marshal.GetLastWin32Error()})");

        try
        {
            byte[] payload = Encoding.Unicode.GetBytes(dllPath + "\0");
            IntPtr mem = VirtualAllocEx(process, IntPtr.Zero, (nuint)payload.Length, MemCommit | MemReserve, PageReadWrite);
            if (mem == IntPtr.Zero)
                return (false, $"can't allocate memory (error {Marshal.GetLastWin32Error()})");

            try
            {
                if (!WriteProcessMemory(process, mem, payload, (nuint)payload.Length, out _))
                    return (false, $"can't write memory (error {Marshal.GetLastWin32Error()})");

                IntPtr kernel32 = GetModuleHandle("kernel32.dll");
                IntPtr loadLibrary = GetProcAddress(kernel32, "LoadLibraryW");
                IntPtr thread = CreateRemoteThread(process, IntPtr.Zero, 0, loadLibrary, mem, 0, out _);
                if (thread == IntPtr.Zero)
                    return (false, $"can't create remote thread (error {Marshal.GetLastWin32Error()})");

                try
                {
                    WaitForSingleObject(thread, Infinite);
                    GetExitCodeThread(thread, out uint exitCode);
                    if (exitCode == 0)
                        return (false, "dll load failed inside target");
                    return (true, string.Empty);
                }
                finally
                {
                    CloseHandle(thread);
                }
            }
            finally
            {
                VirtualFreeEx(process, mem, 0, MemRelease);
            }
        }
        finally
        {
            CloseHandle(process);
        }
    }
}
