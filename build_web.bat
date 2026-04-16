@echo off
cd /d C:UsershernajicDocumentsProjectsgaggimateweb
call npm run build
call pio run -e display -t buildfs
call pio run -e display
call pio run -e display -t upload --upload-port .COM4
