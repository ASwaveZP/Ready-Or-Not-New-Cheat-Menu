call "E:\Data\VS\Common7\Tools\VsDevCmd.bat" -arch=amd64
cmake -B build -G Ninja
cmake --build build --config Release