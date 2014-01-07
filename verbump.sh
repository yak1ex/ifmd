#!/bin/sh
#
#   verbump.sh: Version bump script
#
#       Copyright (C) 2013 Yak! / Yasutaka ATARASHI
#
#       This software is distributed under the terms of a zlib/libpng License
#
#       $Id$
#

if [ $# -lt 2 ]; then
    cat<<EOF

verbump.sh <version> <files>...

ex. verbump.sh 0.05 foo.{txt,rc,cpp}
    verbump.sh 0_05 foo.{txt,rc,cpp}

EOF
    exit 1
fi
version=`echo $1 | sed 's,_,.,g'`
major=`echo $version | sed 's@\.0*@,@g'`
date=`date +%Y/%m/%d`
version2=`date +$major,%Y,%-m%d`
echo $version $date $version2
shift
for i in $@; do
    case $i in
    *.bak)
        continue
        ;;
    esac
    sed -i.bak "s,[0-9]\.[0-9][0-9] (..../../..),$version ($date),;s,[0-9][0-9][0-9][0-9]/[0-9][0-9]/[0-9][0-9] Yak!,$date Yak!,;s@\(\(FILE\|PRODUCT\)VERSION  *\)[0-9]*,[0-9]*,[0-9]*,[0-9]*@\1$version2@g" $i
    case $i in
    *.txt)
        u2d $i
        ;;
    esac
    diff -u $i.bak $i
done
