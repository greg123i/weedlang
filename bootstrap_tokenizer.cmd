@echo off
setlocal
"%~dp0cmake-build-debug\weedc.exe" --dump-tokens "%~1"
