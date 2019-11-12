# this Makefile is to be used to build the mqtt_proto protocol adaptor .so
CXX:=g++

PKGS:= glib-2.0 

SRCS:=  nvds_mqtt_proto.cpp mqtt_client.cpp json_helper.cpp 
TARGET_LIB:= libnvds_mqtt_proto.so 

CFLAGS:= -fPIC -Wall

CFLAGS+= `pkg-config --cflags $(PKGS)`

LIBS:= `pkg-config --libs $(PKGS)`
LDFLAGS:= -shared

DS_INC:= ../../includes
RDPAHO_INC:=/usr/local/include

INC_PATHS:= -I $(DS_INC) -I $(RDPAHO_INC)
CFLAGS+= $(INC_PATHS)

LIBS+= -L../../lib -lrdpaho -ljansson -lnvds_logger
LDFLAGS+= -shared

all: $(TARGET_LIB)

$(TARGET_LIB) : $(SRCS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf $(TARGET_LIB)

