@echo off
cd /d C:\Users\hernajic\Documents\Projects\gaggimate\web
call npm run build
xcopy /E /I /Y web\dist\*.* data\w
pio run -e display -t buildfs
pio run -e display
pio run -e display -t upload --upload-port COM4
