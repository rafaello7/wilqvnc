OBJS = cmdline.o vnclog.o sockstream.o vncconn.o vncdisplay.o wilqvnc.o

wilqvnc: $(OBJS)
	gcc $(OBJS) -o wilqvnc -lX11 -lXext


.c.o:
	gcc -g -c -Wall $<


clean:
	rm -f $(OBJS) wilqvnc

tar:
	cd .. && tar cf wilqvnc/wilqvnc.tar.gz  wilqvnc/*.[ch] wilqvnc/Makefile
