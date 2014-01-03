ifmd.spi
========

A Susie plugin for Markdown/HTML document. This is alpha state but works anyway.

Build
-----

- Makefile is written for mingw64-i686-gcc-g++ on Cygwin 32bit.
- You need Discount library, which is added as a submodule, so you can retrieve by `git submodule init; git submodule update`.
- You also need DispHelper library from http://disphelper.sourceforge.net/ Place single_file_source/* under disphelper/.

Configuration
-------------

Initial size can be specified. Please note that this is not the size of an output image. Actual size may be expanded.

You can configure supported extensions from configuration dialog. Defaults to "\*.md;\*.mkd;\*.mkdn;\*.mdown;\*.markdown" for Markdown and "\*.htm;\*.html" for HTML.

Acknowledgement
---------------

- DispHelper http://disphelper.sourceforge.net/ is used for playing with IDispatch interface.
- Discount http://www.pell.portland.or.us/~orc/Code/discount/ is used for conversion from Markdown to HTML.
- EternalWindows http://eternalwindows.jp/index.html has much useful information to implement this plugin, especially on http://eternalwindows.jp/ole/oledraw/oledraw01.html

Known limitation
----------------

- Character encoding can not be overridden.
- Sometime scroll bar appears.
- When passed by memory, a file having a extension specified as HTML could be treated as Markdown.
- A plain text file as HTML is rendered wihtout line-wrap.
- 24-bit BMP is used even though 2 bits are enough.
- It might produce a very large image.

Author
------

Yak! yak_ex@mx.scn.tv

LICENSE
-------

All source codes except for external libraries are distributed under zlib/libpng license.
http://opensource.org/licenses/Zlib
