#!/bin/bash

base()
{
	echo `basename $1`
}

extractBase()
{
        if [[ "$1" =~ (.*[0-9])\..* ]]; then
                echo ${BASH_REMATCH[1]}
        else
                echo $1
        fi
}

getExtractCommand()
{
        FILEOUT=$(file $1)
        if [[ "$FILEOUT" =~ "gzip" ]]; then
                echo "tar xzf "
        elif [[ "$FILEOUT" =~ "bzip" ]]; then
                echo "tar xjf "
        elif [[ "$FILEOUT" =~ "zip" ]]; then
                echo "unzip -o "
	else 
		echo $1 NONE
        fi
}

pushd `dirname "$0"` >& /dev/null
export SWD=$PWD
popd >& /dev/null
cd $SWD
export INSTALL_LOCATION=$SWD/install
mkdir $INSTALL_LOCATION
#source build_functions
export LD_LIBRARY_PATH=$INSTALL_LOCATION/lib:$LD_LIBRARY_PATH
export PATH=$INSTALL_LOCATION/bin:$PATH

# FFMPEG dependencies
OGG_URL=https://ftp.osuosl.org/pub/xiph/releases/ogg/libogg-1.3.4.tar.gz
VORBIS_URL=https://ftp.osuosl.org/pub/xiph/releases/vorbis/libvorbis-1.3.6.tar.gz
THEORA_URL=https://ftp.osuosl.org/pub/xiph/releases/theora/libtheora-1.1.1.tar.gz
YASM_URL=http://www.tortall.net/projects/yasm/releases/yasm-1.3.0.tar.gz
NASM_URL=https://www.nasm.us/pub/nasm/releasebuilds/2.14.02/nasm-2.14.02.tar.bz2
X264_URL=https://code.videolan.org/videolan/x264.git
X264_BRANCH=stable
#http://ftp.videolan.org/pub/x264/snapshots/x264-snapshot-20191216-2245-stable.tar.bz2
X265_URL=https://bitbucket.org/multicoreware/x265/downloads/x265_3.3.tar.gz
VPX_URL=https://chromium.googlesource.com/webm/libvpx
VPX_BRANCH=v1.8.2
BASIS_URL=https://github.com/BinomialLLC/basis_universal
BASIS_BRANCH=master
AOM_URL=https://aomedia.googlesource.com/aom
AOM_BRANCH=master

# FFMPEG
FFMPEG_URL=https://ffmpeg.org/releases/ffmpeg-4.2.2.tar.bz2

if [[ $1 == "yes" ]]; then
  wget $OGG_URL
  wget $VORBIS_URL
  wget $THEORA_URL
  wget $YASM_URL
  wget $NASM_URL
  git clone -b $X264_BRANCH $X264_URL 
  wget $X265_URL
  git clone -b $VPX_BRANCH $VPX_URL
  git clone -b $BASIS_BRANCH $BASIS_URL
  git clone -b $AOM_BRANCH $AOM_URL
  wget https://ffmpeg.org/releases/ffmpeg-4.2.2.tar.bz2
  exit 0
fi

for url in $X264_URL $VPX_URL $BASIS_URL $AOM_URL; do
	BASE=$(base $url)
	DIRNAME=$(extractBase $BASE)
	cd $DIRNAME
	make clean
        bash --noprofile --norc ../build/${BASE}.build
	cd ..
done


for url in $OGG_URL $VORBIS_URL $THEORA_URL $YASM_URL $NASM_URL $X265_URL $FFMPEG_URL; do
	BASE=$(base $url)
	DIRNAME=$(extractBase $BASE)
	COMM=$(getExtractCommand $BASE)
	$COMM $BASE
	cd $DIRNAME
	make clean
	bash --noprofile --norc ../build/${BASE}.build
	cd ..
done
