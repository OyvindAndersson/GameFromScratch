@echo off

:: Make the build.bat dir the working dir
pushd %~dp0

mkdir ..\bin

pushd ..\bin
cl -FC -Zi ..\code\win32_gfs.cpp User32.lib Gdi32.lib

popd

popd