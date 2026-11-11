# syntax=docker/dockerfile:1.7
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# Server build dependencies (meson + the X11 libraries) and xkbcomp/xkb-data, which the unit tests run.
USER root
RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
    apt-get update && apt-get install -yq --no-install-recommends \
        libbsd-dev libdbus-1-dev libdrm-dev libepoxy-dev libgbm-dev libgcrypt20-dev libpciaccess-dev \
        libpixman-1-dev libseat-dev libsystemd-dev libudev-dev libunwind-dev libx11-xcb-dev libxau-dev \
        libxcb-shm0-dev libxcb-xkb-dev libxcb1-dev libxcvt-dev libxdmcp-dev libxfont-dev libxkbfile-dev \
        libxshmfence-dev meson ninja-build pkg-config x11-xkb-utils x11proto-dev xkb-data xorg-dev \
        xorg-sgml-doctools xserver-xorg-dev xtrans-dev \
 && rm -rf /var/lib/apt/lists/*

USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
