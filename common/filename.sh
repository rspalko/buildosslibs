#!/bin/bash

base()
{
        echo `basename $1`
}

extractBase()
{
	if [[ "$1" =~ ^jpeg.* ]]; then
		echo "jpeg-9d"
        elif [[ "$1" =~ (.*[0-9])\..* ]]; then
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
