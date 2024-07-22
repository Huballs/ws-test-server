conan command:
conan install . --profile:a=msvcd  -s build_type=Debug --build=missing --output-folder=build

msvcd profile:

[settings]
os=Windows
arch=x86_64
compiler=msvc
compiler.version=193
compiler.cppstd=23
compiler.runtime=dynamic
build_type=Debug
[options]

CXX="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.37.32822\bin\Hostx64\x64\cl.exe"
CC="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.37.32822\bin\Hostx64\x64\cl.exe"

VS160COMNTOOLS="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools" 