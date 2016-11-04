OBJS = cmdline.o vnclog.o sockstream.o cliconn.o clidisplay.o \
	   wilqvnc.o

wilqvnc: $(OBJS)
	gcc $(OBJS) -o wilqvnc -lX11 -lXext -lz

.c.o:
	gcc -g -c -Wall $<

$(OBJS): vnccommon.h

clean:
	rm -f $(OBJS) wilqvnc wilqvnc.tar.gz

tar:
	cd .. && tar cf wilqvnc/wilqvnc.tar.gz  wilqvnc/*.[ch] wilqvnc/Makefile
