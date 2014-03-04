TARGET 	    := rlm_omapi.a
SOURCES     := rlm_omapi.c

pwd 	    := ${top_srcdir}/src/modules/rlm_omapi
SRC_CFLAGS  := -I${pwd}/isc-dhcp/includes -I${pwd}/isc-dhcp/bind/include -I${pwd}/isc-dhcp
TGT_LDFLAGS := -L${pwd}/isc-dhcp/bind/lib  -L${pwd}/isc-dhcp/dhcpctl -L${pwd}/isc-dhcp/omapip \
               -ldhcpctl -lisc -ldns -lomapi -ldns -lisc -lirs -lisccfg -lisc -ldns
	       # Yes, we actually have to list the libraries repeatedly and in this order.
	       # No, grouping them somehow does not work. No, I don't know why.
