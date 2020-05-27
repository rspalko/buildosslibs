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

GDAL_URL=https://github.com/OSGeo/gdal/releases/download/v3.1.0/gdal-3.1.0.tar.gz
GEOS_URL=http://download.osgeo.org/geos/geos-3.8.1.tar.bz2
PROJ_URL=https://download.osgeo.org/proj/proj-6.3.2.tar.gz
SQLITE3_URL=https://www.sqlite.org/2020/sqlite-autoconf-3310100.tar.gz
OPENJPEG_URL=https://github.com/uclouvain/openjpeg/archive/v2.3.1.tar.gz



source ../common/filename.sh

if [ $1 == "yes" ]; then
  wget $GDAL_URL
  wget $GEOS_URL
  wget $PROJ_URL
  wget $PROJ_URL
  wget $SQLITE3_URL
  wget $OPENJPEG_URL -O openjpeg-2.3.1.tar.gz
fi
OPENJPEG_URL=https://github.com/uclouvain/openjpeg/archive/openjpeg-2.3.1.tar.gz

for url in $SQLITE3_URL $PROJ_URL $GEOS_URL $OPENJPEG_URL $GDAL_URL; do
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
