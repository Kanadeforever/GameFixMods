call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86

:: BF → C
python convert_bf_to_c.py

:: build
cl /nologo /O2 /MT /GS- /c pop1_chs_compat.c
cl /nologo /O2 /MT /GS- /c popdata_bf.c
cl /nologo /O2 /Oi- /MT /GS- /c memcpy_stub.c

:: link
link /nologo /dll /machine:x86 /out:..\..\build\000_pop1_chs_compat.asi ^
  /nodefaultlib /entry:DllMain@12 /subsystem:windows /def:pop1_chs_compat.def ^
  pop1_chs_compat.obj popdata_bf.obj memcpy_stub.obj kernel32.lib
