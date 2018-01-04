.PHONY: all cc vbi clean

all: cc vbi
cc: dtvcc.c cc.c
	$(Q)gcc -g -O2 -o $@ $^ -I ../libzvbi/src/ -L ../libzvbi/src/.libs --static -lzvbi -lpthread
	$(Q)mv -v cc ../
vbi: vbi.c
	$(Q)gcc -g -O2 -o $@ $^ -I ../libzvbi/src/ -L ../libzvbi/src/.libs --static -lzvbi -lpthread
	$(Q)mv -v vbi ../

clean:
	$(Q)rm -rf cc vbi
