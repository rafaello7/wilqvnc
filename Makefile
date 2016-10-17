CLI_OBJS = clicmdline.o vnclog.o sockstream.o cliconn.o clidisplay.o \
		   wilqvnc.o
SRV_OBJS = srvcmdline.o vnclog.o sockstream.o srvconn.o srvdisplay.o \
		   wilqvncsrv.o
CTL_OBJS = vnclog.o srvcmdline.o wilqvncctl.o


all:: wilqvncsrv wilqvnc wilqvncctl

wilqvnc: $(CLI_OBJS)
	gcc $(CLI_OBJS) -o wilqvnc -lX11 -lXext -llz4 -lzstd -lz

wilqvncsrv: $(SRV_OBJS)
	gcc $(SRV_OBJS) -o wilqvncsrv -lX11 -lXdamage -lXext -lXtst -lXfixes \
		-llz4 -lzstd -lz

wilqvncctl: $(CTL_OBJS)
	gcc $(CTL_OBJS) -o wilqvncctl

.c.o:
	gcc -g -c -Wall $<

$(SRV_OBJS) $(CLI_OBJS): vnccommon.h

clean:
	rm -f $(SRV_OBJS) $(CLI_OBJS) $(CTL_OBJS) wilqvnc wilqvncsrv wilqvncctl

tar:
	cd .. && tar cf wilqvnc/wilqvnc.tar.gz  wilqvnc/*.[ch] wilqvnc/Makefile
