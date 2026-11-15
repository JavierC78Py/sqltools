zlib 1.3.1 - compression, and the one non-optional dependency of ../libxlsxwriter
https://zlib.net/  |  https://github.com/madler/zlib
License: zlib (see LICENSE) - permissive, and compatible with this product's GPL.

Vendored unmodified from the v1.3.1 release tag: every .c and .h of the library proper, which is
exactly the set zlib's own win32/Makefile.msc compiles into zlib.lib. Left out are the examples,
the test programs, the other toolchains' build scripts, and contrib/ - libxlsxwriter carries its
own copy of contrib/minizip and that is the one this product builds.

Nothing in SQLTools calls zlib directly. It is here because libxlsxwriter's minizip writes the
xlsx zip container through it, and because upstream requires zlib >= 1.2.8.

It is compiled with the same per-file settings as the libxlsxwriter drop, described in
../libxlsxwriter/readme.txt. _CRT_NONSTDC_NO_WARNINGS is what silences gzlib.c, gzread.c and
gzwrite.c calling open, read, write and close by their POSIX names.
