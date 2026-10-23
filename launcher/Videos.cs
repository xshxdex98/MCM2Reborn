// The game's movies, as MCM2:Reborn plays them. The three in UI (INTRO.AVI, CREDITS.AVI and
// EVENTS.AVI, the main menu's) are Indeo 5, which nothing plays any more: not Windows, not Wine,
// not Proton. The game never opens them; it plays an MPEG-1 copy beside each instead (INTRO.mpg,
// code/dev/video.c), and skips a movie that has none. The copies are made here, with ffmpeg,
// when one is found: an ffmpeg.exe beside the launcher or the game, one on the PATH, MCM2_FFMPEG,
// or under Wine or Proton the Linux system's own.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using Microsoft.Win32;

namespace Mcm2Reborn.Launcher
{
    static class Videos
    {
        // MPEG-1 picture and MP2 sound in an MPEG program stream, which the game's player reads.
        // MPEG-1 knows a few frame rates only: the originals' 15, 16.7 and 0.25 become 30, each
        // frame shown as long as it was.
        const string Options = "-map 0:v:0 -map 0:a:0? -c:v mpeg1video -q:v 2 -r 30 -g 150 "
                             + "-c:a mp2 -b:a 192k -ar 44100 -ac 2 -f mpeg";
        const int WaitMs = 10 * 60 * 1000;
        const string FailedValue = "VideosFailedWith";

        // Linux's places for ffmpeg, and a Mac's (Homebrew), for the launcher under Wine.
        static readonly string[] UnixPlaces = {
            "/usr/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/bin/ffmpeg", "/app/bin/ffmpeg",
            "/opt/homebrew/bin/ffmpeg", "/home/linuxbrew/.linuxbrew/bin/ffmpeg",
        };

        // The movies in the install that have no copy yet, by their names in UI.
        public static List<string> Missing(string folder)
        {
            string ui = Path.Combine(folder, "UI");
            try
            {
                return Directory.Exists(ui)
                    ? Directory.GetFiles(ui, "*.avi").Select(Path.GetFileName)
                               .Where(name => !File.Exists(Path.Combine(ui, Path.ChangeExtension(name, ".mpg"))))
                               .ToList()
                    : new List<string>();
            }
            catch (Exception)
            {
                return new List<string>();
            }
        }

        // An ffmpeg that runs, or null. A Windows one is asked its version; a Linux one under Wine
        // cannot be asked anything (see Convert), so being there is enough.
        public static string FindFfmpeg(string folder)
        {
            foreach (string candidate in Candidates(folder).Distinct(StringComparer.OrdinalIgnoreCase))
            {
                try
                {
                    if (candidate != null && File.Exists(candidate)
                        && (!IsWindowsProgram(candidate) || Run(candidate, "-hide_banner -version", null, 15000)))
                        return candidate;
                }
                catch (Exception)
                {
                    // not a program that runs here
                }
            }
            return null;
        }

        static IEnumerable<string> Candidates(string folder)
        {
            string chosen = Environment.GetEnvironmentVariable("MCM2_FFMPEG");
            if (!string.IsNullOrEmpty(chosen))
                yield return Wine.DosPath(chosen) ?? chosen;
            if (folder != null)
                yield return Path.Combine(folder, "ffmpeg.exe");
            yield return Path.Combine(Path.GetDirectoryName(typeof(Videos).Assembly.Location), "ffmpeg.exe");
            foreach (string dir in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(';'))
                if (dir.Trim().Length > 0)
                    yield return Path.Combine(dir.Trim().Trim('"'), "ffmpeg.exe");
            if (Wine.Under)
            {
                string home = Wine.Home;
                if (home != null)
                    yield return Path.Combine(home, @".local\bin\ffmpeg");
                foreach (string place in UnixPlaces)
                    yield return Wine.DosPath(place);
            }
        }

        static bool IsWindowsProgram(string path)
        {
            using (FileStream file = File.OpenRead(path))
                return file.ReadByte() == 'M' && file.ReadByte() == 'Z';
        }

        // Whether ffmpeg already failed to make the copies here, so a start does not try again.
        public static bool FailedBefore(string ffmpeg)
        {
            using (RegistryKey key = Registry.CurrentUser.OpenSubKey(Install.KeyPath))
                return string.Equals(key?.GetValue(FailedValue) as string, ffmpeg, StringComparison.OrdinalIgnoreCase);
        }

        // The copies of the movies that have none, made with `ffmpeg`; how many could not be made.
        // `progress` hears each movie, as the share done (0..1).
        public static int Convert(string folder, string ffmpeg, Action<string, float> progress)
        {
            List<string> missing = Missing(folder);
            bool windows = IsWindowsProgram(ffmpeg);
            int failed = 0;
            for (int i = 0; i < missing.Count; i++)
            {
                progress(missing[i], (float) i / missing.Count);
                string ui = Path.Combine(folder, "UI");
                string made = Path.Combine(ui, Path.ChangeExtension(missing[i], ".mpg"));
                string part = made + ".part", report = made + ".progress";
                // Paths from the install folder, with forward slashes: a Linux ffmpeg reads them too.
                string arguments = "-nostdin -hide_banner -loglevel error -y -progress \"UI/" + Path.GetFileName(report)
                                 + "\" -i \"UI/" + missing[i] + "\" " + Options + " \"UI/" + Path.GetFileName(part) + "\"";
                Delete(part);
                Delete(report);
                bool ok;
                try
                {
                    ok = windows ? Run(ffmpeg, arguments, folder, WaitMs) : RunUnix(ffmpeg, arguments, folder, report);
                }
                catch (Exception)
                {
                    ok = false;
                }
                ok = ok && File.Exists(part) && new FileInfo(part).Length > 0;
                if (ok)
                {
                    Delete(made);
                    File.Move(part, made);
                }
                else
                {
                    failed++;
                    Delete(part);
                }
                Delete(report);
            }
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(Install.KeyPath))
            {
                if (failed > 0)
                    key.SetValue(FailedValue, ffmpeg);
                else
                    key.DeleteValue(FailedValue, false);
            }
            progress("", 1);
            return failed;
        }

        static void Delete(string path)
        {
            try
            {
                if (File.Exists(path))
                    File.Delete(path);
            }
            catch (Exception)
            {
                // still open: it is written over next time
            }
        }

        // `program` run to its end, its window hidden: whether it ended well.
        static bool Run(string program, string arguments, string folder, int waitMs)
        {
            var start = new ProcessStartInfo(program, arguments) {
                UseShellExecute = false, CreateNoWindow = true, RedirectStandardError = true,
                RedirectStandardOutput = true,
            };
            if (folder != null)
                start.WorkingDirectory = folder;
            using (Process process = Process.Start(start))
            {
                // Both read as it writes, so a full pipe cannot stall it.
                var output = process.StandardOutput.ReadToEndAsync();
                var errors = process.StandardError.ReadToEndAsync();
                if (!process.WaitForExit(waitMs))
                {
                    process.Kill();
                    return false;
                }
                output.Wait();
                errors.Wait();
                return process.ExitCode == 0;
            }
        }

        /*
            A Linux program under Wine: Wine starts it detached, with no handle to wait on and no
            exit code (ntdll's fork_and_exec). ffmpeg's -progress file says how it goes instead:
            "progress=end" once the copy is written. One that stops writing it has failed.
        */
        const int StalledMs = 30000;

        static bool RunUnix(string program, string arguments, string folder, string report)
        {
            Process.Start(new ProcessStartInfo(program, arguments) {
                UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = folder,
            })?.Dispose();
            var watch = Stopwatch.StartNew();
            long seen = -1, changed = 0;
            while (watch.ElapsedMilliseconds < WaitMs)
            {
                System.Threading.Thread.Sleep(250);
                string text = "";
                try
                {
                    using (var file = new FileStream(report, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                    using (var reader = new StreamReader(file))
                        text = reader.ReadToEnd();
                }
                catch (Exception)
                {
                    // not made yet
                }
                if (text.Contains("progress=end"))
                {
                    System.Threading.Thread.Sleep(500);     // the copy closed after its last report
                    return true;
                }
                if (text.Length != seen)
                {
                    seen = text.Length;
                    changed = watch.ElapsedMilliseconds;
                }
                else if (watch.ElapsedMilliseconds - changed > StalledMs)
                    return false;
            }
            return false;
        }
    }
}
