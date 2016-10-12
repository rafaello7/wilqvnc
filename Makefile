CLI_OBJS = clicmdline.o vnclog.o sockstream.o clivncconn.o clidisplay.o wilqvnc.o
SRV_OBJS = vnclog.o sockstream.o srvvncconn.o srvdisplay.o wilqvncsrv.o

all:: wilqvncsrv wilqvnc

wilqvnc: $(CLI_OBJS)
	gcc $(CLI_OBJS) -o wilqvnc -lX11 -lXext

wilqvncsrv: $(SRV_OBJS)
	gcc $(SRV_OBJS) -o wilqvncsrv -lX11 -lXdamage -lXext -lXtst -lXfixes


.c.o:
	gcc -g -c -Wall $<

$(SRV_OBJS) $(CLI_OBJS): vnccommon.h

clean:
	rm -f $(SRV_OBJS) $(CLI_OBJS) wilqvnc wilqvncsrv

tar:
	cd .. && tar cf wilqvnc/wilqvnc.tar.gz  wilqvnc/*.[ch] wilqvnc/Makefile
