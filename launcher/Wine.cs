// Running under Wine or Proton (on Linux, a Steam Deck or a Mac): what the launcher does differently
// there. Wine's ntdll exports wine_get_version, which Windows' does not; its kernel32 turns a Linux
// path into a drive path; and a Linux program (/usr/bin/ffmpeg) can be started as Z:\usr\bin\ffmpeg,
// which Wine runs as the Linux program it is.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace Mcm2Reborn.Launcher
{
    static class Wine
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
        static extern IntPtr GetModuleHandle(string name);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
        static extern IntPtr GetProcAddress(IntPtr module, string name);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl)]
        static extern IntPtr wine_get_dos_file_name(string unix);

        [DllImport("kernel32.dll")]
        static extern IntPtr GetProcessHeap();

        [DllImport("kernel32.dll")]
        static extern bool HeapFree(IntPtr heap, int flags, IntPtr memory);

        static bool? under;

        public static bool Under
        {
            get
            {
                if (under == null)
                {
                    try
                    {
                        under = GetProcAddress(GetModuleHandle("ntdll.dll"), "wine_get_version") != IntPtr.Zero;
                    }
                    catch (Exception)
                    {
                        under = false;
                    }
                }
                return under.Value;
            }
        }

        // A Linux path as Windows programs here see it (/home/me -> Z:\home\me), or null.
        public static string DosPath(string unix)
        {
            if (!Under || string.IsNullOrEmpty(unix) || unix[0] != '/')
                return null;
            try
            {
                IntPtr path = wine_get_dos_file_name(unix);
                if (path != IntPtr.Zero)
                {
                    string dos = Marshal.PtrToStringUni(path);
                    HeapFree(GetProcessHeap(), 0, path);
                    return dos;
                }
            }
            catch (Exception)
            {
                // an older Wine: Z: is the root of the Linux disk in every prefix
            }
            return "Z:" + unix.Replace('/', '\\');
        }

        // The Linux home folder as a drive path, from WINEHOMEDIR (\??\Z:\home\me; Wine does not pass
        // on $HOME); null on Windows.
        public static string Home
        {
            get
            {
                if (!Under)
                    return null;
                string home = Environment.GetEnvironmentVariable("WINEHOMEDIR");
                if (!string.IsNullOrEmpty(home))
                    return home.StartsWith(@"\??\") ? home.Substring(4) : home;
                return DosPath(Environment.GetEnvironmentVariable("HOME"));
            }
        }

        // Where Linux mounts a CD or a disc image: what is in the media folders, and a level below
        // (/run/media/<user>/<disc>).
        public static IEnumerable<string> MountPoints()
        {
            var found = new List<string>();
            if (!Under)
                return found;
            foreach (string root in new[] { "/run/media", "/media", "/mnt" })
                foreach (string mount in Folders(DosPath(root)))
                {
                    found.Add(mount);
                    found.AddRange(Folders(mount));
                }
            return found;
        }

        // The folders in `folder`; none when it is not there or may not be opened.
        static string[] Folders(string folder)
        {
            try
            {
                return folder != null && Directory.Exists(folder) ? Directory.GetDirectories(folder) : new string[0];
            }
            catch (Exception)
            {
                return new string[0];
            }
        }

        // A folder shown in the Linux file manager (winebrowser hands it to xdg-open), else Explorer.
        public static void OpenFolder(string folder)
        {
            if (Under)
            {
                try
                {
                    Process.Start(new ProcessStartInfo("winebrowser.exe", "\"" + folder + "\"") { UseShellExecute = false });
                    return;
                }
                catch (Exception)
                {
                    // no winebrowser: Wine's own Explorer
                }
            }
            Process.Start("explorer.exe", "\"" + folder + "\"");
        }
    }
}
