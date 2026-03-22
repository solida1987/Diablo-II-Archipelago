@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cl /nologo /MT /O2 mpqfix.c /Fe:mpqfix.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib
del *.obj 2>/dev/null
echo Done.
pause
