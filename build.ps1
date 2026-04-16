Set-Location C:\Users\hernajic\Documents\Projects\gaggimate\web
npm run build
Copy-Item -Recurse -Force C:\Users\hernajic\Documents\Projects\gaggimate\web\dist\* C:\Users\hernajic\Documents\Projects\gaggimate\data\w\
pio run -e display -t buildfs
pio run -e display
pio run -e display -t upload -d C:\Users\hernajic\Documents\Projects\gaggimate --upload-port COM4
