# Shared by the scripts here: where the Ghidra workspace is and how Ghidra and Python are run, on
# Windows (Git Bash) or Linux. Each can be set in the environment; tools/paths.py reads the same.
#
#   MCM2_WORK          the workspace: proj, bin/Mcm2.exe, scripts, out. C:/mcm2ghidra on Windows,
#                      ~/mcm2ghidra elsewhere.
#   GHIDRA_INSTALL_DIR a Ghidra 12.1 install, or GHIDRA_HEADLESS its analyzeHeadless itself.
#   PYTHON             py -3.12 on Windows, python3 elsewhere.
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
        WINDOWS=1
        WORK=${MCM2_WORK:-C:/mcm2ghidra}
        GH="${GHIDRA_HEADLESS:-$GHIDRA_INSTALL_DIR/support/analyzeHeadless.bat}"
        PYTHON=${PYTHON:-py -3.12} ;;
    *)
        WINDOWS=
        WORK=${MCM2_WORK:-$HOME/mcm2ghidra}
        GH="${GHIDRA_HEADLESS:-$GHIDRA_INSTALL_DIR/support/analyzeHeadless}"
        PYTHON=${PYTHON:-python3} ;;
esac
export MCM2_WORK="$WORK"
[ -f "$GH" ] || { echo "set GHIDRA_INSTALL_DIR to a Ghidra 12.1 install, or GHIDRA_HEADLESS to its analyzeHeadless"; exit 1; }
