CC=m68k-atari-mint-gcc
CFLAGS=-fomit-frame-pointer -O2 --std=c99

EXEC=~/Atari/Disques/C/bin/editeur.tos 

$(EXEC):editeur.o vt52.o
	$(CC) -o $@ $^

clean:
	$(RM) $(EXEC) *.o *~
