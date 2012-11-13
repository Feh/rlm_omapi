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

#include <freeradius-devel/ident.h>
#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>

/*
 *	Define a structure for our module configuration.
 *
 *	These variables do not need to be in a structure, but it's
 *	a lot cleaner to do so, and a pointer to the structure can
 *	be used as the instance handle.
 */
typedef struct rlm_omapi_t {
} rlm_omapi_t;

/*
 *	A mapping of configuration file names to internal variables.
 *
 *	Note that the string is dynamically allocated, so it MUST
 *	be freed.  When the configuration file parse re-reads the string,
 *	it free's the old one, and strdup's the new one, placing the pointer
 *	to the strdup'd string into 'config.string'.  This gets around
 *	buffer over-flows.
 */
static const CONF_PARSER module_config[] = {
  { NULL, -1, 0, NULL, NULL }		/* end the list */
};

struct omapi_server {
	int port;
	char server[1024];
	char key[1024], key_name[1024], key_type[128];
	char user_ip[1024], user_mac[1024];
};

static int omapi_vp_getstring(VALUE_PAIR *check, const char *attr, char *buf, int len)
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
	vp = pairfind(check, dattr->attr);
	if(!vp) {
		radlog(L_INFO, "%s: %s not found!", lp, attr);
		return 0;
	}

	radlog(L_INFO, "%s: Found vp: %s = '%s'", lp, attr, vp->vp_strvalue);
	if(!vp->vp_strvalue) {
		radlog(L_ERR, "%s: attribute %s is not of type 'string'!", lp, attr);
		return 0;
	}

	return strlcpy(buf, vp->vp_strvalue, len);
}

static int omapi_add_dhcp_entry(struct omapi_server *s)
{
	return 1;
}

static int omapi_post_auth(void *instance, REQUEST *request)
{
	char port_str[8];
	struct omapi_server *s;

	s = rad_malloc(sizeof(*s));
	memset(s, 0, sizeof(s));
	strcpy(s->key_type, "hmac-md5"); /* hard-coded for now */

	/* quiet the compiler */
	instance = instance;
	request = request;

	/* gather information; sanity checks */

	VALUE_PAIR *rad_check = request->config_items;
	if(!omapi_vp_getstring(rad_check, "Zedat-Omapi-Host", s->server, sizeof(s->server)) ||
	   !omapi_vp_getstring(rad_check, "Zedat-Omapi-Port", port_str, sizeof(port_str)) ||
	   !omapi_vp_getstring(rad_check, "Zedat-Omapi-IP-Address", s->user_ip, sizeof(s->user_ip)) ||
	   !omapi_vp_getstring(rad_check, "Zedat-Omapi-Mac-Address", s->user_mac, sizeof(s->user_mac)) ||
	   !omapi_vp_getstring(rad_check, "Zedat-Omapi-Key", s->key, sizeof(s->key)) ||
	   !omapi_vp_getstring(rad_check, "Zedat-Omapi-Key-Name", s->key_type, sizeof(s->key_type))) {
		radlog(L_ERR, "rlm_omapi: MEEEEH");
		return RLM_MODULE_NOOP;
	}
	s->port = atoi(port_str);

	radlog(L_INFO, "rlm_omapi: trying to add mapping %s = %s to %s:%d",
			s->user_mac, s->user_ip, s->server, s->port);

	/* call OMAPI */
	if(!omapi_add_dhcp_entry(s)) {
		radlog(L_ERR, "rlm_omapi: Adding Host failed, returning noop");
		return RLM_MODULE_NOOP;
	}

	/* free objects */
	free(s);

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
	NULL,				/* instantiation */
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
