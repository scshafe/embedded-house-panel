FROM --platform=linux/amd64 ubuntu@sha256:019e8eb29a85e74d64925745884f2ec79aa27e3feab36353d24656f4d6b89467

ARG DEBIAN_FRONTEND=noninteractive
ARG BHP_LVGL_VERSION=9.5.0
ARG BHP_LVGL_ARCHIVE_SHA256=34a955cdf3a2d005507b704e87357af669a114523b6d3f77b5344fdc68717bc6
ARG BHP_SHELLCHECK_VERSION=0.10.0
ARG BHP_SHELLCHECK_AARCH64_ARCHIVE_SHA256=324a7e89de8fa2aed0d0c28f3dab59cf84c6d74264022c00c22af665ed1a09bb

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
       bc \
       bison \
       build-essential \
       ca-certificates \
       clang \
       cmake \
       cpio \
       curl \
       device-tree-compiler \
       file \
       flex \
       git \
       libcurl4-openssl-dev \
       libdrm-dev \
       libncurses-dev \
       libssl-dev \
       make \
       ninja-build \
       nlohmann-json3-dev \
       openssh-client \
       patch \
       perl \
       pkg-config \
       python3 \
       ripgrep \
       rsync \
       shellcheck \
       unzip \
       wget \
       xz-utils \
    && rm -rf /var/lib/apt/lists/*

COPY system_image/buildroot_external/package/lvgl/lv_conf.h /tmp/bhp-lvgl/lv_conf.h
COPY scripts/verification/shellcheck_with_aarch64_fallback.sh /usr/local/bin/shellcheck

RUN test "$(uname -m)" = x86_64 \
    && curl --fail --location --proto '=https' --tlsv1.2 \
       --output /tmp/bhp-lvgl/lvgl.tar.gz \
       "https://github.com/lvgl/lvgl/archive/refs/tags/v${BHP_LVGL_VERSION}.tar.gz" \
    && printf '%s  %s\n' \
       "${BHP_LVGL_ARCHIVE_SHA256}" /tmp/bhp-lvgl/lvgl.tar.gz \
       | sha256sum --check --strict \
    && mkdir -p /tmp/bhp-lvgl/source \
    && tar --extract --gzip --strip-components=1 \
       --file /tmp/bhp-lvgl/lvgl.tar.gz --directory /tmp/bhp-lvgl/source

RUN curl --fail --location --proto '=https' --tlsv1.2 \
       --output /tmp/bhp-shellcheck-aarch64.tar.xz \
       "https://github.com/koalaman/shellcheck/releases/download/v${BHP_SHELLCHECK_VERSION}/shellcheck-v${BHP_SHELLCHECK_VERSION}.linux.aarch64.tar.xz" \
    && printf '%s  %s\n' \
       "${BHP_SHELLCHECK_AARCH64_ARCHIVE_SHA256}" /tmp/bhp-shellcheck-aarch64.tar.xz \
       | sha256sum --check --strict \
    && mkdir -p /opt/bhp-shellcheck-aarch64 \
    && tar --extract --xz --strip-components=1 \
       --file /tmp/bhp-shellcheck-aarch64.tar.xz \
       --directory /opt/bhp-shellcheck-aarch64 \
    && file /opt/bhp-shellcheck-aarch64/shellcheck | grep -q 'ARM aarch64' \
    && chmod 0555 /usr/local/bin/shellcheck /opt/bhp-shellcheck-aarch64/shellcheck \
    && rm /tmp/bhp-shellcheck-aarch64.tar.xz

RUN set -eu; \
    cmake -S /tmp/bhp-lvgl/source -B /tmp/bhp-lvgl/build -G Ninja \
       -DCMAKE_BUILD_TYPE=Debug \
       -DCMAKE_INSTALL_PREFIX=/opt/bhp-host-dev \
       -DCMAKE_C_COMPILER=/usr/bin/clang \
       -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
       -DCMAKE_C_FLAGS=-I/usr/include/libdrm \
       -DCMAKE_CXX_FLAGS=-I/usr/include/libdrm \
       -DLV_BUILD_CONF_PATH=/tmp/bhp-lvgl/lv_conf.h \
       -DLV_BUILD_LVGL_H_SYSTEM_INCLUDE=ON \
       -DLV_BUILD_SET_CONFIG_OPTS=OFF \
       -DCONFIG_LV_BUILD_EXAMPLES=OFF \
       -DCONFIG_LV_BUILD_DEMOS=OFF \
       -DCONFIG_LV_USE_THORVG_INTERNAL=OFF; \
    cmake --build /tmp/bhp-lvgl/build --parallel 1; \
    cmake --install /tmp/bhp-lvgl/build; \
    test "$(PKG_CONFIG_PATH=/opt/bhp-host-dev/share/pkgconfig \
       pkg-config --modversion lvgl)" = "${BHP_LVGL_VERSION}"; \
    rm -rf /tmp/bhp-lvgl

ARG BHP_VERIFICATION_UID=1000
ARG BHP_VERIFICATION_GID=1000
RUN set -eu; \
    if ! getent group "${BHP_VERIFICATION_GID}" >/dev/null; then \
       groupadd --gid "${BHP_VERIFICATION_GID}" bhp-verifier; \
    fi; \
    if ! getent passwd "${BHP_VERIFICATION_UID}" >/dev/null; then \
       useradd --no-create-home --home-dir /nonexistent \
          --shell /usr/sbin/nologin \
          --uid "${BHP_VERIFICATION_UID}" \
          --gid "${BHP_VERIFICATION_GID}" \
          bhp-verifier; \
    fi

ENV CC=/usr/bin/clang
ENV CXX=/usr/bin/clang++
ENV PKG_CONFIG_PATH=/opt/bhp-host-dev/share/pkgconfig
WORKDIR /workspace

CMD ["make", "verify"]
