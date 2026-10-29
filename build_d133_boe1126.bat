mkdir .tmp
set TMPDIR=.tmp
set TMP=.tmp
set TEMP=.tmp
build d133-boe1126 --jobs 8
if errorlevel 1 pause