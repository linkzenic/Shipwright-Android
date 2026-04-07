@echo off
echo Regenerating soh.o2r...
"C:\Program Files\Git\bin\bash.exe" -c "cd 'c:/Users/LENOVO/Documents/GitHub/Shipwright/soh' && rm -f soh.o2r && python 'c:/Users/LENOVO/Documents/GitHub/Shipwright/OTRExporter/extract_assets.py' -z 'c:/Users/LENOVO/Documents/GitHub/Shipwright/x64/Debug/ZAPD.exe' --norom --custom-otr-file soh.o2r '--custom-assets-path' 'c:/Users/LENOVO/Documents/GitHub/Shipwright/soh/assets/custom' --port-ver '9.1.2' && cp soh.o2r 'c:/Users/LENOVO/Documents/GitHub/Shipwright/x64/Release/soh.o2r' && echo 'Copied to x64/Release/' && echo 'Done!'"
pause
