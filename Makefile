V := 0

ifeq ($(V), 0)
	Q=@
else
	Q=
endif

.PHONY:all clean

all:
	make -C libzvbi
	make -C src

clean:
	$(Q)rm -f cc vbi
