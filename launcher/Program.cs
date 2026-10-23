// MCM2:Reborn's launcher: installs MCM2:Reborn from the player's own Motocross Madness 2, keeps it
// up to date from GitHub, shows the changelog and how many are playing, and starts the game.
//
// With --play (for Steam's "Add a Non-Steam Game" or a Lutris entry), once it is installed it
// updates, starts the game by itself and closes when the game does, so the launcher there is the
// game: Steam and Lutris count it running for as long as the game is.

using System;
using System.Linq;
using System.Windows.Forms;

namespace Mcm2Reborn.Launcher
{
    static class Program
    {
        public const string PlayArgument = "--play";

        [STAThread]
        static void Main(string[] args)
        {
            Updates.TidyOldLauncher();
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Theme.Load();
            Application.Run(new LauncherForm(args.Any(a => a.Equals(PlayArgument, StringComparison.OrdinalIgnoreCase))));
        }
    }
}
