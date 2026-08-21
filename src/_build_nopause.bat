@echo off
rem Kept as an alias only. It used to carry its own cl.exe line that left
rem out d2detours_hook.cpp and /DEF:d2arch.def, so it deployed a DLL that
rem was not the one that ships. build.bat does not pause, so there is
rem nothing left for this script to do differently.
call "%~dp0build.bat" %*
