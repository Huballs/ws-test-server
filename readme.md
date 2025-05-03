# windows msvc

## conan
```
conan install . --profile:a=msvcd  -s build_type=Debug --build=missing --output-folder=build
```

## msvcd profile:
```
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
```

# linux gcc

## conan
```
conan install . --profile:a=gccd  -s build_type=Debug --build=missing --output-folder=build
```

## gcc profile:
```
[settings]
arch=x86_64
build_type=Debug
compiler=gcc
#compiler.cppstd=gnu23
compiler.libcxx=libstdc++
compiler.version=14.2
os=Linux

[options]
CXX="g++-14"
CC="gcc-14"

[conf]
tools.system.package_manager:mode=install
```