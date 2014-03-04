/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Copyright 2000,2006  The FreeRADIUS server project
 * Copyright 2012  Julius Plenz, FU Berlin <plenz@cis.fu-berlin.de>
 */

#include <freeradius-devel/build.h>
#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>

#include <omapip/result.h>
#include <dhcpctl/dhcpctl.h>

#include <netinet/ether.h>

#define FAIL_CLEANUP(r) if(r != ISC_R_SUCCESS) { \
		radlog(L_ERR, "rlm_omapi: error: %s", isc_result_totext(res)); \
		ret = 0; \
		goto cleanup; \
	}

struct omapi_server {
	int port;
	char server[1024];
	char key[1024], key_name[1024], key_type[128];
	char user_ip[1024], user_mac[1024], user_host[1024];
};

static int omapi_instantiate(CONF_SECTION *conf, void *instance)
{
	isc_result_t res;

	/* quiet the compiler */
	conf = conf;
	instance = instance;

	if((res = dhcpctl_initialize()) != ISC_R_SUCCESS) {
		radlog(L_ERR, "rlm_omapi: failed to dhcpctl_initialize(): %s",
				isc_result_totext(res));
		return -1;
	}
	return 0;

}

static int omapi_vp_getstring(VALUE_PAIR *check, const char *attr, char *buf,
		int len, int notfounderr)
{
	char lp[] = "rlm_omapi: omapi_vp_getstring";
	VALUE_PAIR *vp;
	DICT_ATTR *dattr;

	DEBUG("%s: looking up attribute number for '%s'", lp, attr);
	dattr = dict_attrbyname(attr);
	if(!dattr) {
		radlog(L_ERR, "%s: No such attribute in dictionary: %s", lp, attr);
		return 0;
	}

	DEBUG("%s: looking up vp with attribute %s", lp, attr);
	vp = pairfind(check, dattr->attr, 0, TAG_ANY);
	if(!vp) {
		if(notfounderr)
			radlog(L_ERR, "%s: %s not found!", lp, attr);
		return 0;
	}

	DEBUG("%s: Found vp: %s = '%s'", lp, attr, vp->vp_strvalue);
	if(!vp->vp_strvalue) {
		radlog(L_ERR, "%s: attribute %s is not of type 'string'!", lp, attr);
		return 0;
	}

	return strlcpy(buf, vp->vp_strvalue, len);
}

static int omapi_add_dhcp_entry(const struct omapi_server *s)
{
	int ret = 1;
	isc_result_t res;
	dhcpctl_status waitstatus;

	struct ether_addr mac;

	char buf[1024];
	char lp[] = "rlm_omapi: omapi_add_dhcp_entry";

	dhcpctl_handle connection;
	dhcpctl_handle authenticator;
	dhcpctl_handle host;
	dhcpctl_data_string identifier;
	dhcpctl_data_string omapi_mac;
	dhcpctl_data_string omapi_ip;

	/* Create authenticator */
	DEBUG("%s: creating authenticator for %s:%d, key %s(%s)", lp,
			s->server, s->port, s->key_name, s->key_type);
	authenticator = dhcpctl_null_handle;
	res = dhcpctl_new_authenticator (&authenticator, s->key_name,
			s->key_type, (const unsigned char *)s->key,
			strlen(s->key) + 1);
	if(res != ISC_R_SUCCESS) {
		radlog(L_ERR, "%s: failed to create authenticator: %s", lp,
				isc_result_totext(res));
		return 0;
	}

	/* Set up connection */
	DEBUG("%s: connecting to %s:%d", lp, s->server, s->port);
	memset (&connection, 0, sizeof(connection));
	res = dhcpctl_connect(&connection, s->server, s->port, authenticator);
	if(res != ISC_R_SUCCESS) {
		radlog(L_ERR, "%s: failed to connect to %s:%d; error: %s", lp,
				s->server, s->port, isc_result_totext(res));
		return 0;
	}

	/* create a host object */
	DEBUG("%s: searching for host with mac address '%s'", lp, s->user_mac);
	memset (&host, 0, sizeof(host));
	res = dhcpctl_new_object(&host, connection, "host");
	if(res != ISC_R_SUCCESS) {
		radlog(L_ERR, "%s: Failed to create 'host' object: %s", lp,
				isc_result_totext(res));
		return 0;
	}

	/* store MAC address in omapi string and assign it to host object */
	memset (&omapi_mac, 0, sizeof(omapi_mac));
	res = omapi_data_string_new(&omapi_mac, sizeof(mac), MDL);
	FAIL_CLEANUP(res);
	memcpy(omapi_mac->value, ether_aton_r(s->user_mac, &mac), sizeof(mac));
	res = dhcpctl_set_value(host, omapi_mac, "hardware-address");
	FAIL_CLEANUP(res);

	/* query the server for an object matching this criterion */
	dhcpctl_open_object (host, connection, 0); /* 0 = just query information */
	waitstatus = dhcpctl_wait_for_completion(host, &res);

	if(res == ISC_R_SUCCESS) {
		memset (&identifier, 0, sizeof(identifier));
		res = dhcpctl_get_value(&identifier, host, "hardware-type");
		if(res == ISC_R_SUCCESS && identifier->value[3] != 0x01) {
			radlog(L_ERR, "%s: hardware address is not of type 1 (ethernet); aborting", lp);
			dhcpctl_data_string_dereference(&identifier, MDL);
		}
		dhcpctl_data_string_dereference(&identifier, MDL);

		memset (&identifier, 0, sizeof(identifier));
		res = dhcpctl_get_value(&identifier, host, "name");
		if(res == ISC_R_SUCCESS) {
			strncpy(buf, (char *) identifier->value, identifier->len);
			buf[identifier->len] = '\0';
			DEBUG("%s: MAC %s is present with hostname '%s'", lp,
					s->user_mac, buf);
		}
		dhcpctl_data_string_dereference(&identifier, MDL);

		memset (&identifier, 0, sizeof(identifier));
		res = dhcpctl_get_value(&identifier, host, "ip-address");
		if(res == ISC_R_SUCCESS) {
			inet_ntop(AF_INET, identifier->value, buf, sizeof(buf));
			dhcpctl_data_string_dereference(&identifier, MDL);
			DEBUG("%s: MAC %s is present with IP address '%s'", lp,
					s->user_mac, buf);
			if(!strncmp(s->user_ip, buf, strlen(s->user_ip))) {
				DEBUG("%s: MAC %s: oldip='%s', newip='%s', "
					"no update needed", lp, s->user_mac, s->user_ip, buf);
				ret = 0;
				goto cleanup;
			}

			/* IPs don't match. Delete the object from the server */
			dhcpctl_object_remove(connection, host);
			res = dhcpctl_wait_for_completion(host, &waitstatus);
			if(res == ISC_R_SUCCESS) {
				DEBUG("%s: Deleted the object on server", lp);
			} else {
				radlog(L_ERR, "%s: Failed to delete 'host' object: %s", lp,
					isc_result_totext(waitstatus == ISC_R_SUCCESS ? res : waitstatus));
				ret = 1;
				goto cleanup;
			}
		} else /* no success */
			dhcpctl_data_string_dereference(&identifier, MDL);
	} else if(waitstatus != ISC_R_SUCCESS) {
		DEBUG("%s: could not connect: %s", lp,
				isc_result_totext(waitstatus));
	} else {
		DEBUG("%s: no host with MAC address %s present", lp, s->user_mac);
	}

	/* in case we have no IP address, we have no hostname; so stop here */
	if(!strcmp(s->user_ip, "0.0.0.0")) {
		ret = 1;
		goto cleanup;
	}

	omapi_object_dereference(&host, MDL);

	/* remove old host entry, if present */
	DEBUG("%s: removing old entry for host %s...", lp, s->user_host);
	memset (&host, 0, sizeof(host));
	res = dhcpctl_new_object(&host, connection, "host");
	FAIL_CLEANUP(res);

	res = dhcpctl_set_string_value(host, s->user_host, "name");
	FAIL_CLEANUP(res);

	dhcpctl_open_object (host, connection, 0); /* 0 = just query information */
	waitstatus = dhcpctl_wait_for_completion(host, &res);
	if(res == ISC_R_SUCCESS) {
		dhcpctl_object_remove(connection, host);
		res = dhcpctl_wait_for_completion(host, &waitstatus);
		if(res == ISC_R_SUCCESS)
			DEBUG("%s: Deleted the object with hostname '%s' on server",
					lp, s->user_host);
	}
	/* omapi_mac will be used again, so it's dereferenced further down */
	omapi_object_dereference(&host, MDL);

	/* add new host entry in the final form */
	DEBUG("%s: adding new entry...", lp);
	memset (&host, 0, sizeof(host));
	res = dhcpctl_new_object(&host, connection, "host");
	FAIL_CLEANUP(res);

	memset(&omapi_ip, 0, sizeof(omapi_ip));
	res = omapi_data_string_new(&omapi_ip, sizeof(struct in_addr), MDL);
	inet_aton(s->user_ip, (struct in_addr *) &(omapi_ip->value));

	res = dhcpctl_set_string_value(host, s->user_host, "name");
	FAIL_CLEANUP(res);
	res = dhcpctl_set_value(host, omapi_mac, "hardware-address");
	FAIL_CLEANUP(res);
	res = dhcpctl_set_int_value(host, 1, "hardware-type");
	FAIL_CLEANUP(res);
	res = dhcpctl_set_value(host, omapi_ip, "ip-address");
	FAIL_CLEANUP(res);

	/* DHCPCTL_EXCL should be unnecessary here, but include it for safety */
	dhcpctl_open_object (host, connection, DHCPCTL_CREATE | DHCPCTL_EXCL);
	waitstatus = dhcpctl_wait_for_completion(host, &res);

	if(res == ISC_R_SUCCESS) {
		DEBUG("%s: successfully added mapping %s = %s = %s",
				lp, s->user_host, s->user_ip, s->user_mac);
	} else if(waitstatus == ISC_R_SUCCESS) {
		radlog(L_ERR, "%s: could not create new host: %s",
				lp, isc_result_totext(waitstatus));
	} else {
		radlog(L_ERR, "%s: connection failed: %s",
				lp, isc_result_totext(waitstatus));
	}

	dhcpctl_data_string_dereference(&omapi_ip, MDL);

cleanup:
	dhcpctl_data_string_dereference(&omapi_mac, MDL);
	omapi_object_dereference(&host, MDL);

	/* there is no dhcpctl_disconnect function. Instead, manually
	 * disconnect the OMAPI objects */
	omapi_disconnect(connection->outer->outer, 1);
	omapi_object_dereference(&connection, MDL);

	omapi_auth_key_dereference((struct auth_key *) &authenticator, MDL);

	return ret;
}

static rlm_rcode_t omapi_post_auth(void *instance, REQUEST *request)
{
	char port_str[8];
	struct omapi_server server;
	struct omapi_server *s = &server;

	memset(s, 0, sizeof(s));
	strcpy(s->key_type, "hmac-md5"); /* hard-coded for now */

	/* quiet the compiler */
	instance = instance;
	request = request;

	/* gather information; sanity checks */

	VALUE_PAIR *rad_check = request->config_items;

	/* sanity check: we wouldn't even know where to connect to; silently ignore */
	if(!omapi_vp_getstring(rad_check, "Zedat-Omapi-Host", s->server, sizeof(s->server), 0)) {
		DEBUG("rlm_omapi: No Zedat-Omapi-Host specified, fallthrough");
		return RLM_MODULE_NOOP;
	}

	if(!omapi_vp_getstring(rad_check, "Zedat-Omapi-Port", port_str, sizeof(port_str), 1) ||
	   !omapi_vp_getstring(rad_check, "Zedat-Omapi-User-IP", s->user_ip, sizeof(s->user_ip), 1) ||
	   !omapi_vp_getstring(rad_check, "Zedat-Omapi-User-Mac", s->user_mac, sizeof(s->user_mac), 1) ||
	   !omapi_vp_getstring(rad_check, "Zedat-Omapi-User-Host", s->user_host, sizeof(s->user_host), 1) ||
	   !omapi_vp_getstring(rad_check, "Zedat-Omapi-Key", s->key, sizeof(s->key), 1) ||
	   !omapi_vp_getstring(rad_check, "Zedat-Omapi-Key-Name", s->key_name, sizeof(s->key_name), 1)) {
		radlog(L_ERR, "rlm_omapi: At least one of the Zedat-Omapi-* keys are missing!");
		return RLM_MODULE_NOOP;
	}
	s->port = atoi(port_str);

	DEBUG("rlm_omapi: trying to add mapping %s = %s = %s to %s:%d",
			s->user_host, s->user_ip, s->user_mac, s->server, s->port);

	/* call OMAPI */
	if(!omapi_add_dhcp_entry(s)) {
		DEBUG("rlm_omapi: Host is up to date or an error occured, returning noop");
		return RLM_MODULE_NOOP;
	}

	return RLM_MODULE_OK;
}

/*
 *	The module name should be the only globally exported symbol.
 *	That is, everything else should be 'static'.
 *
 *	If the module needs to temporarily modify it's instantiation
 *	data, the type should be changed to RLM_TYPE_THREAD_UNSAFE.
 *	The server will then take care of ensuring that the module
 *	is single-threaded.
 */
module_t rlm_omapi = {
	RLM_MODULE_INIT,
	"omapi",
	RLM_TYPE_THREAD_UNSAFE,		/* type */
	0,				/* sizeof(rlm_omapi_t) */
	NULL,				/* module config struct */
	omapi_instantiate,		/* instantiation */
	NULL,				/* detach */
	{
		NULL,			/* authentication */
		NULL,			/* authorization */
		NULL,			/* preaccounting */
		NULL,			/* accounting */
		NULL,			/* checksimul */
		NULL,			/* pre-proxy */
		NULL,			/* post-proxy */
		omapi_post_auth		/* post-auth */
	},
};

/* vim: set noet ts=8 sw=8: */
