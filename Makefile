test: a.out
	./a.out

a.out: test.c emulate.h aarch64.h ldst.c extr.c fma.c fms.c genlut.c mac16.c matfp.c matint.c vecfp.c vecint.c
	gcc -O2 -g test.c ldst.c extr.c fma.c fms.c genlut.c mac16.c matfp.c matint.c vecfp.c vecint.c

.PHONY: test
