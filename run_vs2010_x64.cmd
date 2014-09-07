@echo off
call "C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.Cmd" /Release /x64 /xp
set GREENLET_STATIC_RUNTIME=1
%*
