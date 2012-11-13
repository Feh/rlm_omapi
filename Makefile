FR_SOURCE = $(HOME)/src/freeradius
TARGET = rlm_omapi
SRCS = rlm_omapi.c

RLM_DIR = $(FR_SOURCE)/src/modules/rlm_example/
top_builddir = $(FR_SOURCE)/
include $(FR_SOURCE)/src/modules/rules.mak

RLM_CFLAGS =
RLM_LDFLAGS =
