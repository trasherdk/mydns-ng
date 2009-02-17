/**************************************************************************************************
	$Id: rr.c,v 1.65 2005/04/29 16:10:27 bboy Exp $

	Copyright (C) 2002-2005  Don Moore <bboy@bboy.net>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at Your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**************************************************************************************************/

#include "named.h"

#include "memoryman.h"

#include "debug.h"
#include "rr.h"
#include "rrtype.h"
#include "taskobj.h"

#if DEBUG_ENABLED
int		debug_rr = 0;
#endif

#define __MYDNS_RR_NAME(__rrp)			((__rrp)->_name)
#define __MYDNS_RR_DATA(__rrp)			((__rrp)->_data)
#define __MYDNS_RR_DATA_LENGTH(__rrp)		((__rrp)->_data.len)
#define __MYDNS_RR_DATA_VALUE(__rrp)		((__rrp)->_data.value)
#define __MYDNS_RR_SRV_WEIGHT(__rrp)		((__rrp)->recData.srv.weight)
#define __MYDNS_RR_SRV_PORT(__rrp)		((__rrp)->recData.srv.port)
#define __MYDNS_RR_RP_TXT(__rrp)		((__rrp)->recData._rp_txt)
#define __MYDNS_RR_NAPTR_ORDER(__rrp)		((__rrp)->recData.naptr.order)
#define __MYDNS_RR_NAPTR_PREF(__rrp)		((__rrp)->recData.naptr.pref)
#define __MYDNS_RR_NAPTR_FLAGS(__rrp)		((__rrp)->recData.naptr.flags)
#define __MYDNS_RR_NAPTR_SERVICE(__rrp)		((__rrp)->recData.naptr._service)
#define __MYDNS_RR_NAPTR_REGEX(__rrp)		((__rrp)->recData.naptr._regex)
#define __MYDNS_RR_NAPTR_REPLACEMENT(__rrp)	((__rrp)->recData.naptr._replacement)

char *mydns_rr_table_name = NULL;
char *mydns_rr_where_clause = NULL;

size_t mydns_rr_data_length = DNS_DATALEN;

/* Optional columns */
int mydns_rr_extended_data = 0;
int mydns_rr_use_active = 0;
int mydns_rr_use_stamp = 0;
int mydns_rr_use_serial = 0;

char *mydns_rr_active_types[] = { (char*)"Y", (char*)"N", (char*)"D" };

void *
__mydns_rr_assert_pointer(void *ptr, const char *fieldname, const char *filename, int linenumber) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_assert_pointer() called for field=%s from %s:%d"),
	fieldname, filename, linenumber);
#endif
  if (ptr != NULL) return ptr;
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_PROGRESS, _("%s Pointer is NULL at %s:%d"),
	fieldname, filename, linenumber);
  abort();
#endif
  return ptr;
}

void
mydns_rr_get_active_types(SQL *sqlConn) {
  SQL_RES	*res;
  SQL_ROW	row;
  int 		querylen;
  char		*query;

  char		*YES = NULL;
  char		*NO = NULL;
  char		*DELETED = NULL;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_get_active_types: called"));
#endif
  querylen = sql_build_query(&query, "SELECT DISTINCT(active) FROM %s", mydns_rr_table_name);

  if (!(res = sql_query(sqlConn, query, querylen))) {
#if DEBUG_ENABLED
    Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_get_active_types: returns - query failed"));
#endif
    return;
  }

  RELEASE(query);

  while ((row = sql_getrow(res, NULL))) {
    char *VAL = row[0];
    if (   !strcasecmp(VAL, "yes")
	   || !strcasecmp(VAL, "y")
	   || !strcasecmp(VAL, "true")
	   || !strcasecmp(VAL, "t")
	   || !strcasecmp(VAL, "active")
	   || !strcasecmp(VAL, "a")
	   || !strcasecmp(VAL, "on")
	   || !strcmp(VAL, "1") ) { RELEASE(YES); YES = STRDUP(VAL); continue; }
    if (   !strcasecmp(VAL, "no")
	   || !strcasecmp(VAL, "n")
	   || !strcasecmp(VAL, "false")
	   || !strcasecmp(VAL, "f")
	   || !strcasecmp(VAL, "inactive")
	   || !strcasecmp(VAL, "i")
	   || !strcasecmp(VAL, "off")
	   || !strcmp(VAL, "0") ) { RELEASE(NO); NO = STRDUP(VAL); continue; }
    if (   !strcasecmp(VAL, "d")
	   || !strcasecmp(VAL, "deleted")
	   || !strcmp(VAL, "2") ) { RELEASE(DELETED); DELETED = STRDUP(VAL); continue; }
  }

  sql_free(res);

  if (!YES) YES = STRDUP("Y");
  if (!NO) NO = STRDUP("N");
  if (!DELETED) DELETED = STRDUP("D");

  mydns_rr_active_types[0] = YES;
  mydns_rr_active_types[1] = NO;
  mydns_rr_active_types[2] = DELETED;
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_get_active_types: returns - active = %s, inactive = %s, deleted = %s"),
	YES, NO, DELETED);
#endif
}

/**************************************************************************************************
	MYDNS_RR_COUNT
	Returns the number of records in the rr table.
**************************************************************************************************/
long
mydns_rr_count(SQL *sqlConn) {
  return sql_count(sqlConn, "SELECT COUNT(*) FROM %s", mydns_rr_table_name);
}
/*--- mydns_rr_count() --------------------------------------------------------------------------*/


/**************************************************************************************************
	MYDNS_SET_RR_TABLE_NAME
**************************************************************************************************/
void
mydns_set_rr_table_name(char *name) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_set_rr_table_name called with %s"), (name) ? name : "NULL");
#endif
  RELEASE(mydns_rr_table_name);
  if (!name)
    mydns_rr_table_name = STRDUP(MYDNS_RR_TABLE);
  else
    mydns_rr_table_name = STRDUP(name);
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_set_rr_table_name returns with %s"), mydns_rr_table_name);
#endif
}
/*--- mydns_set_rr_table_name() -----------------------------------------------------------------*/


/**************************************************************************************************
	MYDNS_SET_RR_WHERE_CLAUSE
**************************************************************************************************/
void
mydns_set_rr_where_clause(char *where) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_set_rr_where_name called with %s"), (where) ? where : "NULL");
#endif
  if (where && strlen(where)) {
    mydns_rr_where_clause = STRDUP(where);
  }
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_set_rr_where_name returns with %s"), mydns_rr_where_clause);
#endif
}
/*--- mydns_set_rr_where_clause() ---------------------------------------------------------------*/

/**************************************************************************************************
	__MYDNS_RR_PARSE_DEFAULT
	Default parser for any record
**************************************************************************************************/
int __mydns_rr_parse_default(const char *origin, MYDNS_RRP rr) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_PROGRESS, _("__mydns_rr_parse_default: called for %s"), origin);
#endif
  return 0;
}
/*--- __mydns_rr_parse_default() ------------------------------------------------------------------*/
/**************************************************************************************************
	__MYDNS_RR_PARSE_HOSTNAME_DATA
	Default parser for record where data is a hostname
**************************************************************************************************/
static inline int __mydns_rr_parse_hostname_data(const char *origin, MYDNS_RR *rr) {
  uint16_t datalen;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_PROGRESS, _("__mydns_rr_parse_hostname_data: called for %s"), origin);
#endif
  /* Append origin to data if it's not there for these types: */
  if (origin) {
    datalen = __MYDNS_RR_DATA_LENGTH(rr);
#ifdef DN_COLUMN_NAMES
    datalen += 1;
    __MYDNS_RR_DATA_LENGTH(rr) = datalen;
    __MYDNS_RR_DATA_VALUE(rr) = REALLOCATE(__MYDNS_RR_DATA_VALUE(rr), datalen+1, char*);
    /* Just append dot for DN */
    ((char*)__MYDNS_RR_DATA_VALUE(rr))[datalen-1] = '.';
#else
    if (datalen && ((char*)__MYDNS_RR_DATA_VALUE(rr))[datalen-1] != '.') {
      datalen = datalen + 1 + strlen(origin);
      __MYDNS_RR_DATA_VALUE(rr) = REALLOCATE(__MYDNS_RR_DATA_VALUE(rr), datalen+1, char*);
      ((char*)__MYDNS_RR_DATA_VALUE(rr))[__MYDNS_RR_DATA_LENGTH(rr)] = '.';
      memcpy(&((char*)__MYDNS_RR_DATA_VALUE(rr))[__MYDNS_RR_DATA_LENGTH(rr)+1], origin, strlen(origin));
      __MYDNS_RR_DATA_LENGTH(rr) = datalen;
    }
#endif
    ((char*)__MYDNS_RR_DATA_VALUE(rr))[__MYDNS_RR_DATA_LENGTH(rr)] = '\0';
  }
  return __mydns_rr_parse_default(origin, rr);
}

int __mydns_rr_parse_cname(const char *origin, MYDNS_RRP rr) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_PROGRESS, _("__mydns_rr_parse_cname: called for %s"), origin);
#endif
  return __mydns_rr_parse_hostname_data(origin, rr);
}

int __mydns_rr_parse_mx(const char *origin, MYDNS_RRP rr) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_PROGRESS, _("__mydns_rr_parse_mx: called for %s"), origin);
#endif
  return __mydns_rr_parse_hostname_data(origin, rr);
}

int __mydns_rr_parse_ns(const char *origin, MYDNS_RRP rr) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_PROGRESS, _("__mydns_rr_parse_ns: called for %s"), origin);
#endif
  return __mydns_rr_parse_hostname_data(origin, rr);
}

/*--- __mydns_rr_parse_cname_etc() ---------------------------------------------------------------*/
/**************************************************************************************************
	__MYDNS_RR_PARSE_RP
	RP contains two names in 'data' -- the mbox and the txt.
	NUL-terminate mbox and fill 'rp_txt' with the txt part of the record.
**************************************************************************************************/
int __mydns_rr_parse_rp(const char *origin, MYDNS_RRP rr) {
  char *c;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_PROGRESS, _("__mydns_rr_parse_rp: called for %s"), origin);
#endif
  /* If no space, set txt to '.' */
  if (!(c = strchr(__MYDNS_RR_DATA_VALUE(rr), ' '))) {
    __MYDNS_RR_RP_TXT(rr) = STRDUP(".");
  } else {
    int namelen = strlen(&c[1]);
    if (LASTCHAR(&c[1]) != '.') {
      namelen += strlen(origin) + 1;
    }
    __MYDNS_RR_RP_TXT(rr) = ALLOCATE_N(namelen + 1, sizeof(char), char*);
    strncpy(__MYDNS_RR_RP_TXT(rr), &c[1], namelen);
    if (LASTCHAR(__MYDNS_RR_RP_TXT(rr)) != '.') {
      strncat(__MYDNS_RR_RP_TXT(rr), ".", namelen);
      strncat(__MYDNS_RR_RP_TXT(rr), origin, namelen);
    }
    *c = '\0';
    __MYDNS_RR_DATA_VALUE(rr) = REALLOCATE(__MYDNS_RR_DATA_VALUE(rr),
					   strlen(__MYDNS_RR_DATA_VALUE(rr))+1,
					   char*);
    __MYDNS_RR_DATA_LENGTH(rr) = strlen(__MYDNS_RR_DATA_VALUE(rr));
  }
  return __mydns_rr_parse_default(origin, rr);
}
/*--- __mydns_rr_parse_rp() -----------------------------------------------------------------------*/
/**************************************************************************************************
	__MYDNS_RR_PARSE_SRV
	SRV records contain two unsigned 16-bit integers in the "data" field before the target,
	'srv_weight' and 'srv_port' - parse them and make "data" contain only the target.  Also, make
	sure 'aux' fits into 16 bits, clamping values above 65535.
**************************************************************************************************/
int __mydns_rr_parse_srv(const char *origin, MYDNS_RRP rr) {
  char *weight, *port, *target;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_PROGRESS, _("__mydns_rr_parse_srv: called for %s"), origin);
#endif
  /* Clamp 'aux' if necessary */
  if (rr->aux > 65535)
    rr->aux = 65535;

  /* Parse weight (into srv_weight), port (into srv_port), and target */
  target = __MYDNS_RR_DATA_VALUE(rr);
  if ((weight = strsep(&target, " \t"))) {
    uint32_t iweight = atoi(weight);
    if (iweight > 65535)
      iweight = 65535;
    __MYDNS_RR_SRV_WEIGHT(rr) = iweight;
    if ((port = strsep(&target, " \t"))) {
      uint32_t iport = atoi(port);
      if (iport > 65535)
	iport = 65535;
      __MYDNS_RR_SRV_PORT(rr) = iport;
    }
    /* Strip the leading data off and just hold target */
    memmove(__MYDNS_RR_DATA_VALUE(rr), target, strlen(target)+1);
    __MYDNS_RR_DATA_LENGTH(rr) = strlen(__MYDNS_RR_DATA_VALUE(rr));
    __MYDNS_RR_DATA_VALUE(rr) = REALLOCATE(__MYDNS_RR_DATA_VALUE(rr),
					   __MYDNS_RR_DATA_LENGTH(rr) + 1,
					   char*);
  }
  return __mydns_rr_parse_default(origin, rr);
}
/*--- __mydns_rr_parse_srv() ----------------------------------------------------------------------*/
/**************************************************************************************************
	__MYDNS_RR_PARSE_NAPTR
	Returns 0 on success, -1 on error.
**************************************************************************************************/
int __mydns_rr_parse_naptr(const char *origin, MYDNS_RRP rr) {
  char 		*int_tmp, *p;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_PROGRESS, _("__mydns_rr_parse_naptr: called for %s"), origin);
#endif
  p = __MYDNS_RR_DATA_VALUE(rr);

  if (!strsep_quotes2(&p, &int_tmp))
    return (-1);
  __MYDNS_RR_NAPTR_ORDER(rr) = atoi(int_tmp);
  RELEASE(int_tmp);

  if (!strsep_quotes2(&p, &int_tmp))
    return (-1);
  __MYDNS_RR_NAPTR_PREF(rr) = atoi(int_tmp);
  RELEASE(int_tmp);

  if (!strsep_quotes(&p, __MYDNS_RR_NAPTR_FLAGS(rr), sizeof(__MYDNS_RR_NAPTR_FLAGS(rr))))
    return (-1);

  if (!strsep_quotes2(&p, &__MYDNS_RR_NAPTR_SERVICE(rr)))
    return (-1);

  if (!strsep_quotes2(&p, &__MYDNS_RR_NAPTR_REGEX(rr)))
    return (-1);

  if (!strsep_quotes2(&p, &__MYDNS_RR_NAPTR_REPLACEMENT(rr)))
    return (-1);

  __MYDNS_RR_DATA_LENGTH(rr) = 0;
  RELEASE(__MYDNS_RR_DATA_VALUE(rr));

  return __mydns_rr_parse_default(origin, rr);
}
/*--- __mydns_rr_parse_naptr() --------------------------------------------------------------------*/
/**************************************************************************************************
	__MYDNS_RR_PARSE_TXT
	Returns 0 on success, -1 on error.
**************************************************************************************************/
int __mydns_rr_parse_txt(const char *origin, MYDNS_RRP rr) {
  int datalen = __MYDNS_RR_DATA_LENGTH(rr);
  char *data = __MYDNS_RR_DATA_VALUE(rr);

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_PROGRESS, _("__mydns_rr_parse_txt: called for %s"), origin);
#endif
  if (datalen > DNS_MAXTXTLEN) return (-1);

  while (datalen > 0) {
    size_t elemlen = strlen(data);
    if (elemlen > DNS_MAXTXTELEMLEN) return (-1);
    data = &data[elemlen+1];
    datalen -= elemlen + 1;
  }
  
  return __mydns_rr_parse_default(origin, rr);
}
/*--- __mydns_rr_parse_txt() ----------------------------------------------------------------------*/

static char *
__mydns_rr_append(char *s1, char *s2) {
  int s1len = strlen(s1);
  int s2len = strlen(s2);
  int newlen = s1len;
  char *name;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_append called with %s, %s"), s1, s2);
#endif
  if (s1len) newlen += 1;
  newlen += s2len;

  name = ALLOCATE(newlen+1, char*);
  if (s1len) { strncpy(name, s1, s1len); name[s1len] = '.'; s1len += 1; }
  strncpy(&name[s1len], s2, s2len);
  name[newlen] = '\0';
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_append returns %s"), name);
#endif
  return name;
}

char *
mydns_rr_append_origin(char *str, char *origin) {
  char *res;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_append_origin called with %s, %s"), str, origin);
#endif
  res = ((!*str || LASTCHAR(str) != '.')
	 ?__mydns_rr_append(str, origin)
	 :str);

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_append_origin returns %s"), res);
#endif
  return res;
}

void
mydns_rr_name_append_origin(MYDNS_RRP rr, char *origin) {
  char *res;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_name_append_origin called with %s"), origin);
#endif
  res = mydns_rr_append_origin(__MYDNS_RR_NAME(rr), origin);
  if (__MYDNS_RR_NAME(rr) != res) RELEASE(__MYDNS_RR_NAME(rr));
  __MYDNS_RR_NAME(rr) = res;
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_name_append_origin returns %s"), res);
#endif
}
      
void
mydns_rr_data_append_origin(MYDNS_RRP rr, char *origin) {
  char *res;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_data_append_origin called with %s"), origin);
#endif
  res = mydns_rr_append_origin(__MYDNS_RR_DATA_VALUE(rr), origin);
  if (__MYDNS_RR_DATA_VALUE(rr) != res) RELEASE(__MYDNS_RR_DATA_VALUE(rr));
  __MYDNS_RR_DATA_VALUE(rr) = res;
  __MYDNS_RR_DATA_LENGTH(rr) = strlen(__MYDNS_RR_DATA_VALUE(rr));
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_data_append_origin returns %s"), res);
#endif
}
      
/**************************************************************************************************
	_MYDNS_RR_FREE
	Frees the pointed-to structure.	Don't call this function directly, call the macro.
**************************************************************************************************/
void __mydns_rr_free_default(MYDNS_RRP rr) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_free_default called"));
#endif
}

void __mydns_rr_free_naptr(MYDNS_RRP rr) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_free_naptr called"));
#endif
  RELEASE(__MYDNS_RR_NAPTR_SERVICE(rr));
  RELEASE(__MYDNS_RR_NAPTR_REGEX(rr));
  RELEASE(__MYDNS_RR_NAPTR_REPLACEMENT(rr));
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_free_naptr returns"));
#endif
}

void __mydns_rr_free_rp(MYDNS_RRP rr) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_free_rp called"));
#endif
  RELEASE(__MYDNS_RR_RP_TXT(rr));
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_free_rp returns"));
#endif
}

void _mydns_rr_free(MYDNS_RRP first) {
  register MYDNS_RR *p, *tmp;
  register dns_qtype_map *map;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("_mydns_rr_free called"));
#endif
  for (p = first; p; p = tmp) {
    tmp = p->next;
    RELEASE(p->stamp);
    RELEASE(__MYDNS_RR_NAME(p));
    RELEASE(__MYDNS_RR_DATA_VALUE(p));
    map = mydns_rr_get_type_by_id(p->type);
    map->rr_free(p);
    RELEASE(p);
  }
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("_mydns_rr_free returns"));
#endif
}
/*--- _mydns_rr_free() --------------------------------------------------------------------------*/

MYDNS_RR *
mydns_rr_build(uint32_t id,
	       uint32_t zone,
	       dns_qtype_map *map,
	       dns_class_t class,
	       uint32_t aux,
	       uint32_t ttl,
	       char *active,
#if USE_PGSQL
	       timestamp *stamp,
#else
	       MYSQL_TIME *stamp,
#endif
	       uint32_t serial,
	       char *name,
	       char *data,
	       uint16_t	datalen,
	       const char *origin) {
  dns_qtype_t	type;
  MYDNS_RR	*rr = NULL;
  uint32_t	namelen;

  type = map->rr_type;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_build(): called for id=%d, zone=%d, type=%d, class=%d, aux=%d, "
			"ttl=%d, active='%s', stamp=%p, serial=%d, name='%s', data=%p, datalen=%d, origin='%s'"),
	 id, zone, type, class, aux, ttl, active, stamp, serial,
	 (name)?name:_("<NULL>"), data, datalen, origin);
#endif

  if ((namelen = (name)?strlen(name):0) > DNS_MAXNAMELEN) {
    /* Name exceeds permissable length - should report error */
    goto PARSEFAILED;
  }

  rr = (MYDNS_RR *)ALLOCATE(sizeof(MYDNS_RR), MYDNS_RR*);
  memset(rr, '\0', sizeof(MYDNS_RR));
  rr->next = NULL;

  rr->id = id;
  rr->zone = zone;

  __MYDNS_RR_NAME(rr) = ALLOCATE(namelen+1, char*);
  memset(__MYDNS_RR_NAME(rr), '\0', namelen+1);
  if (name) strncpy(__MYDNS_RR_NAME(rr), name, namelen);

  __MYDNS_RR_DATA_LENGTH(rr) = datalen;
  __MYDNS_RR_DATA_VALUE(rr) = ALLOCATE(datalen+1, char*);
  memcpy(__MYDNS_RR_DATA_VALUE(rr), data, datalen);

  rr->class = class;
  rr->aux = aux;
  rr->ttl = ttl;
  rr->type = type;
  if (rr->type == DNS_QTYPE_ALIAS) {
    rr->type = DNS_QTYPE_A;
    rr->alias = 1;
  } else
    rr->alias = 0;

  /* Find a constant value so we do not have to allocate or free this one */
  if (active) {
    int i;
    for (i = 0; i < 3; i++) {
      if (!strcasecmp(mydns_rr_active_types[i], active)) { active = mydns_rr_active_types[i]; break; }
    }
  }
  rr->active = active;
  rr->stamp = stamp;
  rr->serial = serial;

  if (map->rr_parser(origin, rr) < 0)
    goto PARSEFAILED;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_build(): returning result=%p"), rr);
#endif
  return (rr);

 PARSEFAILED:
  mydns_rr_free(rr);
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_build(): returning result=NULL"));
#endif
  return (NULL);
}

/**************************************************************************************************
	MYDNS_RR_PARSE
	Given the SQL results with RR data, populates and returns a matching MYDNS_RR structure.
	Returns NULL on error.
**************************************************************************************************/
inline MYDNS_RR *
mydns_rr_parse(SQL_ROW row, unsigned long *lengths, const char *origin) {
  dns_qtype_map *map;
  char		*active = NULL;
#if USE_PGSQL
  timestamp	*stamp = NULL;
#else
  MYSQL_TIME	*stamp = NULL;
#endif
  uint32_t	serial = 0;
  int		ridx = MYDNS_RR_NUMFIELDS;
  char		*data;
  uint16_t	datalen;
  MYDNS_RR	*rr;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_parse(): called for origin %s"), origin);
#endif

  if (!(map = mydns_rr_get_type_by_name(row[6]))) {
    /* Ignore unknown RR type(s) */
#if DEBUG_ENABLED
    Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_parse(): returns NULL unknown rr type %s"), row[6]);
#endif
    return (NULL);
  }

  data = row[3];
  datalen = lengths[3];
  if (mydns_rr_extended_data) {
    if (lengths[ridx]) {
      char *newdata = ALLOCATE(datalen + lengths[ridx], char*);
      memcpy(newdata, data, datalen);
      memcpy(&newdata[datalen], row[ridx], lengths[ridx]);
      datalen += lengths[ridx];
      data = newdata;
    }
    ridx++;
  }

  if (mydns_rr_use_active) active = row[ridx++];
  if (mydns_rr_use_stamp) {
#if USE_PGSQL
    stamp = row[ridx++];
#else
    stamp = (MYSQL_TIME*)ALLOCATE(sizeof(MYSQL_TIME), MYSQL_TIME*);
    memcpy(stamp, row[ridx++], sizeof(MYSQL_TIME));
#endif
  }
  if (mydns_rr_use_serial && row[ridx]) {
    serial = atou(row[ridx++]);
  }

  rr = mydns_rr_build(atou(row[0]),
		      atou(row[1]),
		      map,
		      DNS_CLASS_IN,
		      atou(row[4]),
		      atou(row[5]),
		      active,
		      stamp,
		      serial,
		      row[2],
		      data,
		      datalen,
		      origin);

  if (mydns_rr_extended_data && lengths[MYDNS_RR_NUMFIELDS]) RELEASE(data);

#if DEBUG_ENABLED
    Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_parse(): returns"));
#endif
  return (rr);
}
/*--- mydns_rr_parse() --------------------------------------------------------------------------*/


/**************************************************************************************************
	MYDNS_RR_DUP
	Make and return a copy of a MYDNS_RR record.  If 'recurse' is specified, copies all records
	in the RRset.
**************************************************************************************************/
void  __mydns_rr_duplicate_default(MYDNS_RRP dst, MYDNS_RRP src) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_duplicate_default called"));
#endif
}

void  __mydns_rr_duplicate_srv(MYDNS_RRP dst, MYDNS_RRP src) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_duplicate_srv called"));
#endif
  __MYDNS_RR_SRV_WEIGHT(dst) = __MYDNS_RR_SRV_WEIGHT(src);
  __MYDNS_RR_SRV_PORT(dst) = __MYDNS_RR_SRV_PORT(src);
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_duplicate_srv returns"));
#endif
}

void  __mydns_rr_duplicate_rp(MYDNS_RRP dst, MYDNS_RRP src) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_duplicate_rp called"));
#endif
  RELEASE(__MYDNS_RR_RP_TXT(dst));
  __MYDNS_RR_RP_TXT(dst) = STRDUP(__MYDNS_RR_RP_TXT(src));
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_duplicate_rp returns"));
#endif
}

void  __mydns_rr_duplicate_naptr(MYDNS_RRP dst, MYDNS_RRP src) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_duplicate_naptr called"));
#endif
  __MYDNS_RR_NAPTR_ORDER(dst) = __MYDNS_RR_NAPTR_ORDER(src);
  __MYDNS_RR_NAPTR_PREF(dst) = __MYDNS_RR_NAPTR_PREF(src);
  memcpy(__MYDNS_RR_NAPTR_FLAGS(dst), __MYDNS_RR_NAPTR_FLAGS(src), sizeof(__MYDNS_RR_NAPTR_FLAGS(dst)));
  RELEASE(__MYDNS_RR_NAPTR_SERVICE(dst));
  __MYDNS_RR_NAPTR_SERVICE(dst) = STRDUP(__MYDNS_RR_NAPTR_SERVICE(src));
  RELEASE(__MYDNS_RR_NAPTR_REGEX(dst));
  __MYDNS_RR_NAPTR_REGEX(dst) = STRDUP(__MYDNS_RR_NAPTR_REGEX(src));
  RELEASE(__MYDNS_RR_NAPTR_REPLACEMENT(dst));
  __MYDNS_RR_NAPTR_REPLACEMENT(dst) = STRDUP(__MYDNS_RR_NAPTR_REPLACEMENT(src));
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_duplicate_naptr returns"));
#endif
}

MYDNS_RR *
mydns_rr_dup(MYDNS_RR *start, int recurse) {
  register MYDNS_RR *first = NULL, *last = NULL, *rr, *s, *tmp;
  dns_qtype_map *map;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_dup called"));
#endif
  for (s = start; s; s = tmp) {
    tmp = s->next;

    rr = (MYDNS_RR *)ALLOCATE(sizeof(MYDNS_RR), MYDNS_RR*);

    memset(rr, '\0', sizeof(MYDNS_RR));
    rr->id = s->id;
    rr->zone = s->zone;
    __MYDNS_RR_NAME(rr) = STRDUP(__MYDNS_RR_NAME(s));
    rr->type = s->type;
    rr->class = s->class;
    __MYDNS_RR_DATA_LENGTH(rr) = __MYDNS_RR_DATA_LENGTH(s);
    __MYDNS_RR_DATA_VALUE(rr) = ALLOCATE(__MYDNS_RR_DATA_LENGTH(s)+1, char*);
    memcpy(__MYDNS_RR_DATA_VALUE(rr), __MYDNS_RR_DATA_VALUE(s), __MYDNS_RR_DATA_LENGTH(s));
    ((char*)__MYDNS_RR_DATA_VALUE(rr))[__MYDNS_RR_DATA_LENGTH(rr)] = '\0';
    rr->aux = s->aux;
    rr->ttl = s->ttl;
    rr->alias = s->alias;

    rr->active = s->active;
    if (s->stamp) {
#if USE_PGSQL
      rr->stamp = s->stamp;
#else
      rr->stamp = (MYSQL_TIME*)ALLOCATE(sizeof(MYSQL_TIME), MYSQL_TIME*);
      memcpy(rr->stamp, s->stamp, sizeof(MYSQL_TIME));
#endif
    } else
      rr->stamp = NULL;
    rr->serial = s->serial;

    map = mydns_rr_get_type_by_id(rr->type);

    map->rr_duplicator(rr, s);

    rr->next = NULL;
    if (recurse) {
      if (!first) first = rr;
      if (last) last->next = rr;
      last = rr;
    } else {
#if DEBUG_ENABLED
      Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_dup returns rr=%p"), rr);
#endif
      return (rr);
    }
  }
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_dup returns first=%p"), first);
#endif
  return (first);
}
/*--- mydns_rr_dup() ----------------------------------------------------------------------------*/


/**************************************************************************************************
	MYDNS_RR_SIZE
**************************************************************************************************/
size_t __mydns_rr_size_default(MYDNS_RRP rr) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_size_default called"));
#endif
  return 0;
}

size_t __mydns_rr_size_naptr(MYDNS_RRP rr) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_size_naptr called"));
#endif
  size_t size = strlen(__MYDNS_RR_NAPTR_SERVICE(rr)) + 1;
  size += strlen(__MYDNS_RR_NAPTR_REGEX(rr)) + 1;
  size += strlen(__MYDNS_RR_NAPTR_REPLACEMENT(rr)) + 1;
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_size_naptr returns"));
#endif
  return size;
}

size_t __mydns_rr_size_rp(MYDNS_RRP rr) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_size_rp called"));
#endif
  return strlen(__MYDNS_RR_RP_TXT(rr)) + 1;
}

size_t mydns_rr_size(MYDNS_RR *first) {
  register MYDNS_RR *p;
  register size_t size = 0;
  dns_qtype_map *map;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_size called"));
#endif
  for (p = first; p; p = p->next) {
    size += sizeof(MYDNS_RR)
      + (strlen(__MYDNS_RR_NAME(p)) + 1)
      + (__MYDNS_RR_DATA_LENGTH(p) + 1);
#if USE_PGSQL
#else
    size += sizeof(MYSQL_TIME);
#endif
    map = mydns_rr_get_type_by_id(p->type);
    size += map->rr_sizor(p);
  }    

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_size returns %d"), size);
#endif
  return (size);
}
/*--- mydns_rr_size() ---------------------------------------------------------------------------*/


/**************************************************************************************************
	MYDNS_RR_LOAD
	Returns 0 on success or nonzero if an error occurred.
	If "name" is NULL, all resource records for the zone will be loaded.
**************************************************************************************************/
char *
mydns_rr_columns() {
  char		*columns = NULL;
  size_t	columnslen = 0;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_columns called"));
#endif
  columnslen = sql_build_query(&columns, MYDNS_RR_FIELDS"%s%s%s%s",
			       /* Optional columns */
			       (mydns_rr_extended_data ? ",edata" : ""),
			       (mydns_rr_use_active ? ",active" : ""),
			       (mydns_rr_use_stamp  ? ",stamp"  : ""),
			       (mydns_rr_use_serial ? ",serial" : ""));
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_columns returns %s"), columns);
#endif
  return columns;
}

char *
mydns_rr_prepare_query(uint32_t zone, dns_qtype_t type, char *name, char *origin,
		       char *active, char *columns, char *filter) {
  size_t	querylen;
  char		*query = NULL;
  char		*namequery = NULL;
  char		*wheretype;
  char		*cp;
#ifdef DN_COLUMN_NAMES
  int		originlen = origin ? strlen(origin) : 0;
  int		namelen = name ? strlen(name) : 0;
#endif
  dns_qtype_map	*map;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_prepare_query called"));
#endif
  if (!(map = mydns_rr_get_type_by_id(type))) {
    errno = EINVAL;
#if DEBUG_ENABLED
    Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_prepare_query returns NULL as the type %d is not known"), type);
#endif
    return NULL;
  }

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_PROGRESS, _("mydns_rr_prepare_query(zone=%u, type='%s', name='%s', origin='%s')"),
	 zone, map->rr_type_name, name ?: _("NULL"), origin ?: _("NULL"));
#endif

  /* Make sure 'name' and 'origin' (if present) are valid */
  if (name) {
    for (cp = name; *cp; cp++)
      if (SQL_BADCHAR(*cp)) {
#if DEBUG_ENABLED
	Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_prepare_query returns NULL as the name contains a bad character %s(%c)"), name, *cp);
#endif
	return (NULL);
      }
  }
  if (origin) {
    for (cp = origin; *cp; cp++)
      if (SQL_BADCHAR(*cp)) {
#if DEBUG_ENABLED
	Debug(rr, DEBUGLEVEL_FUNCS, _("mydns_rr_prepare_query returns NULL as the origin contains a bad character %s(%c)"), origin, *cp);
#endif
	return (NULL);
      }
  }

#ifdef DN_COLUMN_NAMES
  /* Remove dot from origin and name for DN */
  if (originlen && origin[originlen - 1] == '.')
    origin[originlen-1] = '\0';
  else
    originlen = 0;

  if (name) {
    if (namelen && name[namelen - 1] == '.')
      name[namelen-1] = '\0';
    else
      namelen = 0;
  }
#endif

  /* Construct query */
  if (name) {
    if (origin) {
      if (!name[0])
	sql_build_query(&namequery, "(name='' OR name='%s')", origin);
      else {
#ifdef DN_COLUMN_NAMES
	sql_build_query(&namequery, "name='%s'", name);
#else
	sql_build_query(&namequery, "(name='%s' OR name='%s.%s')", name, name, origin);
#endif
      }
    }
    else
      sql_build_query(&namequery, "name='%s'", name);
  }

#ifdef DN_COLUMN_NAMES
  if (originlen)
    origin[originlen - 1] = '.';				/* Re-add dot to origin for DN */

  if (name) {
    if (namelen)
      name[namelen - 1] = '.';
  }
#endif

  if (map->rr_whereclause) {
    wheretype = STRDUP(map->rr_whereclause);
  } else {
    ASPRINTF(&wheretype, " AND type='%s'", map->rr_type_name);
  }

  querylen = sql_build_query(&query, "SELECT %s FROM %s WHERE "
#ifdef DN_COLUMN_NAMES
			     "zone_id=%u%s"
#else
			     "zone=%u%s"
#endif
			     "%s%s"
			     "%s%s%s"
			     "%s%s"
			     "%s%s"
			     "%s",

			     columns,

			     /* Fixed data */
			     mydns_rr_table_name,
			     zone, wheretype,

			     /* Name based query */
			     (namequery)? " AND " : "",
			     (namequery)? namequery : "",

			     /* Optional check for active value */
			     (mydns_rr_use_active)? " AND active='" : "",
			     (mydns_rr_use_active)? active : "",
			     (mydns_rr_use_active)? "'" : "",

			     /* Optional where clause for rr table */
			     (mydns_rr_where_clause)? " AND " : "",
			     (mydns_rr_where_clause)? mydns_rr_where_clause : "",

			     /* Apply additional filter if requested */
			     (filter)? " AND " : "",
			     (filter)? filter : "",

			     /* Optional sorting */
			     (mydns_rr_use_stamp)? " ORDER BY stamp DESC" : "");

  RELEASE(namequery);
  RELEASE(wheretype);

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_SQL, _("mydns_rr_prepare_query return %s"), query);
#endif
  return (query);
}
			 
static int __mydns_rr_do_load(SQL *sqlConn, MYDNS_RR **rptr, char *query, char *origin) {
  MYDNS_RR	*first = NULL, *last = NULL;
  char		*cp;
  SQL_RES	*res;
  SQL_ROW	row;
  unsigned long *lengths;


#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_do_load(query='%s', origin='%s')"), query, origin ? origin : _("NULL"));
#endif

  if (rptr) *rptr = NULL;

  /* Verify args */
  if (!sqlConn || !rptr || !query) {
    errno = EINVAL;
#if DEBUG_ENABLED
    Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_do_load returns -1 as %s"),
	  (!sqlConn) ? _("sql is not connected") 
	  : (!rptr) ? _("rrptr is NULL")
	  : _("query is NULL"));
#endif
    return (-1);
  }

  /* Submit query */
  if (!(res = sql_query(sqlConn, query, strlen(query)))) {
#if DEBUG_ENABLED
    Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_do_load returns -1 as the query failed"));
#endif
    return (-1);
  }

#if DEBUG_ENABLED
  {
    int numresults = sql_num_rows(res);

    Debug(rr, DEBUGLEVEL_PROGRESS, _("RR query: %d row%s: %s"), numresults, S(numresults), query);
  }
#endif

  RELEASE(query);

  /* Add results to list */
  while ((row = sql_getrow(res, &lengths))) {
    MYDNS_RR *new;

    if (!(new = mydns_rr_parse(row, lengths, origin)))
      continue;

    /* Always trim origin from name (XXX: Why? When did I add this?) */
    /* Apparently removing this code breaks RRs where the name IS the origin */
    /* But trim only where the name is exactly the origin */
    if (origin && (cp = strstr(__MYDNS_RR_NAME(new), origin)) && !(cp - __MYDNS_RR_NAME(new)))
      *cp = '\0';

    if (!first) first = new;
    if (last) last->next = new;
    last = new;
  }

  *rptr = first;
  sql_free(res);
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_do_load returns 0 (Success)"));
#endif
  return (0);
}

static int
__mydns_rr_count(SQL *sqlConn, uint32_t zone,
		 dns_qtype_t type, char *name, char *origin, char *active, char *filter) {
  char		*query = NULL;
  int		result;

  SQL_RES	*res;
  SQL_ROW	row;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_count called"));
#endif
  query = mydns_rr_prepare_query(zone, type, name, origin, active, (char*)"COUNT(*)", filter);

  if (!query || !(res = sql_query(sqlConn, query, strlen(query)))) {
    WarnSQL(sqlConn, _("error processing count with filter %s"), filter);
#if DEBUG_ENABLED
    Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_count returns -1 because %s"), (!query) ? _("query was NULL") : _("query failed"));
#endif
    return (-1);
  }

  RELEASE(query);

  if ((row = sql_getrow(res, NULL)))
    result = atoi(row[0]);
  else
    result = 0;

  sql_free(res);

#if DEBUG_ENABLED
    Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_count returns %d"), result);
#endif
  return result;
}

static int 
__mydns_rr_load(SQL *sqlConn, MYDNS_RR **rptr, uint32_t zone,
		dns_qtype_t type, char *name, char *origin, char *active, char *filter) {
  char		*query = NULL;
  int		res;
  char		*columns = NULL;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_load called"));
#endif
  columns = mydns_rr_columns();

  query = mydns_rr_prepare_query(zone, type, name, origin, active, columns, filter);

  RELEASE(columns);

  res = __mydns_rr_do_load(sqlConn, rptr, query, origin);

#if DEBUG_ENABLED
    Debug(rr, DEBUGLEVEL_FUNCS, _("__mydns_rr_load returns %d"), res);
#endif
  return res;
}

int
mydns_rr_load_all(SQL *sqlConn, MYDNS_RR **rptr, uint32_t zone,
		     dns_qtype_t type, char *name, char *origin) {

  return __mydns_rr_load(sqlConn, rptr, zone, type, name, origin, NULL, NULL);
}

int
mydns_rr_load_active(SQL *sqlConn, MYDNS_RR **rptr, uint32_t zone,
		     dns_qtype_t type, char *name, char *origin) {

  return __mydns_rr_load(sqlConn, rptr, zone, type, name, origin, mydns_rr_active_types[0], NULL);
}

int
mydns_rr_load_inactive(SQL *sqlConn, MYDNS_RR **rptr, uint32_t zone,
		       dns_qtype_t type, char *name, char *origin) {

  return __mydns_rr_load(sqlConn, rptr, zone, type, name, origin, mydns_rr_active_types[1], NULL);
}

int
mydns_rr_load_deleted(SQL *sqlConn, MYDNS_RR **rptr, uint32_t zone,
		      dns_qtype_t type, char *name, char *origin) {

  return __mydns_rr_load(sqlConn, rptr, zone, type, name, origin, mydns_rr_active_types[2], NULL);
}

int
mydns_rr_count_all(SQL *sqlConn, uint32_t zone,
		      dns_qtype_t type, char *name, char *origin) {

  return __mydns_rr_count(sqlConn, zone, type, name, origin, NULL, NULL);
}

int
mydns_rr_count_active(SQL *sqlConn, uint32_t zone,
		      dns_qtype_t type, char *name, char *origin) {

  return __mydns_rr_count(sqlConn, zone, type, name, origin, mydns_rr_active_types[0], NULL);
}

int
mydns_rr_count_inactive(SQL *sqlConn, uint32_t zone,
		      dns_qtype_t type, char *name, char *origin) {

  return __mydns_rr_count(sqlConn, zone, type, name, origin, mydns_rr_active_types[1], NULL);
}

int
mydns_rr_count_deleted(SQL *sqlConn, uint32_t zone,
		      dns_qtype_t type, char *name, char *origin) {

  return __mydns_rr_count(sqlConn, zone, type, name, origin, mydns_rr_active_types[2], NULL);
}


int
mydns_rr_load_all_filtered(SQL *sqlConn, MYDNS_RR **rptr, uint32_t zone,
			   dns_qtype_t type, char *name, char *origin, char *filter) {

  return __mydns_rr_load(sqlConn, rptr, zone, type, name, origin, NULL, filter);
}

int
mydns_rr_load_active_filtered(SQL *sqlConn, MYDNS_RR **rptr, uint32_t zone,
			      dns_qtype_t type, char *name, char *origin, char *filter) {

  return __mydns_rr_load(sqlConn, rptr, zone, type, name, origin, mydns_rr_active_types[0], filter);
}

int
mydns_rr_load_inactive_filtered(SQL *sqlConn, MYDNS_RR **rptr, uint32_t zone,
				dns_qtype_t type, char *name, char *origin, char *filter) {

  return __mydns_rr_load(sqlConn, rptr, zone, type, name, origin, mydns_rr_active_types[1], filter);
}

int
mydns_rr_load_deleted_filtered(SQL *sqlConn, MYDNS_RR **rptr, uint32_t zone,
			       dns_qtype_t type, char *name, char *origin, char *filter) {

  return __mydns_rr_load(sqlConn, rptr, zone, type, name, origin, mydns_rr_active_types[2], filter);
}

int
mydns_rr_count_all_filtered(SQL *sqlConn, uint32_t zone,
			    dns_qtype_t type, char *name, char *origin, char *filter) {

  return __mydns_rr_count(sqlConn, zone, type, name, origin, NULL, filter);
}

int
mydns_rr_count_active_filtered(SQL *sqlConn, uint32_t zone,
			       dns_qtype_t type, char *name, char *origin, char *filter) {

  return __mydns_rr_count(sqlConn, zone, type, name, origin, mydns_rr_active_types[0], filter);
}

int
mydns_rr_count_inactive_filtered(SQL *sqlConn, uint32_t zone,
				 dns_qtype_t type, char *name, char *origin, char *filter) {

  return __mydns_rr_count(sqlConn, zone, type, name, origin, mydns_rr_active_types[1], filter);
}

int
mydns_rr_count_deleted_filtered(SQL *sqlConn, uint32_t zone,
				dns_qtype_t type, char *name, char *origin, char *filter) {

  return __mydns_rr_count(sqlConn, zone, type, name, origin, mydns_rr_active_types[2], filter);
}

/*--- mydns_rr_load() ---------------------------------------------------------------------------*/

/**************************************************************************************************
	RRLIST_FREE
**************************************************************************************************/
void
rrlist_free(RRLIST *list) {
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("rrlist_free called"));
#endif
  if (list) {
    register RR *p, *tmp;

    for (p = list->head; p; p = tmp) {
      tmp = p->next;
      switch (p->rrtype) {
      case DNS_RRTYPE_SOA:
	mydns_soa_free(p->rr);
	break;
      case DNS_RRTYPE_RR:
	mydns_rr_free(p->rr);
	break;
      }
      RELEASE(p);
    }
    memset(list, 0, sizeof(RRLIST));
  }
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("rrlist_free returns"));
#endif
}
/*--- rrlist_free() -----------------------------------------------------------------------------*/


/**************************************************************************************************
	RRDUP
	Returns nonzero if the resource record specified is duplicated in the provided list.
**************************************************************************************************/
static int
rrdup(RRLIST *list, dns_rrtype_t rrtype, uint32_t id) {
  register RR *r = NULL;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("rrdup called"));
#endif
  if (list && id)						/* Ignore (fake) RRs with ID 0 */
    for (r = list->head; r; r = r->next)
      if (r->rrtype == rrtype && r->id == id) {
#if DEBUG_ENABLED
	Debug(rr, DEBUGLEVEL_FUNCS, _("rrdup returns 1"));
#endif
	return (1);
      }
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("rrlist_free returns 0"));
#endif
  return (0);
}
/*--- rrdup() -----------------------------------------------------------------------------------*/


/**************************************************************************************************
	RRLIST_ADD
	Adds a resource record to the section specified.
	The function will make a copy of `rr', so the caller should free their copy if desired.
**************************************************************************************************/
void
rrlist_add(
	TASK *t,					/* The task to add a record to */
	datasection_t ds,				/* Add `rr' to this data section */
	dns_rrtype_t rrtype,				/* The type of resource record being added */
	void *rr,					/* The resource record to add */
	char *name					/* Name to send with reply */
) {
  RRLIST *list = NULL;
  RR *new = NULL;
  uint32_t id = 0;
  register char *s = NULL, *d = NULL;

#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("rrlist_add called"));
#endif
  /* Remove erroneous empty labels in 'name' if any exist */
  if (name) {
    name = STRDUP(name); /* Might be read only */
    for (s = d = name; *s; s++)
      if (s[0] == '.' && s[1] == '.')
	*d++ = *s++;
      else
	*d++ = *s;
    *d = '\0';
  }

#if DN_COLUMN_NAMES
  if (rrtype == DNS_RRTYPE_RR && ds == ADDITIONAL) {
    MYDNS_RR *r = (MYDNS_RR *)rr;
    if (!strcmp(MYDNS_RR_NAME(r), "*")) {
      RELEASE(name);
#if DEBUG_ENABLED
      Debug(rr, DEBUGLEVEL_FUNCS, _("rrlist_add returns"));
#endif
      return;
    }
  }
#endif

#if DEBUG_ENABLED
  {
    switch (rrtype) {
    case DNS_RRTYPE_SOA:
      {
	MYDNS_SOA *soa = (MYDNS_SOA *)rr;
	Debug(rr, DEBUGLEVEL_PROGRESS, _("%s: RRLIST_ADD: %s (id=%u) (%s) (`%s')"), desctask(t),
	      mydns_section_str(ds), soa->id, soa->origin, name);
      }
      break;

    case DNS_RRTYPE_RR:
      {
	MYDNS_RR *r = (MYDNS_RR *)rr;
	Debug(rr, DEBUGLEVEL_PROGRESS, _("%s: RRLIST_ADD: %s (id=%u) (name='%s',qtype='%s',data='%s') (`%s')"),
	       desctask(t),
	      mydns_section_str(ds), r->id,
	       (char *)(strlen(MYDNS_RR_NAME(r)) ? MYDNS_RR_NAME(r) : (char *)""),
	       mydns_rr_get_type_by_id(r->type)->rr_type_name, (char*)MYDNS_RR_DATA_VALUE(r), name);
      }
      break;
    }
  }
#endif

  /* Check to make sure this isn't a duplicate */
  switch (rrtype) {
  case DNS_RRTYPE_SOA:		id = ((MYDNS_SOA *)rr)->id; break;
  case DNS_RRTYPE_RR:		id = ((MYDNS_RR *)rr)->id; break;
  }

  /* Check only the current section */ 
  switch (ds) {
  case QUESTION:
    break;

  case ANSWER:
    /* Suppress this check if the reply is an IXFR */
    list = &t->an;
    if (t->qtype == DNS_QTYPE_IXFR) break;
    if (rrdup(&t->an, rrtype, id)) {
#if DEBUG_ENABLED
      Debug(rr, DEBUGLEVEL_PROGRESS, _("%s: Duplicate record, ignored"), desctask(t));
#endif
      RELEASE(name);
      return;
    }
    break;

  case AUTHORITY:
    list = &t->ns;
    if (rrdup(&t->ns, rrtype, id) || rrdup(&t->an, rrtype, id)) {
#if DEBUG_ENABLED
      Debug(rr, DEBUGLEVEL_PROGRESS, _("%s: Duplicate record, ignored"), desctask(t));
#endif
      RELEASE(name);
      return;
    }
    break;

  case ADDITIONAL:
    list = &t->ar;
    if (rrdup(&t->ar, rrtype, id) || rrdup(&t->an, rrtype, id)) {
#if DEBUG_ENABLED
      Debug(rr, DEBUGLEVEL_PROGRESS, _("%s: Duplicate record, ignored"), desctask(t));
#endif
      RELEASE(name);
      return;
    }
    break;
  }

  new = ALLOCATE(sizeof(RR), RR*);
  new->rrtype = rrtype;
  switch (new->rrtype) {
  case DNS_RRTYPE_SOA:
    new->rr = mydns_soa_dup((MYDNS_SOA *)rr, 0);
    if (!ignore_minimum && (((MYDNS_SOA *)new->rr)->ttl < t->minimum_ttl))
      ((MYDNS_SOA *)new->rr)->ttl = t->minimum_ttl;
    break;

  case DNS_RRTYPE_RR:
    new->rr = mydns_rr_dup((MYDNS_RR *)rr, 0);
    /* Some RR types need to be flagged for sorting */
    switch (((MYDNS_RR *)rr)->type) {
    case DNS_QTYPE_A:
    case DNS_QTYPE_AAAA:
      list->a_records++;
      break;

    case DNS_QTYPE_MX:
      list->mx_records++;
      break;

    case DNS_QTYPE_SRV:
      list->srv_records++;
      break;

    default:
      break;
    }

    /* Keep track of the lowest TTL found (for cache) */
    if (!ignore_minimum && (((MYDNS_RR *)new->rr)->ttl < t->minimum_ttl))
      ((MYDNS_RR *)new->rr)->ttl = t->minimum_ttl;

    /* Don't cache this reply if the TTL for this record is 0 */
    if (((MYDNS_RR *)new->rr)->ttl == 0)
      t->reply_cache_ok = 0;

    break;
  }

  new->id = id;
  new->offset = 0;
  new->sort_level = t->sort_level;
  new->sort1 = 0;
  new->sort2 = 0;
  strncpy((char*)new->name, name, sizeof(new->name)-1);
  RELEASE(name);
  new->next = NULL;
  if (!list->head)
    list->head = list->tail = new;
  else {
    list->tail->next = new;
    list->tail = new;
  }
  list->size++;
#if DEBUG_ENABLED
  Debug(rr, DEBUGLEVEL_FUNCS, _("rrlist_add returns"));
#endif
}
/*--- rrlist_add() ------------------------------------------------------------------------------*/

/* vi:set ts=3: */
