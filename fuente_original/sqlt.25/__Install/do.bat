rem C:\Apps\NSIS3\makensis.exe /DSQLTOOLSVER=20b%1 /DSQLTOOLS_BUILDDIR=Release SQLTools.nsi
C:\Apps\NSIS3\makensis.exe /DSQLTOOLSVER=25b%1 /DSQLTOOLS_BUILDDIR=Release /DNOADMIN=1 SQLTools.nsi

if exist SQLTools_25b%1 rmdir /S /Q SQLTools_25b%1
mkdir SQLTools_25b%1
copy ..\sqlqkref\sqlqkref.chm			SQLTools_25b%1
copy ..\_output_\Release\SQLTools.chm	SQLTools_25b%1\SQLToolsU.chm
copy ..\_output_\Release\SQLTools.exe	SQLTools_25b%1\SQLToolsU.exe
copy ..\SQLTools\License.txt			SQLTools_25b%1
copy ..\SQLTools\History.txt			SQLTools_25b%1

mkdir SQLTools_25b%1\Launcher
copy ..\sqlt-launcher\sqlt-launcher.exe .\SQLTools_25b%1\Launcher
copy ..\sqlt-launcher\sqlt-launcher.xml .\SQLTools_25b%1\Launcher

pkzip25 -dir -max -add SQLTools_25b%1 SQLTools_25b%1\*.*
rmdir /S /Q SQLTools_25b%1