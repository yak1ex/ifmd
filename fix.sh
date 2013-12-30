#!/bin/sh

name=ifmd

if ! grep -q DEBUG ${name}.rc; then
    sed -i.bak -e '/#include <windows.h>/i/***********************************************************************/\
/*                                                                     */\
/* ifmd.rc: Resource file for ifmd                                     */\
/*                                                                     */\
/*     Copyright (C) 2013 Yak! / Yasutaka ATARASHI                     */\
/*                                                                     */\
/*     This software is distributed under the terms of a zlib/libpng   */\
/*     License.                                                        */\
/*                                                                     */\
/*     $Id: 92e6ccc0097dd83dffc6e4b4e6fd95e01a607f1b $                 */\
/*                                                                     */\
/***********************************************************************/' -e 's/FILEFLAGSMASK   0x0000003F/FILEFLAGSMASK   VS_FFI_FILEFLAGSMASK/;/FILEFLAGS       0x00000000/c#ifdef DEBUG\
    FILEFLAGS       VS_FF_DEBUG | VS_FF_PRIVATEBUILD | VS_FF_PRERELEASE\
#else\
    FILEFLAGS       0x00000000\
#endif' -e '/ProductVersion/a#ifdef DEBUG\
            VALUE "PrivateBuild", "Debug build"\
#endif' ${name}.rc
    d2u ${name}.rc.bak
    diff ${name}.rc.bak ${name}.rc
fi

if ! grep -q resource\\.h resource.h; then
    sed -i.bak -e '1i/***********************************************************************/\
/*                                                                     */\
/* resource.h: Header file for windows resource constants              */\
/*                                                                     */\
/*     Copyright (C) 2013 Yak! / Yasutaka ATARASHI                     */\
/*                                                                     */\
/*     This software is distributed under the terms of a zlib/libpng   */\
/*     License.                                                        */\
/*                                                                     */\
/*     $Id$                  */\
/*                                                                     */\
/***********************************************************************/\
#ifndef RESOURCE_H\
#define RESOURCE_H\
' -e '$a\
\
#endif' resource.h
    d2u resource.h.bak
    diff resource.h.bak resource.h
fi
