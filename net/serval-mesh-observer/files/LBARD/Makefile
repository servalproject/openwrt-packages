BINDIR=.
EXECS = $(BINDIR)/lbard $(BINDIR)/manifesttest $(BINDIR)/fakecsmaradio $(BINDIR)/fakeouternet

all:	$(EXECS)

test:	$(EXECS)
	tests/lbard

clean:
	rm -rf version.h $(EXECS) echotest

SRCDIR=src
INCLUDEDIR=include

MESSAGEHANDLERS=	$(SRCDIR)/messages/*.c
RADIODRIVERS=		$(SRCDIR)/drivers/drv_*.c
RADIOHEADERS=		$(SRCDIR)/drivers/drv_*.h

SRCS=	$(SRCDIR)/main.c \
	$(SRCDIR)/timeaccount.c \
	\
	$(SRCDIR)/succinct/stun.c \
	\
	$(SRCDIR)/rhizome/rhizome.c \
	$(SRCDIR)/rhizome/bundle_cache.c \
	$(SRCDIR)/rhizome/json.c \
	$(SRCDIR)/rhizome/peers.c \
	$(SRCDIR)/rhizome/rank.c \
	$(SRCDIR)/rhizome/bundles.c \
	$(SRCDIR)/rhizome/manifest_compress.c \
	$(SRCDIR)/rhizome/meshms.c \
	$(SRCDIR)/rhizome/otaupdate.c \
	\
	$(SRCDIR)/fec/golay.c \
	$(SRCDIR)/fec/fec-3.0.1/ccsds_tables.c \
	$(SRCDIR)/fec/fec-3.0.1/encode_rs_8.c \
	$(SRCDIR)/fec/fec-3.0.1/init_rs_char.c \
	$(SRCDIR)/fec/fec-3.0.1/decode_rs_8.c \
	\
	$(SRCDIR)/http/httpd.c \
	$(SRCDIR)/http/httpclient.c \
	\
	$(SRCDIR)/status/progress.c \
	$(SRCDIR)/status/monitor.c \
	$(SRCDIR)/status/status_dump.c \
	$(SRCDIR)/status/rssi.c \
	\
	$(SRCDIR)/energy_experiment.c \
	\
	$(SRCDIR)/crypto/sha1.c \
	$(SRCDIR)/crypto/sha3.c \
	\
	$(SRCDIR)/eeprom/eeprom.c \
	\
	$(SRCDIR)/util.c \
	$(SRCDIR)/code_instrumentation.c \
	\
	$(SRCDIR)/xfer/progress_bitmaps.c \
	$(SRCDIR)/xfer/txmessages.c \
	$(SRCDIR)/xfer/rxmessages.c \
	$(SRCDIR)/xfer/serial.c \
	$(SRCDIR)/xfer/radio.c \
	$(SRCDIR)/xfer/partials.c \
	\
	$(SRCDIR)/sync/bundle_tree.c \
	$(SRCDIR)/sync/sync.c \
	\
	$(SRCDIR)/xfer/radio_types.c \
	$(RADIODRIVERS) \
	\
	$(SRCDIR)/hf/ale.c \
	$(SRCDIR)/hf/config.c \
	\
	$(SRCDIR)/outernetrx/outernetrx.c \
	\
	$(SRCDIR)/xfer/message_handlers.c \
	$(MESSAGEHANDLERS) \


HDRS=	$(INCLUDEDIR)/lbard.h \
	$(INCLUDEDIR)/serial.h \
	Makefile \
	$(INCLUDEDIR)/sync.h \
	$(INCLUDEDIR)/sha3.h \
	$(INCLUDEDIR)/util.h \
	$(INCLUDEDIR)/radios.h \
	$(INCLUDEDIR)/radio_type.h \
	$(RADIOHEADERS) \
	$(SRCDIR)/eeprom/miniz.c \
	$(INCLUDEDIR)/message_handlers.h

#CC=/usr/local/Cellar/llvm/3.6.2/bin/clang
#LDFLAGS= -lgmalloc
#CFLAGS= -fno-omit-frame-pointer -fsanitize=address
#CC=clang
#LDFLAGS= -lefence
LDFLAGS=
# -I$(SRCDIR) is required for fec-3.0.1
CFLAGS= -g -std=gnu99 -Wall -fno-omit-frame-pointer -D_GNU_SOURCE=1 -I$(INCLUDEDIR) -I$(SRCDIR)/fec -I$(SRCDIR)

$(INCLUDEDIR)/version.h:	$(SRCS) $(HDRS)
	echo "#define VERSION_STRING \""`./md5 $(SRCS)`"\"" >$(INCLUDEDIR)/version.h
	echo "#define GIT_VERSION_STRING \""`git describe --always --abbrev=10 --dirty=+DIRTY`"\"" >>$(INCLUDEDIR)/version.h
	echo "#define GIT_BRANCH \""`git rev-parse --abbrev-ref HEAD`"\"" >>$(INCLUDEDIR)/version.h
	echo "#define BUILD_DATE \""`date`"\"" >>$(INCLUDEDIR)/version.h

lbard:	$(SRCS) $(HDRS) $(INCLUDEDIR)/version.h
	$(CC) $(CFLAGS) -o lbard $(SRCS) $(LDFLAGS)

echotest:	Makefile echotest.c
	$(CC) $(CFLAGS) -o echotest echotest.c

FAKERADIOSRCS=	$(SRCDIR)/fakeradio/fakecsmaradio.c \
		$(SRCDIR)/drivers/fake_*.c \
		\
		$(SRCDIR)/fec/fec-3.0.1/ccsds_tables.c \
		$(SRCDIR)/fec/fec-3.0.1/encode_rs_8.c \
		$(SRCDIR)/fec/fec-3.0.1/init_rs_char.c \
		$(SRCDIR)/fec/fec-3.0.1/decode_rs_8.c
fakecsmaradio:	\
	Makefile $(FAKERADIOSRCS) $(INCLUDEDIR)/fakecsmaradio.h
	$(CC) $(CFLAGS) -o fakecsmaradio $(FAKERADIOSRCS)

FAKEOUTERNETSRCS=	$(SRCDIR)/fakeradio/fakeouternet.c \
			$(SRCDIR)/code_instrumentation.c
fakeouternet:	\
	Makefile $(FAKEOUTERNETSRCS) $(INCLUDEDIR)/code_instrumentation.h
	$(CC) $(CFLAGS) -o fakeouternet $(FAKEOUTERNETSRCS)

$(BINDIR)/manifesttest:	Makefile $(SRCDIR)/rhizome/manifest_compress.c $(SRCDIR)/util.c $(SRCDIR)/code_instrumentation.c
	$(CC) $(CFLAGS) -DTEST -o $(BINDIR)/manifesttest $(SRCDIR)/rhizome/manifest_compress.c $(SRCDIR)/util.c $(SRCDIR)/code_instrumentation.c

$(INCLUDEDIR)/radios.h:	$(RADIODRIVERS) Makefile
	echo "Radio driver files: $(RADIODRIVERS)"
	echo '#include "radio_type.h"' > $(INCLUDEDIR)/radios.h
	echo "" >> $(INCLUDEDIR)/radios.h
	echo "#define RADIOTYPE_MIN 0" >> $(INCLUDEDIR)/radios.h
	grep "^RADIO TYPE:" src/drivers/*.c | cut -f3 -d: | cut -f1 -d, | awk '{ printf("#define RADIOTYPE_%s %d\n",$$1,n); n++; } END { printf("#define RADIOTYPE_MAX %d\n",n-1); }' >> $(INCLUDEDIR)/radios.h
	echo "" >> $(INCLUDEDIR)/radios.h
	for fn in `(cd $(SRCDIR); echo drivers/drv_*.h)`; do echo "#include \"$$fn\""; done >> $(INCLUDEDIR)/radios.h
	echo "" >> $(INCLUDEDIR)/radios.h

$(SRCDIR)/xfer/radio_types.c:	$(RADIODRIVERS) Makefile
	echo '#include <stdio.h>' > $(SRCDIR)/xfer/radio_types.c
	echo '#include <fcntl.h>' >> $(SRCDIR)/xfer/radio_types.c
	echo '#include <sys/uio.h>' >> $(SRCDIR)/xfer/radio_types.c
	echo '#include <sys/socket.h>' >> $(SRCDIR)/xfer/radio_types.c
	echo '#include <time.h>' >> $(SRCDIR)/xfer/radio_types.c
	echo '#include "sync.h"' >> $(SRCDIR)/xfer/radio_types.c
	echo '#include "lbard.h"' >> $(SRCDIR)/xfer/radio_types.c
	echo '#include "hf.h"' >> $(SRCDIR)/xfer/radio_types.c
	echo '#include "radios.h"' >> $(SRCDIR)/xfer/radio_types.c
	echo '' >> $(SRCDIR)/xfer/radio_types.c
	echo "radio_type radio_types[]={" >> $(SRCDIR)/xfer/radio_types.c
	grep "^RADIO TYPE:" $(RADIODRIVERS) | cut -f3- -d: | sed -e 's/^ /  {RADIOTYPE_/' -e 's/$$/\},/' >> $(SRCDIR)/xfer/radio_types.c
	echo "  {-1,NULL,NULL,NULL,NULL,NULL,NULL,NULL,-1}" >> $(SRCDIR)/xfer/radio_types.c
	echo "};" >> $(SRCDIR)/xfer/radio_types.c

$(SRCDIR)/xfer/message_handlers.c:	$(MESSAGEHANDLERS) Makefile gen_msghandler_list
	echo '#include <stdio.h>' > $(SRCDIR)/xfer/message_handlers.c
	echo '#include <fcntl.h>' >> $(SRCDIR)/xfer/message_handlers.c
	echo '#include <sys/uio.h>' >> $(SRCDIR)/xfer/message_handlers.c
	echo '#include <sys/socket.h>' >> $(SRCDIR)/xfer/message_handlers.c
	echo '#include <time.h>' >> $(SRCDIR)/xfer/message_handlers.c
	echo '#include "sync.h"' >> $(SRCDIR)/xfer/message_handlers.c
	echo '#include "lbard.h"' >> $(SRCDIR)/xfer/message_handlers.c
	echo '#include "hf.h"' >> $(SRCDIR)/xfer/message_handlers.c
	echo '#include "radios.h"' >> $(SRCDIR)/xfer/message_handlers.c
	echo '#include "message_handlers.h"' >> $(SRCDIR)/xfer/message_handlers.c
	echo '' >> $(SRCDIR)/xfer/message_handlers.c
	echo "message_handler message_handlers[257]={" >> $(SRCDIR)/xfer/message_handlers.c
	./gen_msghandler_list >> $(SRCDIR)/xfer/message_handlers.c
	echo 'NULL};' >> $(SRCDIR)/xfer/message_handlers.c

$(INCLUDEDIR)/message_handlers.h:	$(MESSAGEHANDLERS) Makefile
	cat $(SRCDIR)/messages/*.c | grep message_parser_ | cut -f2 -d" " | cut -f1 -d"(" | awk '{ printf("int %s(struct peer_state *,char *, char *, char *,unsigned char *,int);\n",$$1); }' >$(INCLUDEDIR)/message_handlers.h
	cat $(SRCDIR)/messages/*.c | grep "#define message_parser_" >>$(INCLUDEDIR)/message_handlers.h

