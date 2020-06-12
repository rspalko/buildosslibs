#!/bin/bash

pushd `dirname "$0"` >& /dev/null
export SWD=$PWD
popd >& /dev/null
cd $SWD
export OSSIM_INSTALL_PREFIX=$SWD/../install

export LD_LIBRARY_PATH=$OSSIM_INSTALL_PREFIX/lib64:$OSSIM_INSTALL_PREFIX/lib:/home/rp/intel/sw_dev_tools/compilers_and_libraries_2020.1.219/linux/compiler/lib/intel64_lin
export PATH=$OSSIM_INSTALL_PREFIX/bin:$PATH
export OSSIM_PREFS_FILE=$OSSIM_INSTALL_PREFIX/share/ossim/ossim_preferences

