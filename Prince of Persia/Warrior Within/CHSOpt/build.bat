call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86

cl /nologo /LD /MT /O1 /GS- CHSOpt.c /FeCHSOpt.asi /link /NODEFAULTLIB /SUBSYSTEM:WINDOWS /ENTRY:DllMain /BASE:0x11000000 /DYNAMICBASE:NO /ALIGN:512

PAUSE