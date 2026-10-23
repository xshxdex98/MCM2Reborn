// Finding the player's Motocross Madness 2 and making MCM2:Reborn from it.
//
// MCM2:Reborn never carries the game itself: it is made beside the player's own copy - a retail
// install (its data on the disk, or some of it left on the CD for a smaller install) or a copy
// kept anywhere (a "portable" folder). The game is copied to a folder of MCM2:Reborn's own, so
// the original is left as it was, or Reborn is added to the game's folder itself, as the player
// chooses; then the release's files go over it.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Microsoft.Win32;

namespace Mcm2Reborn.Launcher
{
    sealed class GameCopy
    {
        public string Folder;
        public string Kind;         // how it was found: "RETAIL INSTALL", "COPY", "CHOSEN"
        public string DataFolder;   // where its track and bike data are, if not in Folder (the CD)
        public bool Download;       // not a copy here: the game downloaded (Updates.InstallBase)
        public bool NeedsDisc;      // its tracks are not here, and no CD with them is in a drive
        // On a CD, so MCM2:Reborn cannot go into its folder.
        public bool OnDisc => Kind == "CD" || Install.IsDisc(Folder);
        public override string ToString() => Folder;
    }

    static class Install
    {
        public const string KeyPath = @"Software\MCM2Reborn";
        // The launcher's name in an install, whatever the one run was saved as (GitHub makes its spaces dots).
        public const string LauncherName = "MCM2Reborn Launcher.exe";
        const string GameKey = @"Microsoft\Microsoft Games\Motocross Madness 2";

        // What a copy of the game has: its exe, and the folders its menus and races read.
        static readonly string[] DataFolders = { "BIKES", "RIDERS", "SX", "NATIONAL", "BAJA", "ENDURO", "QUARRIES" };
        // What is copied from the CD when an install has left it there.
        static readonly string[] DiscFolders = DataFolders.Concat(new[] { "RES", "TERAFORM" }).ToArray();
        static readonly string[] CopyFolders = { "RES", "UI" };
        // What Setup copies beside the game from the CD's root, and the game does not start without:
        // the licence check it makes at start (EBUEULA.DLL) and the licence that shows (EULA.RTF).
        static readonly string[] SetupFiles = { "EBUEULA.DLL", "EULA.RTF" };

        // Not copied from a copy of the game: a developer's leftovers, recordings and logs.
        static readonly string[] SkipFolders = { "SDK" };
        static readonly string[] SkipExtensions = { ".log", ".csv", ".mp4", ".reg" };

        public static string Installed
        {
            get
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(KeyPath))
                    return key?.GetValue("InstallPath") as string;
            }
            set
            {
                using (RegistryKey key = Registry.CurrentUser.CreateSubKey(KeyPath))
                    key.SetValue("InstallPath", value);
            }
        }

        public static string DefaultFolder =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs",
                         "MCM2 Reborn");

        // The game's exe in a folder, as MCM2:Reborn names it (or the dev build's).
        public static string GameExe(string folder)
        {
            foreach (string name in new[] { "MCM2Reborn.exe", "OpenMCM2-Dev.exe", "OpenMCM2.exe" })
            {
                string path = Path.Combine(folder, name);
                if (File.Exists(path))
                    return path;
            }
            return null;
        }

        public static bool IsGame(string folder)
        {
            try
            {
                return folder != null && File.Exists(Path.Combine(folder, "Mcm2.exe"))
                       && CopyFolders.All(f => Directory.Exists(Path.Combine(folder, f)));
            }
            catch (Exception)
            {
                return false;
            }
        }

        // An install with the game's data: its archives in RES and its tracks in TERAFORM's folders.
        public static bool HasData(string folder)
        {
            try
            {
                string res = Path.Combine(folder, "RES"), tracks = Path.Combine(folder, "TERAFORM");
                return Directory.Exists(res) && Directory.GetFiles(res, "*.res").Length > 0
                       && Directory.Exists(tracks) && Directory.GetFiles(tracks, "*.env", SearchOption.AllDirectories).Length > 0;
            }
            catch (Exception)
            {
                return false;
            }
        }

        // The CD's data folders, as it lays them out.
        static bool DiscHasData(string folder) => DataFolders.All(f => Directory.Exists(Path.Combine(folder, f)))
                                                  || HasData(folder);

        // Every copy of the game this computer has, as far as can be found.
        public static List<GameCopy> Find()
        {
            var found = new List<GameCopy>();
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            void Add(string folder, string kind)
            {
                if (string.IsNullOrEmpty(folder))
                    return;
                try
                {
                    folder = Path.GetFullPath(folder.Trim().Trim('"')).TrimEnd('\\');
                }
                catch (Exception)
                {
                    return;
                }
                if (!seen.Add(folder) || !IsGame(folder))
                    return;
                var copy = new GameCopy { Folder = folder, Kind = kind };
                if (!HasData(folder))
                {
                    copy.DataFolder = FindDisc();
                    copy.NeedsDisc = copy.DataFolder == null;
                }
                found.Add(copy);
            }

            foreach (string folder in RegistryFolders())
                Add(folder, "RETAIL INSTALL");
            string programs = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
            foreach (string root in new[] { programs, Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles) })
                if (!string.IsNullOrEmpty(root))
                    Add(Path.Combine(root, @"Microsoft Games\Motocross Madness 2"), "RETAIL INSTALL");
            foreach (string folder in new[] { @"C:\Games\Motocross Madness 2", @"C:\Games\MCM2", @"C:\MCM2" })
                Add(folder, "COPY");
            foreach (Environment.SpecialFolder place in new[] { Environment.SpecialFolder.Desktop,
                                                                 Environment.SpecialFolder.MyDocuments })
                foreach (string folder in Search(Environment.GetFolderPath(place), 3))
                    Add(folder, "COPY");
            foreach (string folder in Search(Path.Combine(Environment.GetFolderPath(
                         Environment.SpecialFolder.UserProfile), "Downloads"), 3))
                Add(folder, "COPY");

            // The game's CD in a drive: its GAME folder, which Setup would copy.
            foreach (DriveInfo drive in DiscDrives())
            {
                Add(Path.Combine(drive.RootDirectory.FullName, "GAME"), "CD");
                Add(drive.RootDirectory.FullName, "CD");
            }

            // Under Wine or Proton: the Linux home's own folders (a Steam or Lutris prefix has
            // Windows ones of its own), and a CD or disc image Linux has mounted (its GAME folder).
            string home = Wine.Home;
            if (home != null)
                foreach (string place in new[] { "", "Games", "Downloads", "Desktop", "Documents" })
                    foreach (string folder in Search(Path.Combine(home, place), place.Length == 0 ? 1 : 3))
                        Add(folder, "COPY");
            foreach (string mount in Wine.MountPoints())
            {
                Add(Path.Combine(mount, "GAME"), "CD");
                Add(mount, "CD");
            }
            return found;
        }

        // The folders the retail installer left in the registry: any of its values that names a copy.
        static IEnumerable<string> RegistryFolders()
        {
            var folders = new List<string>();
            foreach (RegistryView view in new[] { RegistryView.Registry32, RegistryView.Registry64 })
            {
                try
                {
                    using (RegistryKey machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                    {
                        using (RegistryKey game = machine.OpenSubKey(@"SOFTWARE\" + GameKey))
                            Values(game, folders, 2);
                        using (RegistryKey uninstall = machine.OpenSubKey(
                                   @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"))
                        {
                            if (uninstall == null)
                                continue;
                            foreach (string name in uninstall.GetSubKeyNames())
                                using (RegistryKey entry = uninstall.OpenSubKey(name))
                                    if ((entry?.GetValue("DisplayName") as string)?.IndexOf(
                                            "Motocross Madness 2", StringComparison.OrdinalIgnoreCase) >= 0)
                                        Values(entry, folders, 0);
                        }
                    }
                }
                catch (Exception)
                {
                    // a key the user may not read: the others still count
                }
            }
            using (RegistryKey user = Registry.CurrentUser.OpenSubKey(@"Software\" + GameKey))
                Values(user, folders, 2);
            return folders;
        }

        static void Values(RegistryKey key, List<string> folders, int depth)
        {
            if (key == null)
                return;
            foreach (string name in key.GetValueNames())
            {
                if (!(key.GetValue(name) is string value) || value.Length < 3 || value.IndexOf(':') != 1)
                    continue;
                // A folder, or a file in one (an uninstaller, the exe).
                folders.Add(value);
                try
                {
                    folders.Add(Path.GetDirectoryName(value.Trim('"').Split('"')[0]));
                }
                catch (Exception)
                {
                }
            }
            if (depth > 0)
                foreach (string name in key.GetSubKeyNames())
                    using (RegistryKey sub = key.OpenSubKey(name))
                        Values(sub, folders, depth - 1);
        }

        // Folders under `root`, `depth` deep, that have the game's exe.
        static IEnumerable<string> Search(string root, int depth)
        {
            var found = new List<string>();
            if (string.IsNullOrEmpty(root) || !Directory.Exists(root))
                return found;
            try
            {
                if (File.Exists(Path.Combine(root, "Mcm2.exe")))
                    found.Add(root);
                if (depth > 0)
                    foreach (string folder in Directory.GetDirectories(root))
                        found.AddRange(Search(folder, depth - 1));
            }
            catch (Exception)
            {
                // a folder the user may not open
            }
            return found;
        }

        // The game's CD in a drive, or mounted by Linux: a folder on it with the game's data.
        public static string FindDisc()
        {
            foreach (DriveInfo drive in DiscDrives())
            {
                foreach (string folder in new[] { drive.RootDirectory.FullName }.Concat(
                             Directory.GetDirectories(drive.RootDirectory.FullName)))
                    if (DiscHasData(folder))
                        return folder;
            }
            foreach (string mount in Wine.MountPoints())
                foreach (string folder in new[] { mount, Path.Combine(mount, "GAME") })
                    if (Directory.Exists(folder) && DiscHasData(folder))
                        return folder;
            return null;
        }

        static IEnumerable<DriveInfo> DiscDrives()
        {
            var found = new List<DriveInfo>();
            foreach (DriveInfo drive in DriveInfo.GetDrives())
            {
                try
                {
                    if (drive.DriveType == DriveType.CDRom && drive.IsReady)
                        found.Add(drive);
                }
                catch (Exception)
                {
                    // a drive that went away while being asked
                }
            }
            return found;
        }

        public static bool IsDisc(string folder)
        {
            try
            {
                return new DriveInfo(Path.GetPathRoot(Path.GetFullPath(folder))).DriveType == DriveType.CDRom;
            }
            catch (Exception)
            {
                return false;
            }
        }

        // The game copied from `copy` to `to` (unless they are the same folder), its data from the CD
        // when the install left it there. `progress` hears each file, as the share done (0..1).
        public static void CopyGame(GameCopy copy, string to, Action<string, float> progress)
        {
            var files = new List<(string from, string to)>();
            if (!string.Equals(Path.GetFullPath(copy.Folder).TrimEnd('\\'), Path.GetFullPath(to).TrimEnd('\\'),
                               StringComparison.OrdinalIgnoreCase))
                Gather(copy.Folder, to, files, true);
            if (copy.DataFolder != null)
                foreach (string folder in DiscFolders)
                {
                    string from = Path.Combine(copy.DataFolder, folder);
                    if (Directory.Exists(from) && !Directory.Exists(Path.Combine(to, folder)))
                        Gather(from, Path.Combine(to, folder), files, false);
                }
            // Setup's files, where a copy has none of its own: from the CD's root, beside its GAME folder.
            foreach (string name in SetupFiles)
            {
                if (File.Exists(Path.Combine(copy.Folder, name)))
                    continue;
                string from = new[] { copy.Folder, copy.DataFolder }
                    .Where(f => f != null)
                    .Select(f => new DirectoryInfo(f))
                    .SelectMany(d => new[] { d, d.Parent })
                    .Where(d => d != null)
                    .Select(d => Path.Combine(d.FullName, name))
                    .FirstOrDefault(File.Exists);
                if (from != null)
                    files.Add((from, Path.Combine(to, name)));
            }
            Directory.CreateDirectory(to);
            for (int i = 0; i < files.Count; i++)
            {
                progress(Path.GetFileName(files[i].from), (float) i / Math.Max(1, files.Count));
                Directory.CreateDirectory(Path.GetDirectoryName(files[i].to));
                if (!File.Exists(files[i].to))
                    File.Copy(files[i].from, files[i].to);
                File.SetAttributes(files[i].to, FileAttributes.Normal);     // off a CD, files are read-only
            }
            progress("", 1);
        }

        static void Gather(string from, string to, List<(string, string)> files, bool top)
        {
            foreach (string file in Directory.GetFiles(from))
            {
                string extension = Path.GetExtension(file);
                if (SkipExtensions.Any(e => e.Equals(extension, StringComparison.OrdinalIgnoreCase)))
                    continue;
                files.Add((file, Path.Combine(to, Path.GetFileName(file))));
            }
            foreach (string folder in Directory.GetDirectories(from))
            {
                string name = Path.GetFileName(folder);
                if (top && SkipFolders.Any(s => s.Equals(name, StringComparison.OrdinalIgnoreCase)))
                    continue;
                Gather(folder, Path.Combine(to, name), files, false);
            }
        }

        // The launcher itself in the install folder, and the shortcuts to it.
        // What older copies of the game left that MCM2:Reborn does not use: the portable copy's own
        // launcher, the dgVoodoo DirectX wrapper (Reborn draws with OpenGL) and mcm2hook.
        static readonly string[] Leftovers = {
            "Play MCM2.exe", "DDraw.dll", "D3DImm.dll", "dgVoodoo.conf", "dgVoodooCpl.exe",
            "mcm2hook.dll", "mcm2hook.ini", "mcm2hook.log", "mcm2inject.exe",
        };

        // Those files out of an install, to the Recycle Bin (one in use, by a running game, waits for
        // the next start); how many went.
        public static int RemoveLeftovers(string folder)
        {
            int removed = 0;
            foreach (string name in Leftovers)
            {
                string path = Path.Combine(folder, name);
                if (!File.Exists(path))
                    continue;
                try
                {
                    using (File.Open(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
                    {
                    }                                       // in use: left for the next start, quietly
                    Microsoft.VisualBasic.FileIO.FileSystem.DeleteFile(path,
                        Microsoft.VisualBasic.FileIO.UIOption.OnlyErrorDialogs,
                        Microsoft.VisualBasic.FileIO.RecycleOption.SendToRecycleBin);
                    removed++;
                }
                catch (Exception)
                {
                }
            }
            return removed;
        }

        public static string PlaceLauncher(string folder)
        {
            string self = typeof(Install).Assembly.Location;
            string placed = Path.Combine(folder, LauncherName);
            if (!string.Equals(Path.GetFullPath(self), Path.GetFullPath(placed), StringComparison.OrdinalIgnoreCase))
                File.Copy(self, placed, true);
            // Under Wine a shortcut becomes a Linux menu entry that runs the default prefix, not
            // this one: Steam or Lutris starts the game there instead.
            if (Wine.Under)
                return placed;
            Shortcut(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                                  "MCM2 Reborn.lnk"), placed, folder);
            string menu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "MCM2 Reborn");
            Directory.CreateDirectory(menu);
            Shortcut(Path.Combine(menu, "MCM2 Reborn.lnk"), placed, folder);
            return placed;
        }

        static void Shortcut(string link, string target, string folder)
        {
            try
            {
                Type shell = Type.GetTypeFromProgID("WScript.Shell");
                dynamic wsh = Activator.CreateInstance(shell);
                dynamic shortcut = wsh.CreateShortcut(link);
                shortcut.TargetPath = target;
                shortcut.WorkingDirectory = folder;
                shortcut.Description = "MCM2:Reborn";
                shortcut.Save();
            }
            catch (Exception)
            {
                // no shortcut: the launcher still runs from its folder
            }
        }
    }
}
