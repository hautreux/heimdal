/*
 * Copyright (c) 1997 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. All advertising materials mentioning features or use of this software 
 *    must display the following acknowledgement: 
 *      This product includes software developed by Kungliga Tekniska 
 *      H�gskolan and its contributors. 
 *
 * 4. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "kdc_locl.h"
#include <getarg.h>
#include <parse_units.h>

RCSID("$Id$");

static char *config_file;
int require_preauth = -1;
char *keyfile;
static char *max_request_str;
size_t max_request;
time_t kdc_warn_pwexpire;
char *database;
char *port_str;
int enable_http = -1;

#ifdef KRB4
char *v4_realm;
#endif

static int help_flag;
static int version_flag;

static struct getargs args[] = {
    { 
	"config-file",	'c',	arg_string,	&config_file, 
	"location of config file",	"file" 
    },
    { 
	"require-preauth",	'p',	arg_negative_flag, &require_preauth, 
	"don't require pa-data in as-reqs"
    },
    { 
	"key-file",	'k',	arg_string, &keyfile, 
	"location of master key file", "file"
    },
    { 
	"max-request",	0,	arg_string, &max_request, 
	"max size for a kdc-request", "size"
    },
    {
	"database",	'd', 	arg_string, &database,
	"location of database", "database"
    },
    { "enable-http", 'H', arg_flag, &enable_http, "turn on HTTP support" },
#ifdef KRB4
    { 
	"v4-realm",	'r',	arg_string, &v4_realm, 
	"realm to serve v4-requests for"
    },
#endif
    {	"ports",	'P', 	arg_string, &port_str,
	"ports to listen to" 
    },
    {	"help",		'h',	arg_flag,   &help_flag },
    {	"version",	'v',	arg_flag,   &version_flag }
};

static int num_args = sizeof(args) / sizeof(args[0]);

struct units byte_units[] = {
    { "megabyte", 1024 * 1024 },
    { "mbyte", 1024 * 1024 },
    { "kilobyte", 1024 },
    { "kbyte", 1024 },
    { "byte", 1 },
    { NULL, 0 }
};

static void
usage(int ret)
{
    arg_printusage (args, num_args, "");
    exit (ret);
}

void
configure(int argc, char **argv)
{
    krb5_config_section *cf = NULL;
    int optind = 0;
    int e;
    const char *p;
    
    while((e = getarg(args, num_args, argc, argv, &optind)))
	warnx("error at argument `%s'", argv[optind]);

    if(help_flag)
	usage (0);

    if (version_flag)
	krb5_errx(context, 0, "%s", heimdal_version);

    argc -= optind;
    argv += optind;

    if (argc != 0)
	usage(1);
    
    if(config_file == NULL)
	config_file = HDB_DB_DIR "/kdc.conf";
    
    if(krb5_config_parse_file(config_file, &cf))
	goto end;
    
    if(keyfile == NULL){
	p = krb5_config_get_string (cf, 
				    "kdc",
				    "key-file",
				    NULL);
	if(p)
	    keyfile = strdup(p);
    }

    if(database == NULL){
	p = krb5_config_get_string (cf, "kdc", "database", NULL);
	if(p) database = strdup(p);
    }
    
    if(max_request_str){
	max_request = parse_units(max_request_str, byte_units, NULL);
    }

    if(max_request == 0){
	p = krb5_config_get_string (cf, 
				    "kdc",
				    "max-request",
				    NULL);
	if(p)
	    max_request = parse_units(max_request_str, byte_units, NULL);
    }
    
    if(require_preauth == -1)
	require_preauth = krb5_config_get_bool(cf, "kdc", 
					       "require-preauth", NULL);

    if(port_str == NULL){
	p = krb5_config_get_string(cf, "kdc", "ports", NULL);
	port_str = (char*)p;
    }
    if(enable_http == -1)
	enable_http = krb5_config_get_bool(cf, "kdc", "enable-http", NULL);
#ifdef KRB4
    if(v4_realm == NULL){
	p = krb5_config_get_string (cf, 
				    "kdc",
				    "v4-realm",
				    NULL);
	if(p)
	    v4_realm = strdup(p);
    }
#endif

    kdc_warn_pwexpire = krb5_config_get_time (cf,
					      "kdc",
					      "kdc_warn_pwexpire",
					      NULL);
end:
    kdc_openlog(cf);
    if(cf)
	krb5_config_file_free (cf);
    if(max_request == 0)
	max_request = 64 * 1024;
    if(require_preauth == -1)
	require_preauth = 1;
    if (port_str == NULL)
	port_str = "+";
#ifdef KRB4
    if(v4_realm == NULL){
	v4_realm = malloc(40); /* REALM_SZ */
	krb_get_lrealm(v4_realm, 1);
    }
#endif
}
