/*
 * Copyright (c) 1995, 1996 Kungliga Tekniska H�gskolan (Royal Institute
 * of Technology, Stockholm, Sweden).
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
 *      This product includes software developed by the Kungliga Tekniska
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

#ifdef HAVE_CONFIG_H
#include <config.h>
RCSID("$Id$");
#endif
#include <stdio.h>
#include <string.h>
#include <siad.h>
#include <pwd.h>

#include <krb.h>

/* Is it necessary to have all functions? I think not. */

int 
siad_init(void)
{
    return SIADSUCCESS;
}

int 
siad_chk_invoker(void)
{
    return SIADFAIL;
}

int 
siad_ses_init(SIAENTITY *entity, int pkgind)
{
    entity->mech[pkgind] = (int*)malloc(MaxPathLen);
    if(entity->mech[pkgind] == NULL)
	return SIADFAIL;
    return SIADSUCCESS;
}

static int
setup_name(SIAENTITY *e, prompt_t *p)
{
    e->name = malloc(SIANAMEMIN+1);
    if(e->name == NULL)
	return SIADFAIL;
    p->prompt = (unsigned char*)"login: ";
    p->result = (unsigned char*)e->name;
    p->min_result_length = 1;
    p->max_result_length = SIANAMEMIN;
    p->control_flags = 0;
    return SIADSUCCESS;
}

static int
setup_password(SIAENTITY *e, prompt_t *p)
{
    e->password = malloc(SIAMXPASSWORD+1);
    if(e->password == NULL)
	return SIADFAIL;
    p->prompt = (unsigned char*)"Password: ";
    p->result = (unsigned char*)e->password;
    p->min_result_length = 0;
    p->max_result_length = SIAMXPASSWORD;
    p->control_flags = SIARESINVIS;
    return SIADSUCCESS;
}

int 
siad_ses_authent(sia_collect_func_t *collect, 
		 SIAENTITY *entity, 
		 int siastat,
		 int pkgind)
{
    prompt_t prompts[2], *pr;
    if((siastat == SIADSUCCESS) && (geteuid() == 0))
	return SIADSUCCESS;
    if(entity == NULL)
	return SIADFAIL | SIADSTOP;
    if((entity->acctname != NULL) || (entity->pwd != NULL))
	return SIADFAIL | SIADSTOP;
    
    if((collect != NULL) && entity->colinput) {
	int num;
	pr = prompts;
	if(entity->name == NULL){
	    if(setup_name(entity, pr) != SIADSUCCESS)
		return SIADFAIL;
	    pr++;
	}
	if(entity->password == NULL){
	    if(setup_password(entity, pr) != SIADSUCCESS)
		return SIADFAIL;
	    pr++;
	}
	num = pr - prompts;
	if(num == 1){
	    if((*collect)(240, SIAONELINER, (unsigned char*)"", num, 
			  prompts) != SIACOLSUCCESS)
		return SIADFAIL | SIADSTOP;
	} else if(num > 0){
	    if((*collect)(0, SIAFORM, (unsigned char*)"", num, 
			  prompts) != SIACOLSUCCESS)
		return SIADFAIL | SIADSTOP;
	}
    }
    
    if(entity->password == NULL || strlen(entity->password) > SIAMXPASSWORD)
	return SIADFAIL;
    if(entity->name[0] == 0)
	return SIADFAIL;
    
    {
	char realm[REALM_SZ];
	int ret;
	struct passwd pw;
	char pwbuf[1024];

	if(getpwnam_r(entity->name, &pw, pwbuf, sizeof(pwbuf)) < 0)
	    return SIADFAIL;
	sprintf((char*)entity->mech[pkgind], "%d%d_%d", 
		TKT_ROOT, pw.pw_uid, getpid());
	krb_set_tkt_string((char*)entity->mech[pkgind]);
	
	krb_get_lrealm(realm, 1);
	ret = krb_verify_user(entity->name, "", realm, 
			      entity->password, 1, NULL);
	if(ret){
	    if(ret != KDC_PR_UNKNOWN)
		/* since this is most likely a local user (such as
                   root), just silently return failure when the
                   principal doesn't exist */
		SIALOG("WARNING", "krb_verify_user(%s): %s", 
		       entity->name, krb_get_err_text(ret));
	    return SIADFAIL;
	}
	if(sia_make_entity_pwd(&pw, entity) == SIAFAIL)
	    return SIADFAIL;
    }
    return SIADSUCCESS;
}

int 
siad_ses_estab(sia_collect_func_t *collect, 
	       SIAENTITY *entity, int pkgind)
{
    return SIADFAIL;
}

int 
siad_ses_launch(sia_collect_func_t *collect,
		SIAENTITY *entity,
		int pkgind)
{
    char buf[MaxPathLen];
    chown((char*)entity->mech[pkgind],entity->pwd->pw_uid, entity->pwd->pw_gid);
    setenv("KRBTKFILE", (char*)entity->mech[pkgind]);
    return SIADSUCCESS;
}

int 
siad_ses_release(SIAENTITY *entity, int pkgind)
{
    if(entity->mech[pkgind])
	free(entity->mech[pkgind]);
    return SIADSUCCESS;
}

int 
siad_ses_suauthent(sia_collect_func_t *collect,
		   SIAENTITY *entity,
		   int siastat,
		   int pkgind)
{
    char name[ANAME_SZ];
    char toname[ANAME_SZ];
    char toinst[INST_SZ];
    char realm[REALM_SZ];
    struct passwd pw, topw;
    char pw_buf[1024], topw_buf[1024];
    
    if(geteuid() != 0)
	return SIADFAIL;
    if(siastat == SIADSUCCESS)
	return SIADSUCCESS;
    if(getpwuid_r(getuid(), &pw, pw_buf, sizeof(pw_buf)) < 0)
	return SIADFAIL;
    if(entity->name[0] == 0 || strcmp(entity->name, "root") == 0){
	strcpy(toname, pw.pw_name);
	strcpy(toinst, "root");
	if(getpwnam_r("root", &topw, topw_buf, sizeof(topw_buf)) < 0)
	    return SIADFAIL;
    }else{
	strcpy(toname, entity->name);
	toinst[0] = 0;
	if(getpwnam_r(entity->name, &topw, topw_buf, sizeof(topw_buf)) < 0)
	    return SIADFAIL;
    }
    if(krb_get_lrealm(realm, 1))
	return SIADFAIL;
    if(entity->password == NULL){
	prompt_t prompt;
	if(collect == NULL)
	    return SIADFAIL;
	setup_password(entity, &prompt);
	if((*collect)(0, SIAONELINER, (unsigned char*)"", 1, 
		      &prompt) != SIACOLSUCCESS)
	    return SIADFAIL;
    }
    if(entity->password == NULL)
	return SIADFAIL;
    {
	int ret;

	if(krb_kuserok(toname, toinst, realm, entity->name))
	    return SIADFAIL;
	
	sprintf((char*)entity->mech[pkgind], "/tmp/tkt_%s_to_%s_%d", 
		pw.pw_name, topw.pw_name, getpid());
	krb_set_tkt_string((char*)entity->mech[pkgind]);
	ret = krb_verify_user(toname, toinst, realm, entity->password, 1, NULL);
	if(ret){
	    SIALOG("WARNING", "krb_verify_user(%s.%s): %s", toname, toinst, 
		   krb_get_err_text(ret));
	    return SIADFAIL;
	}
    }
    if(sia_make_entity_pwd(&topw, entity) == SIAFAIL)
	return SIADFAIL;
    return SIADSUCCESS;
}


int 
siad_ses_reauthent(sia_collect_func_t *collect,
		   SIAENTITY *entity,
		   int siastat,
		   int pkgind)
{
    return SIADFAIL;
}


int 
siad_chg_finger(sia_collect_func_t *collect,
		const char *username, int argc, char *argv[])
{
    return SIADFAIL;
}


int 
siad_chg_password(sia_collect_func_t *collect,
		  const char *username, int argc, char *argv[])
{
    return SIADFAIL;
}


int 
siad_chg_shell(sia_collect_func_t *collect,
	       const char *username, int argc, char *argv[])
{
    return SIADFAIL;
}


int 
siad_getpwent(struct passwd *result, char *buf, int bufsize, FILE
	      **context)
{
    return SIADFAIL;
}


int 
siad_getpwuid(uid_t uid, struct passwd *result, char *buf, int bufsize)
{
    return SIADFAIL;
}


int 
siad_getpwnam(const char *name, struct passwd *result, char *buf,
	      int bufsize)
{
    return SIADFAIL;
}


int 
siad_setpwent(FILE **context)
{
    return SIADFAIL;
}


int 
siad_endpwent(FILE **context)
{
    return SIADFAIL;
}


int 
siad_getgrent(struct group *result, char *buf, int bufsize, FILE 
	      **context)
{
    return SIADFAIL;
}


int 
siad_getgrgid(gid_t gid, struct group *result, char *buf, int bufsize)
{
    return SIADFAIL;
}


int 
siad_getgrnam(const char *name, struct group *result, char *buf, 
	      int bufsize)
{
    return SIADFAIL;
}


int 
siad_setgrent(FILE **context)
{
    return SIADFAIL;
}


int 
siad_endgrent(FILE **context)
{
    return SIADFAIL;
}


int 
siad_chk_user(const char *logname, int checkflag)
{
    return SIADFAIL;
}
