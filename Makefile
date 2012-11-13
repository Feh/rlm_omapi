FR_SOURCE = $(HOME)/src/freeradius
TARGET = rlm_omapi
SRCS = rlm_omapi.c

RLM_DIR = $(FR_SOURCE)/src/modules/rlm_example/
top_builddir = $(FR_SOURCE)/
include $(FR_SOURCE)/src/modules/rules.mak

RLM_CFLAGS = -I/server/dhcp/server/include -I/server/dhcp/src/isc-dhcp/bind/include
RLM_LDFLAGS = -L/server/dhcp/server/lib -ldhcpctl -lomapi -ldns -lisc
