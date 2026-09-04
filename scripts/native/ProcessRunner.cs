using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace RollbackLab.Tools
{
    public sealed class ProcessRunnerResult
    {
        public int ProcessId { get; internal set; }
        public int ExitCode { get; internal set; }
        public bool TimedOut { get; internal set; }
        public bool AllChildrenExited { get; internal set; }
        public uint TotalProcesses { get; internal set; }
    }

    // A single invocation owns one anonymous job and its complete descendant tree.
    // The caller supplies validated absolute log paths and repository working directory.
    // No process-name lookup, shell, output reader task, or breakaway flag is used.
    public static class ProcessRunner
    {
        private const uint KillOnJobClose = 0x00002000;
        private const uint CreateSuspended = 0x00000004;
        private const uint ExtendedStartupInfoPresent = 0x00080000;
        private const uint CreateNoWindow = 0x08000000;
        private const uint StartfUseShowWindow = 0x00000001;
        private const uint StartfUseStdHandles = 0x00000100;
        private const uint WaitObject0 = 0;
        private const uint WaitTimeout = 258;
        private const uint WaitFailed = 0xffffffff;
        private const uint WatchdogExitCode = 1460; // ERROR_TIMEOUT
        private const uint AuxiliaryExitCode = 1223; // ERROR_CANCELLED
        private const int CleanupTimeoutMs = 10000;
        private static readonly IntPtr InvalidHandle = new IntPtr(-1);

        /// <summary>
        /// Run an executable with inherited environment and separate direct log files.
        /// Normal/crash root exit codes are captured before terminating auxiliaries.
        /// A watchdog terminates the job with exit 1460. Cleanup has an additional
        /// bounded 10-second allowance. AllChildrenExited must be checked by callers;
        /// false is an infrastructure failure, even when the root returned zero.
        /// ExitCode is -1 only if a timed-out root could not be observed exiting.
        /// Start/configuration/Win32 failures throw after owned-handle cleanup.
        /// </summary>
        public static ProcessRunnerResult Run(string exe, string[] args, string workingDirectory,
            string stdoutPath, string stderrPath, int timeoutMs, bool visible = false)
        {
            if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows) || IntPtr.Size != 8)
                throw new PlatformNotSupportedException("ProcessRunner requires Windows x64.");
            ValidatePath(exe, nameof(exe));
            ValidatePath(workingDirectory, nameof(workingDirectory));
            ValidatePath(stdoutPath, nameof(stdoutPath));
            ValidatePath(stderrPath, nameof(stderrPath));
            if (String.Equals(Path.GetFullPath(stdoutPath), Path.GetFullPath(stderrPath), StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("stdoutPath and stderrPath must be distinct files.");
            if (args == null) throw new ArgumentNullException(nameof(args));
            if (timeoutMs <= 0) throw new ArgumentOutOfRangeException(nameof(timeoutMs));
            var commandLine = new StringBuilder(QuoteArgument(exe));
            foreach (string argument in args)
            {
                if (argument == null || argument.IndexOf('\0') >= 0)
                    throw new ArgumentException("Arguments cannot be null or contain NUL.", nameof(args));
                commandLine.Append(' ').Append(QuoteArgument(argument));
            }
            if (commandLine.Length >= 32767)
                throw new ArgumentException("Windows command line exceeds 32766 characters.", nameof(args));

            IntPtr job = IntPtr.Zero;
            IntPtr output = IntPtr.Zero;
            IntPtr error = IntPtr.Zero;
            IntPtr input = IntPtr.Zero;
            IntPtr attributeList = IntPtr.Zero;
            IntPtr inheritedHandles = IntPtr.Zero;
            bool attributeListInitialized = false;
            bool assigned = false;
            bool completed = false;
            ProcessInformation process = new ProcessInformation();
            try
            {
                job = CreateJobObjectW(IntPtr.Zero, null);
                RequireHandle(job, "CreateJobObjectW");
                var limits = new JobExtendedLimitInformation();
                limits.BasicLimitInformation.LimitFlags = KillOnJobClose;
                if (!SetInformationJobObject(job, 9, ref limits, (uint)Marshal.SizeOf<JobExtendedLimitInformation>()))
                    ThrowLastError("SetInformationJobObject");

                var security = new SecurityAttributes();
                security.Length = (uint)Marshal.SizeOf<SecurityAttributes>();
                security.InheritHandle = true;
                output = CreateFileW(stdoutPath, 0x40000000, 7, ref security, 2, 0x80, IntPtr.Zero);
                RequireHandle(output, "CreateFileW(stdout)");
                error = CreateFileW(stderrPath, 0x40000000, 7, ref security, 2, 0x80, IntPtr.Zero);
                RequireHandle(error, "CreateFileW(stderr)");
                input = CreateFileW("NUL", 0x80000000, 3, ref security, 3, 0x80, IntPtr.Zero);
                RequireHandle(input, "CreateFileW(stdin)");

                // Inherit only the three intended standard handles, never any other
                // inheritable handle belonging to the hosting PowerShell process.
                UIntPtr attributeBytes = UIntPtr.Zero;
                InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref attributeBytes);
                if (attributeBytes == UIntPtr.Zero) ThrowLastError("InitializeProcThreadAttributeList(size)");
                attributeList = Marshal.AllocHGlobal(new IntPtr(checked((long)attributeBytes.ToUInt64())));
                if (!InitializeProcThreadAttributeList(attributeList, 1, 0, ref attributeBytes))
                    ThrowLastError("InitializeProcThreadAttributeList");
                attributeListInitialized = true;
                inheritedHandles = Marshal.AllocHGlobal(3 * IntPtr.Size);
                Marshal.WriteIntPtr(inheritedHandles, 0, input);
                Marshal.WriteIntPtr(inheritedHandles, IntPtr.Size, output);
                Marshal.WriteIntPtr(inheritedHandles, 2 * IntPtr.Size, error);
                if (!UpdateProcThreadAttribute(attributeList, 0, new IntPtr(0x00020002),
                    inheritedHandles, new UIntPtr((uint)(3 * IntPtr.Size)), IntPtr.Zero, IntPtr.Zero))
                    ThrowLastError("UpdateProcThreadAttribute(handle list)");

                var startup = new StartupInfoEx();
                startup.StartupInfo.Size = (uint)Marshal.SizeOf<StartupInfoEx>();
                startup.StartupInfo.Flags = StartfUseStdHandles | StartfUseShowWindow;
                startup.StartupInfo.ShowWindow = visible ? (ushort)1 : (ushort)0;
                startup.StartupInfo.StandardInput = input;
                startup.StartupInfo.StandardOutput = output;
                startup.StartupInfo.StandardError = error;
                startup.AttributeList = attributeList;
                uint flags = CreateSuspended | ExtendedStartupInfoPresent;
                if (!visible) flags |= CreateNoWindow;
                if (!CreateProcessW(exe, commandLine, IntPtr.Zero, IntPtr.Zero, true,
                    flags, IntPtr.Zero, workingDirectory, ref startup, out process))
                    ThrowLastError("CreateProcessW");

                // The root cannot spawn a child before it belongs to our job.
                // Assignment may fail under an incompatible outer job; never
                // resume or break away in that case, just terminate the suspended root.
                if (!AssignProcessToJobObject(job, process.Process))
                    ThrowLastError("AssignProcessToJobObject");
                assigned = true;
                if (ResumeThread(process.Thread) == UInt32.MaxValue)
                    ThrowLastError("ResumeThread");

                uint wait = WaitForSingleObject(process.Process, (uint)timeoutMs);
                if (wait == WaitFailed) ThrowLastError("WaitForSingleObject(root)");
                if (wait != WaitObject0 && wait != WaitTimeout)
                    throw new InvalidOperationException("Unexpected process wait result: " + wait);
                bool timedOut = wait == WaitTimeout;
                int rootExit = -1;
                if (!timedOut) rootExit = ReadExitCode(process.Process);

                var cleanupClock = Stopwatch.StartNew();
                // Always seal the job, including on normal root completion. An
                // auxiliary that respawns during cleanup cannot escape ownership.
                if (!TerminateJobObject(job, timedOut ? WatchdogExitCode : AuxiliaryExitCode))
                    ThrowLastError("TerminateJobObject");
                JobAccountingInformation accounting;
                bool allExited = WaitForEmptyJob(job, cleanupClock, out accounting);
                if (timedOut)
                {
                    uint rootWait = WaitForSingleObject(process.Process, RemainingCleanup(cleanupClock));
                    if (rootWait == WaitFailed) ThrowLastError("WaitForSingleObject(terminated root)");
                    if (rootWait == WaitObject0) rootExit = ReadExitCode(process.Process);
                    else allExited = false;
                }
                completed = true;
                return new ProcessRunnerResult {
                    ProcessId = unchecked((int)process.ProcessId), ExitCode = rootExit,
                    TimedOut = timedOut, AllChildrenExited = allExited,
                    TotalProcesses = accounting.TotalProcesses
                };
            }
            finally
            {
                if (!completed && process.Process != IntPtr.Zero)
                {
                    // Preserve the original exception. Kill-on-close remains a
                    // final fallback even if a cleanup Win32 call itself fails.
                    if (assigned)
                    {
                        TerminateJobObject(job, AuxiliaryExitCode);
                        try {
                            JobAccountingInformation ignored;
                            WaitForEmptyJob(job, Stopwatch.StartNew(), out ignored);
                        } catch { }
                    }
                    else
                    {
                        TerminateProcess(process.Process, AuxiliaryExitCode);
                        WaitForSingleObject(process.Process, CleanupTimeoutMs);
                    }
                }
                CloseOwnedHandle(process.Thread);
                CloseOwnedHandle(process.Process);
                CloseOwnedHandle(job);
                if (attributeListInitialized) DeleteProcThreadAttributeList(attributeList);
                if (attributeList != IntPtr.Zero) Marshal.FreeHGlobal(attributeList);
                if (inheritedHandles != IntPtr.Zero) Marshal.FreeHGlobal(inheritedHandles);
                CloseOwnedHandle(input);
                CloseOwnedHandle(error);
                CloseOwnedHandle(output);
            }
        }

        private static void ValidatePath(string path, string name)
        {
            if (String.IsNullOrEmpty(path) || path.IndexOf('\0') >= 0 || !Path.IsPathFullyQualified(path))
                throw new ArgumentException("An absolute path without NUL is required.", name);
        }

        // Microsoft CRT argv quoting: double backslashes before a quote or the
        // closing quote; preserve all other backslashes. Quote even empty argv.
        private static string QuoteArgument(string value)
        {
            var quoted = new StringBuilder("\"");
            int backslashes = 0;
            foreach (char character in value)
            {
                if (character == '\\') { backslashes++; continue; }
                if (character == '"') quoted.Append('\\', backslashes * 2 + 1).Append('"');
                else quoted.Append('\\', backslashes).Append(character);
                backslashes = 0;
            }
            return quoted.Append('\\', backslashes * 2).Append('"').ToString();
        }

        private static uint RemainingCleanup(Stopwatch clock)
        {
            return (uint)Math.Max(0L, CleanupTimeoutMs - clock.ElapsedMilliseconds);
        }

        private static bool WaitForEmptyJob(IntPtr job, Stopwatch clock, out JobAccountingInformation info)
        {
            do
            {
                if (!QueryInformationJobObject(job, 1, out info, (uint)Marshal.SizeOf<JobAccountingInformation>(), IntPtr.Zero))
                    ThrowLastError("QueryInformationJobObject(accounting)");
                if (info.ActiveProcesses == 0) return true;
                uint remaining = RemainingCleanup(clock);
                if (remaining == 0) return false;
                Thread.Sleep((int)Math.Min(remaining, 10U));
            } while (true); // Deadline above bounds every iteration.
        }

        private static int ReadExitCode(IntPtr process)
        {
            uint exitCode;
            if (!GetExitCodeProcess(process, out exitCode)) ThrowLastError("GetExitCodeProcess");
            return unchecked((int)exitCode);
        }

        private static void RequireHandle(IntPtr handle, string operation)
        {
            if (handle == IntPtr.Zero || handle == InvalidHandle) ThrowLastError(operation);
        }

        private static void ThrowLastError(string operation)
        {
            int error = Marshal.GetLastWin32Error();
            throw new Win32Exception(error, operation + " failed (Win32 " + error + ").");
        }

        private static void CloseOwnedHandle(IntPtr handle)
        {
            if (handle != IntPtr.Zero && handle != InvalidHandle) CloseHandle(handle);
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct SecurityAttributes {
            public uint Length; public IntPtr SecurityDescriptor;
            [MarshalAs(UnmanagedType.Bool)] public bool InheritHandle;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct StartupInfo {
            public uint Size; public IntPtr Reserved; public IntPtr Desktop; public IntPtr Title;
            public uint X; public uint Y; public uint XSize; public uint YSize;
            public uint XCountChars; public uint YCountChars; public uint FillAttribute; public uint Flags;
            public ushort ShowWindow; public ushort ReservedBytes; public IntPtr Reserved2;
            public IntPtr StandardInput; public IntPtr StandardOutput; public IntPtr StandardError;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct StartupInfoEx { public StartupInfo StartupInfo; public IntPtr AttributeList; }
        [StructLayout(LayoutKind.Sequential)]
        private struct ProcessInformation {
            public IntPtr Process; public IntPtr Thread; public uint ProcessId; public uint ThreadId;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct JobBasicLimitInformation {
            public long PerProcessUserTimeLimit; public long PerJobUserTimeLimit; public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize; public UIntPtr MaximumWorkingSetSize;
            public uint ActiveProcessLimit; public UIntPtr Affinity; public uint PriorityClass; public uint SchedulingClass;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct IoCounters {
            public ulong ReadOperationCount; public ulong WriteOperationCount; public ulong OtherOperationCount;
            public ulong ReadTransferCount; public ulong WriteTransferCount; public ulong OtherTransferCount;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct JobExtendedLimitInformation {
            public JobBasicLimitInformation BasicLimitInformation; public IoCounters IoInfo;
            public UIntPtr ProcessMemoryLimit; public UIntPtr JobMemoryLimit;
            public UIntPtr PeakProcessMemoryUsed; public UIntPtr PeakJobMemoryUsed;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct JobAccountingInformation {
            public long TotalUserTime; public long TotalKernelTime;
            public long ThisPeriodTotalUserTime; public long ThisPeriodTotalKernelTime;
            public uint TotalPageFaultCount; public uint TotalProcesses;
            public uint ActiveProcesses; public uint TotalTerminatedProcesses;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateJobObjectW(IntPtr securityAttributes, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetInformationJobObject(IntPtr job, int informationClass, ref JobExtendedLimitInformation info, uint size);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool QueryInformationJobObject(IntPtr job, int informationClass, out JobAccountingInformation info, uint size, IntPtr returnLength);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateFileW(string name, uint access, uint share, ref SecurityAttributes attributes, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool InitializeProcThreadAttributeList(IntPtr list, int count, uint flags, ref UIntPtr size);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool UpdateProcThreadAttribute(IntPtr list, uint flags, IntPtr attribute, IntPtr value, UIntPtr size, IntPtr previous, IntPtr returnSize);
        [DllImport("kernel32.dll")]
        private static extern void DeleteProcThreadAttributeList(IntPtr list);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CreateProcessW(string application, StringBuilder commandLine, IntPtr processAttributes,
            IntPtr threadAttributes, [MarshalAs(UnmanagedType.Bool)] bool inheritHandles, uint flags, IntPtr environment,
            string currentDirectory, ref StartupInfoEx startup, out ProcessInformation process);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool TerminateJobObject(IntPtr job, uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool TerminateProcess(IntPtr process, uint exitCode);
        [DllImport("kernel32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr handle);
    }
}
