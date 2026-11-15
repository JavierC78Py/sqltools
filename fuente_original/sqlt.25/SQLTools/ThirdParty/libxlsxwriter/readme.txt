libxlsxwriter 1.2.4 - a C library that writes Excel XLSX files
https://libxlsxwriter.github.io/  |  https://github.com/jmcnamara/libxlsxwriter
License: BSD-2-Clause (see License.txt), so it can be linked into this GPL product.

Vendored unmodified from the v1.2.4 release tag. Nothing here is edited, which keeps an upgrade
to a re-copy. What was taken, out of what upstream ships:

    include/xlsxwriter.h
    include/xlsxwriter/*.h                    every public header
    include/xlsxwriter/third_party/*.h        the headers of the vendored dependencies
    src/*.c                                   the whole library, 26 files
    third_party/dtoa/emyg_dtoa.c              double-to-ASCII; optional upstream, but the docs
    third_party/md5/md5.c                     measure it 40-50% faster on numeric data than the
    third_party/minizip/ioapi.c               CRT, and md5 is what keeps duplicate images out of
    third_party/minizip/zip.c                 the package
    third_party/minizip/iowin32.c             minizip writes the xlsx zip container, and
    third_party/tmpfileplus/tmpfileplus.c     iowin32 is its native-Windows file backend;
                                              tmpfileplus is the temp-file helper upstream
                                              recommends for Windows over the CRT tmpfile()

Left out: test/, examples/, dev/, docs/, the Makefile and CMakeLists.txt, the cocoapods and zig
packaging, and the reading half of minizip (unzip.c, miniunz.c, mztools.c, minizip.c) - this
product writes workbooks and never opens one. third_party/*/*.h is left out as well, because
include/xlsxwriter/third_party/ carries byte-identical copies (checked by hash when this drop was
made) and the project puts that directory on the include path, so the .c files still find their
headers.

zlib is NOT part of this drop. Upstream treats it as the one non-optional external dependency and
requires >= 1.2.8; it is vendored beside this one, in ../zlib.

What SQLTools.vcxproj sets for these files, and why:

    PrecompiledHeader   NotUsing      none of it is app code and none of it includes stdafx.h
    /utf-8                            src/theme.c and src/utility.c carry UTF-8 bytes; upstream
                                      builds with it ("Ensure MSVC supports /utf-8")
    NOCRYPT NOUNCRYPT                 drops minizip's zip encryption, which an xlsx never uses
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS          zlib's gz*.c call open, read, write and close
    4244 off, src/drawing.c only      uint64_t to double in image placement, code this product
                                      never calls; it is the only warning the drop produces at
                                      the project's own warning level

Include directories: include, include/xlsxwriter/third_party, and ../zlib.

One upstream limitation is worth knowing before touching the export code. third_party/tmpfileplus
builds its temporary names with the ANSI CRT calls, and on Windows there is no in-memory alternative:
lxw_get_filehandle is compiled without USE_FMEMOPEN, so every part of the workbook - and every row of
a constant_memory sheet - is spooled through it. A TEMP path holding characters the process ANSI code
page cannot spell is therefore unusable, and tmpfileplus does not fail when it cannot use a directory
it was given: it falls through to TMP, TEMP and finally the current one. The escape hatch is the
tmpdir member of lxw_workbook_options, and OpenGrid/GridSourceExportXlsx.cpp uses it - a directory it
has checked is ASCII, taking the 8.3 name of TEMP when the long one is not. The sources here are
otherwise unmodified, so an upgrade is still a re-copy.
