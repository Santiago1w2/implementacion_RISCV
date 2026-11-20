.option norvc
.option norelax
.global _start
_start:
    addi t0, zero, 5
    addi t1, zero, 3
    add a0, t0, t1
    lui t2, 0x1
    sw a0, 0(t2)
    ebreak
  