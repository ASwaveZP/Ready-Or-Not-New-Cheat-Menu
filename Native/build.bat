rmdir /s /q build
call "E:\Data\VS\Common7\Tools\VsDevCmd.bat"
cmake -B build -G "Visual Studio 18 2026"
cmake --build build --config Release
