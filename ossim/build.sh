#!/bin/bash

pushd `dirname "$0"` >& /dev/null
export SWD=$PWD
popd >& /dev/null
cd $SWD
export INSTALL_LOCATION=$SWD/../install
mkdir $INSTALL_LOCATION
#source build_functions
export LD_LIBRARY_PATH=$INSTALL_LOCATION/lib:$LD_LIBRARY_PATH
export PATH=$INSTALL_LOCATION/bin:$PATH

source ../common/filename.sh

BSCRIPT=myconfig.sh
OSSIM_URL=https://github.com/ossimlabs/ossim
OSSIM_PLUGINS_URL=https://github.com/ossimlabs/ossim-plugins
OSSIM_VIDEO_URL=https://github.com/ossimlabs/ossim-video
OSSIM_OMS_URL=https://github.com/ossimlabs/ossim-oms
OSSIM_PLANET_URL=https://github.com/ossimlabs/ossim-planet
GEOS_URL=http://download.osgeo.org/geos/geos-3.8.1.tar.bz2
GEOTIFF_URL=http://download.osgeo.org/geotiff/libgeotiff/libgeotiff-1.5.1.tar.gz
LIBTIFF_URL=https://download.osgeo.org/libtiff/tiff-4.1.0.tar.gz
#PROJ_DATA_URL=https://download.osgeo.org/proj/proj-data-1.0.tar.gz
PROJ_URL=https://download.osgeo.org/proj/proj-6.3.1.tar.gz
SQLITE_URL=https://www.sqlite.org/2020/sqlite-autoconf-3310100.tar.gz
JPEG_URL=https://www.ijg.org/files/jpegsrc.v9d.tar.gz
OPENSCENEGRAPH_URL=https://github.com/openscenegraph/OpenSceneGraph
GPSTK_URL=https://github.com/SGL-UT/GPSTk
CURL_URL=https://curl.haxx.se/download/curl-7.69.1.tar.gz
ZLIB_URL=https://www.zlib.net/zlib-1.2.11.tar.gz
OSSIM_WMS_URL=https://github.com/ossimlabs/ossim-wms
EXPAT_URL=https://github.com/libexpat/libexpat/releases/download/R_2_2_9/expat-2.2.9.tar.gz
OPENJPEG_URL=https://github.com/uclouvain/openjpeg/archive/v2.3.1.tar.gz
GROK_URL=https://github.com/GrokImageCompression/grok

if [ $1 == "yes" ]; then
  git clone -b dev $OSSIM_URL
  git clone -b dev $OSSIM_PLUGINS_URL
  git clone -b dev $OSSIM_VIDEO_URL 
  git clone -b dev $OSSIM_OMS_URL
  git clone -b dev $OSSIM_PLANET_URL
  git clone -b dev $OSSIM_WMS_URL
  wget $GEOS_URL
  wget $GEOTIFF_URL
  wget $LIBTIFF_URL
  wget $PROJ_URL
  wget $SQLITE_URL
  wget $JPEG_URL -O jpeg-9d.tar.gz
  git clone --branch OpenSceneGraph-3.6.5 $OPENSCENEGRAPH_URL
  git clone $GPSTK_URL
  wget $CURL_URL
  wget $ZLIB_URL
  wget $EXPAT_URL
  wget $OPENJPEG_URL -O openjpeg-2.3.1.tar.gz
  git clone https://github.com/GrokImageCompression/grok
fi
# Update URL base on internal dir name
JPEG_URL=https://www.ijg.org/files/jpeg-9d.tar.gz
OPENJPEG_URL=https://github.com/uclouvain/openjpeg/archive/openjpeg-2.3.1.tar.gz

export KAKADU_DIRNAME=v7_9-01368N
cd $KAKADU_DIRNAME/make
make -f Makefile-Linux-x86-64-gcc
cd ..
cp ./lib/Linux-x86-64-gcc/*so $INSTALL_LOCATION/lib
cp managed/all_includes/*.h $INSTALL_LOCATION/include
cd ..
cp $INSTALL_LOCATION/include/openjpeg-2.3/* $INSTALL_LOCATION/include
#for url in $ZLIB_URL $JPEG_URL $EXPAT_URL $CURL_URL $GPSTK_URL $SQLITE_URL $PROJ_URL $LIBTIFF_URL $GEOTIFF_URL $GEOS_URL $OPENSCENEGRAPH_URL $OPENJPEG_URL $GROK_URL $OSSIM_URL; do
for url in $OSSIM_URL; do
        BASE=$(base $url)
        DIRNAME=$(extractBase $BASE)
	if [ ! -d $DIRNAME ]; then
          COMM=$(getExtractCommand $BASE)
	  echo $COMM
          $COMM $BASE
	fi
        cd $DIRNAME
        bash --noprofile --norc ../build/${BASE}.build 
        cd ..
done

cp build/ossim_preferences $INSTALL_LOCATION/share/ossim

source .bashrc
