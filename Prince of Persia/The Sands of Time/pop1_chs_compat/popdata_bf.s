        .section .rdata,"dr"
        .globl _g_popdata_chs_start
_g_popdata_chs_start:
        .incbin "POPData_chs.BF"
        .globl _g_popdata_chs_end
_g_popdata_chs_end:
        .byte 0
        .globl _g_popdata_mixed_start
_g_popdata_mixed_start:
        .incbin "POPData_mixed_gamepad.BF"
        .globl _g_popdata_mixed_end
_g_popdata_mixed_end:
        .byte 0
