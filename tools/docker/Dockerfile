# MCM2:Reborn's build on Linux: the same MSVC as on Windows, run under Wine (msvc-wine), Ghidra
# 12.1.2, and the Python and .NET tools the build uses. tools/docker/run.sh builds this image and
# runs the build in it, with the repository and the Ghidra workspace mounted from the host.
#
# Visual Studio's build tools are not redistributable, so the image downloads them from Microsoft,
# and building it accepts their license (https://go.microsoft.com/fwlink/?LinkId=2086102):
#
#   podman build --build-arg MSVC_LICENSE=accept -t mcm2reborn-build tools/docker
#
# The toolset is pinned to MSVC 14.51, the Windows build's, with the Windows 11 SDK 10.0.26100.
# Microsoft keeps only the newest servicing build of each toolset: when this was written that was
# cl and link 14.51.36260 in the 14.51.36231 folder, and its build of 0.4.0.1 matched the Windows
# release in every byte of code and data - only the link timestamps and the Rich header differed.
FROM docker.io/library/fedora:44

ARG MSVC_LICENSE
RUN [ "$MSVC_LICENSE" = accept ] || { \
        echo "Building this image downloads Visual Studio's build tools, under Microsoft's license:"; \
        echo "  https://go.microsoft.com/fwlink/?LinkId=2086102"; \
        echo "Pass --build-arg MSVC_LICENSE=accept to accept it (tools/docker/run.sh: MSVC_LICENSE=accept)."; \
        exit 1; }

# Fedora's Wine is built for WoW64: 32-bit Windows programs (the game, the tests) run without
# 32-bit Linux libraries. wine-common and wine-cms hold files a new prefix copies in; without them
# its setup asks for them in a dialog nobody sees, and waits for ever.
RUN dnf install -y --setopt=install_weak_deps=False \
        wine-core wine-common wine-cms wine-filesystem msitools git-core perl-interpreter \
        python3 python3-capstone python3-pillow python3-numpy java-25-openjdk-devel dotnet-sdk-8.0 \
        unzip xz findutils procps-ng which diffutils \
    && dnf clean all

# The prefix MSVC runs in. Wine's offer to install Mono and Gecko would wait the same way, and
# neither is needed, so both are left out.
ENV WINEDEBUG=-all WINEPREFIX=/root/.wine
RUN WINEDLLOVERRIDES="mscoree,mshtml=" wine wineboot --init \
    && while pgrep wineserver >/dev/null; do sleep 1; done

# msvc-wine at a known commit, and the toolset it unpacks into /opt/msvc: about 0.9 GB to download,
# less the address sanitizer, code analysis and the CRT's sources, which the build does not use.
ARG MSVC_WINE_COMMIT=514f8ea34842cd6d831804d0e9658d3a32870ae1
ARG MSVC_COMPONENT=Microsoft.VisualStudio.Component.VC.14.51.x86.x64
ARG MSVC_PREFIX=Microsoft.VC.14.51
ARG WINSDK_VERSION=10.0.26100
RUN mkdir -p /tmp/msvc-wine && cd /tmp/msvc-wine \
    && curl -sSfL https://github.com/mstorsjo/msvc-wine/archive/$MSVC_WINE_COMMIT.tar.gz | tar xz --strip-components=1 \
    && skip="" \
    && for p in ASAN.ARM64.base ASAN.X64.base ASAN.X86.base ASAN.X86.OneCore.base \
                CA.Ext.Hostx64.Targetx64.base CA.Ext.Hostx64.Targetx86.base CRT.Source.base; do \
           skip="$skip --ignore $MSVC_PREFIX.$p"; done \
    && PYTHONUNBUFFERED=1 ./vsdownload.py --accept-license --dest /opt/msvc --architecture x86 x64 \
           --sdk-version $WINSDK_VERSION $skip $MSVC_COMPONENT \
    && ./install.sh /opt/msvc \
    && while pgrep wineserver >/dev/null; do sleep 1; done \
    && cd / && rm -rf /tmp/msvc-wine

# Ghidra, as the Windows build has it.
ARG GHIDRA_URL=https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.2_build/ghidra_12.1.2_PUBLIC_20260605.zip
RUN curl -sSfL -o /tmp/ghidra.zip "$GHIDRA_URL" \
    && unzip -q /tmp/ghidra.zip -d /opt && rm /tmp/ghidra.zip \
    && ln -s /opt/ghidra_* /opt/ghidra

ENV MSVC_ROOT=/opt/msvc GHIDRA_INSTALL_DIR=/opt/ghidra MCM2_WORK=/work PYTHON=python3 \
    DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1

COPY entry.sh /usr/local/bin/mcm2-entry
WORKDIR /src
ENTRYPOINT ["sh", "/usr/local/bin/mcm2-entry"]
CMD ["sh", "tools/ghidra/cycle.sh"]
