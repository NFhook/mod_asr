SER_CC_PRE=
#CLI_CC_PRE=ppc_4xx-

SER_CC=$(SER_CC_PRE)g++
CLI_CC=$(CLI_CC_PRE)g++
SER_STRIP=$(SER_CC_PRE)strip
CLI_STRIP=$(CLI_CC_PRE)strip


CFLAGS += -shared -fPIC -O2 -Wall
SER_SRCS=mod_asr.cpp

SDK_LIBS=-L./NlsSdkCpp2.0/lib/linux -lpthread  -lnlsCppSdk -lnlsCommonSdk -ljsoncpp -luuid -lopus -ldl -D_GLIBCXX_USE_CXX11_ABI=0
SDK_INCLUDE=-I./NlsSdkCpp2.0/include

FREESWITCH_LIBS=-L/usr/local/freeswitch/lib -lfreeswitch
FREESWITCH_INCLUDE=-I/usr/local/freeswitch/include/freeswitch

TARGET_SER=mod_asr.so

SER_OBJS=$(SER_SRCS:.c=.o)

default: $(TARGET_SER)

$(TARGET_SER): $(SER_OBJS) $(HEADERS)
	$(SER_CC) $(CFLAGS) -o $(TARGET_SER) $(SER_OBJS) $(SDK_INCLUDE) $(SDK_LIBS) $(FREESWITCH_INCLUDE) $(FREESWITCH_LIBS)
#	$(SER_STRIP) $(TARGET_SER)

#$(SER_OBJS):%.o:%.c
#	$(SER_CC) $(SDK_INCLUDE) $(FREESWITCH_INCLUDE) $(CFLAGS) -c $< -o $@

install:
	echo "nothing to do"
clean:
	rm -rf  $(TARGET_SER)

