FROM ubuntu:25.10 AS qt
RUN apt-get update \
 && apt-get install -y --no-install-recommends python3-venv ca-certificates \
 && python3 -m venv /venv \
 && /venv/bin/pip install --no-cache-dir aqtinstall \
 && /venv/bin/aqt install-qt linux desktop 6.8.2 linux_gcc_64 --archives qtbase -O /opt/qt

RUN cd /opt/qt/6.8.2/gcc_64 \
 && rm -rf sbom translations doc phrasebooks \
 && find lib -maxdepth 1 -name 'libQt6*' ! -name 'libQt6Core*' ! -name 'libQt6Gui*' ! -name 'libQt6Widgets*' ! -name 'libQt6DBus*' -delete \
 && find include -mindepth 1 -maxdepth 1 ! -name 'QtCore' ! -name 'QtGui' ! -name 'QtWidgets' ! -name 'QtDBus' -exec rm -rf {} +

FROM ubuntu:25.10
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    clang cmake ninja-build git ca-certificates libstdc++-15-dev libgl-dev libopengl-dev libxkbcommon-dev \
 && dpkg --purge --force-depends \
    mesa-libgallium libgl1-mesa-dri libglx-mesa0 libgbm1 libdrm-amdgpu1 libdrm-intel1 libdrm-radeon1 libdrm-nouveau2 \
    libstdc++-13-dev libgcc-13-dev libtsan2 libasan8 libubsan1 liblsan0 libhwasan0 libperl5.40 perl-modules-5.40 \
    liberror-perl \
 && rm -rf /var/lib/apt/lists/* /usr/share/doc /usr/share/man /usr/share/info /usr/share/locale \
 && mkdir -m 777 /cache /out /build
COPY --from=qt /opt/qt /opt/qt
ENV QT_ROOT_DIR=/opt/qt/6.8.2/gcc_64 CPM_SOURCE_CACHE=/cache HOME=/tmp
WORKDIR /src
CMD ["sh", "-c", "cmake --preset linux-release -B /build -DCMAKE_CXX_SCAN_FOR_MODULES=OFF -DBROMAIDA_BUILD_TESTS=OFF && cmake --build /build && cmake --install /build --prefix /out"]
