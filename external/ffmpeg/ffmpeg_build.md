# Build ffmpeg library for 3DS

Initial idea: https://github.com/Core-2-Extreme/Video_player_for_3DS/blob/main/library/ffmpeg_build.md  
Using ffmpeg library for decoding .m4a (aac encoding) audio files. The built files are already committed to the repo, this file is just for documentation of how they were built.

###### .a files:
- `external/ffmpeg/lib/libavcodec.a`
- `external/ffmpeg/lib/libavformat.a`
- `external/ffmpeg/lib/libavutil.a`
- `external/ffmpeg/lib/libswresample.a`

###### .h files in:
- `external/ffmpeg/libavcodec`
- `external/ffmpeg/libavformat`
- `external/ffmpeg/libavutil`
- `external/ffmpeg/libswresample`


## Clone and setup source code to specific version (commit)
Used commit : `Changed arg name` (`2a054b87a7bacf3bc55690f621cf56ed9cf1ba5d`).
```
git clone -b 3ds https://github.com/Core-2-Extreme/FFmpeg_for_3DS && cd FFmpeg_for_3DS && git reset --hard 2a054b87a7bacf3bc55690f621cf56ed9cf1ba5d
```

## Configure and build ffmpeg
```
#!/bin/bash

# Clean previous build
make distclean 2>/dev/null || true

# Configure with all warning suppressions
./configure \
    --prefix=$DEVKITPRO/portlibs/3ds \
    --disable-all \
    --disable-autodetect \
    --disable-avdevice \
    --disable-avfilter \
    --disable-postproc \
    --disable-swscale \
    --disable-programs \
    --disable-doc \
    --disable-htmlpages \
    --disable-manpages \
    --disable-podpages \
    --disable-txtpages \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swresample \
    --enable-decoder=aac \
    --enable-parser=aac \
    --enable-demuxer=mov \
    --enable-demuxer=aac \
    --enable-protocol=file \
    --enable-small \
    --extra-cflags="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfp -mword-relocations -ffunction-sections -O2 -Wno-incompatible-pointer-types -Wno-format -Wno-array-parameter -Wno-switch-unreachable -Wno-unused-function" \
    --extra-ldflags="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfp" \
    --target-os=linux \
    --arch=arm \
    --cpu=arm1176jzf-s \
    --enable-cross-compile \
    --cross-prefix=arm-none-eabi- \
    --disable-asm

# Build
make clean
make -j8
```

## Copy files (**replace {FILE_PATH_TO_PROJECT}** with file path to this repo's root (eg: `/home/username/projects/app`))
```
# make directories if they don't exist
mkdir -p /{FILE_PATH_TO_PROJECT}/external/ffmpeg/libavformat
mkdir -p /{FILE_PATH_TO_PROJECT}/external/ffmpeg/libavcodec
mkdir -p /{FILE_PATH_TO_PROJECT}/external/ffmpeg/libswresample
mkdir -p /{FILE_PATH_TO_PROJECT}/external/ffmpeg/libavutil

# copy header files in root of folders (avoid copying nested header files, which are definitely private and unnecessary)
cp libavformat/*.h /{FILE_PATH_TO_PROJECT}/external/ffmpeg/libavformat
cp libavcodec/*.h /{FILE_PATH_TO_PROJECT}/external/ffmpeg/libavcodec
cp libswresample/*.h /{FILE_PATH_TO_PROJECT}/external/ffmpeg/libswresample
cp libavutil/*.h /{FILE_PATH_TO_PROJECT}/external/ffmpeg/libavutil
```
