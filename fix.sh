#!/bin/sh

if ! grep -q DEBUG axffmpeg.rc; then
    sed -i.bak -e '/#include <windows.h>/i/***********************************************************************/\
/*                                                                     */\
/* axffmpeg.rc: Resource file for axffmpeg                             */\
/*                                                                     */\
/*     Copyright (C) 2012,2013 Yak! / Yasutaka ATARASHI                */\
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
#endif' axffmpeg.rc
    d2u axffmpeg.rc.bak
    diff axffmpeg.rc.bak axffmpeg.rc
fi

if ! grep -q resource\\.h resource.h; then
    sed -i.bak -e '1i/***********************************************************************/\
/*                                                                     */\
/* resource.h: Header file for windows resource constants              */\
/*                                                                     */\
/*     Copyright (C) 2012 Yak! / Yasutaka ATARASHI                     */\
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
