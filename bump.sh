#!/bin/sh
#
#   bump.sh: Version bump script
#
#       Copyright (C) 2013 Yak! / Yasutaka ATARASHI
#
#       This software is distributed under the terms of a zlib/libpng License
#
#       $Id$
#

version=`echo $1 | sed 's,_,.,g'`
major=`echo $1 | sed 's@_0*@,@g'`
date=`date +%Y/%m/%d`
version2=`date +$major,%Y,%-m%d`
echo $version $date $version2
for i in axffmpeg.cpp axffmpeg.rc axffmpeg.txt; do
    sed -i.bak "s,[0-9]\.[0-9][0-9] (..../../..),$version ($date),;s,[0-9][0-9][0-9][0-9]/[0-9][0-9]/[0-9][0-9] Yak!,$date Yak!,;s@\(FILE\|PRODUCT\)VERSION [0-9]*,[0-9]*,[0-9]*,[0-9]*@\1VERSION $version2@g" $i
    if [ $i = axffmpeg.txt ]; then
        u2d $i
    fi
    diff -u $i.bak $i
done
