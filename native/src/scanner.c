name: build-arm64
on:
  push:
    tags:
      - v*
  workflow_dispatch:
jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-java@v4
        with:
          distribution: temurin
          java-version: 11
      - name: Install deps
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake ninja-build pkg-config unzip
      - name: Use preinstalled NDK
        run: |
          echo "ANDROID_NDK_HOME=$ANDROID_NDK_HOME" >> $GITHUB_ENV
          ls -d /usr/local/lib/android/sdk/ndk/*
      - name: Build ebur128
        run: |
          git clone --depth 1 https://github.com/jiixyj/libebur128.git
          cmake -S libebur128 -B libebur128/build -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_shared -DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_C_FLAGS="-fPIC -DPIC" -DCMAKE_CXX_FLAGS="-fPIC -DPIC"
          cmake --build libebur128/build -j$(nproc)
          cmake --install libebur128/build --prefix $GITHUB_WORKSPACE/install
      - name: Build TagLib
        run: |
          git clone --depth 1 --branch v2.0.2 --recurse-submodules https://github.com/taglib/taglib.git
          cd taglib
          git submodule update --init --recursive
          cd ..
          cmake -S taglib -B taglib/build -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_shared -DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DWITH_MP4=ON -DWITH_ASF=ON -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_C_FLAGS="-fPIC -DPIC" -DCMAKE_CXX_FLAGS="-fPIC -DPIC"
          cmake --build taglib/build -j$(nproc)
          cmake --install taglib/build --prefix $GITHUB_WORKSPACE/install
      - name: Build FFmpeg arm64
        run: |
          git clone --depth 1 --branch release/7.1 https://github.com/FFmpeg/FFmpeg ffmpeg
          cd ffmpeg
          CC_BIN=$(find $ANDROID_NDK_HOME -name aarch64-linux-android26-clang | head -n1)
          CXX_BIN=$(find $ANDROID_NDK_HOME -name aarch64-linux-android26-clang++ | head -n1)
          AR_BIN=$(find $ANDROID_NDK_HOME -name llvm-ar | head -n1)
          RANLIB_BIN=$(find $ANDROID_NDK_HOME -name llvm-ranlib | head -n1)
          STRIP_BIN=$(find $ANDROID_NDK_HOME -name llvm-strip | head -n1)
          NM_BIN=$(find $ANDROID_NDK_HOME -name llvm-nm | head -n1)
          ./configure --target-os=android --arch=aarch64 --enable-cross-compile --enable-pic --cc="$CC_BIN" --cxx="$CXX_BIN" --ar="$AR_BIN" --ranlib="$RANLIB_BIN" --strip="$STRIP_BIN" --nm="$NM_BIN" --prefix=$GITHUB_WORKSPACE/install --disable-programs --disable-doc --disable-avdevice --disable-swscale --disable-avfilter --enable-avformat --enable-avcodec --enable-swresample --enable-static --disable-shared --enable-decoder=flac --enable-decoder=mp3 --enable-decoder=aac --enable-decoder=alac --enable-decoder=opus --enable-decoder=vorbis --enable-decoder=wavpack --enable-decoder=ape --enable-demuxer=flac --enable-demuxer=mp3 --enable-demuxer=aac --enable-demuxer=mov --enable-demuxer=ogg --enable-demuxer=wav --extra-cflags="-O3 -fPIC -DPIC" --extra-cxxflags="-O3 -fPIC -DPIC" --extra-asflags="-fPIC -DPIC" --extra-ldflags="-fPIC -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384"
          make -j$(nproc)
          make install
      - name: Build scanner
        run: |
          if [ -f native/src/scanner.c ]; then mv native/src/scanner.c native/src/scanner.cpp; fi
          ls -l native/src/
          cmake -S native -B build -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_shared -DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF -DCMAKE_PREFIX_PATH=$GITHUB_WORKSPACE/install -DFFMPEG_DIR=$GITHUB_WORKSPACE/install -DCMAKE_POSITION_INDEPENDENT_CODE=ON
          cmake --build build -j$(nproc)
      - name: Check 16KB alignment
        run: |
          READELF_BIN=$(find $ANDROID_NDK_HOME -name llvm-readelf | head -n1)
          for so in $(find build -name "*.so"); do echo "$so:"; "$READELF_BIN" -l $so | grep -i align; done
      - uses: actions/upload-artifact@v4
        with:
          name: libreplaygain_scanner-arm64
          path: build/libreplaygain_scanner.so
