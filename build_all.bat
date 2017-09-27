
cd driver
build -ceZ
cd ..

copy /Y driver\filterdrv.inf bin\filterdrv.inf
copy /Y driver\objchk_win7_amd64\amd64\filterdrv.sys bin\filterdrv.sys

cd test
build -ceZ
cd ..

copy /Y test\objchk_win7_amd64\amd64\testdrv.exe bin\testdrv.exe

