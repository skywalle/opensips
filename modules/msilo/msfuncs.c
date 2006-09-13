/*
 * $Id$
 *
 * Copyright (C) 2001-2003 FhG Fokus
 *
 * This file is part of openser, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * openser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "msfuncs.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include "../../dprint.h"
#include "../../config.h"
#include "../../ut.h"
#include "../../forward.h"
#include "../../resolve.h"
#include "../../globals.h"
#include "../../udp_server.h"
#include "../../pt.h"

#define CONTACT_PREFIX "Contact: <"
#define CONTACT_SUFFIX  ">;msilo=yes"CRLF
#define CONTACT_PREFIX_LEN (sizeof(CONTACT_PREFIX)-1)
#define CONTACT_SUFFIX_LEN  (sizeof(CONTACT_SUFFIX)-1)

extern int ms_add_date;

/**
 * apostrophes escaping
 * - src: source buffer
 * - slen: length of source buffer
 * - dst: destination buffer
 * - dlen: max length of destination buffer
 * #return: destination length => OK; -1 => error
 */

int m_apo_escape(char* src, int slen, char* dst, int dlen)
{
	int i, j;

	if(!src || !dst || dlen <= 0)
		return -1;

	if(slen == -1)
		slen = strlen(src);

	for(i=j=0; i<slen; i++)
	{
		switch(src[i])
		{
			case '\'':
					if(j+2>=dlen)
						return -2;
					memcpy(&dst[j], "\\'", 2);
					j += 2;
				break;
			default:
				if(j+1>=dlen)
						return -2;
				dst[j] = src[i];
				j++;
		}
	}
	dst[j] = '\0';

	return j;
}

/**
 * extract the value of Content-Type header
 * - src: pointer to C-T content
 * - len: length of src
 * - ctype: parsed C-T
 * - flag: what to parse - bit mask of CT_TYPE, CT_CHARSET, CT_MSGR
 *
 * #return: 0 OK ; -1 error
  */
int m_extract_content_type(char* src, int len, content_type_t* ctype, int flag)
{
	char *p, *end;
	int f = 0;

	if( !src || len <=0 )
		goto error;
	p = src;
	end = p + len;
	while((p < end) && (f != flag))
	{
		while((p < end) && (*p==' ' || *p=='\t'))
			p++;
		if(p >= end)
			goto done;
		if((flag & CT_TYPE) && !(f & CT_TYPE))
		{
			ctype->type.s = p;
			while(p < end && *p!=' ' && *p!='\t' && *p!='\0'
					 && *p!=';' && *p!='\r' && *p!='\n')
				p++;
			
			DBG("MSILO:m_extract_content_type: content-type found\n");
			f |= CT_TYPE;
			ctype->type.len = p - ctype->type.s;
			if(f == flag) {
				return 0;
			} else {
				p++;
				continue;
			}
		} else {
			if((flag & CT_CHARSET) && !(f & CT_CHARSET))
			{
				return -1;
			} else {
				if((flag & CT_MSGR) && !(f & CT_MSGR))
				{
					return -1;
				} else {
					return 0;
				}
			}
		}
	}

done:
	if(f==flag)
		return 0;
	else
		return -1;
error:
	DBG("MSILO:m_extract_content_type: error\n");
	return -1;
}

/** build MESSAGE headers 
 *
 * only Content-Type at this moment
 * expects - max buf len of the resulted body in body->len
 *         - body->s MUST be allocated
 * #return: 0 OK ; -1 error
 * */
int m_build_headers(str *buf, str ctype, str contact)
{
	char *p;
	if(!buf || !buf->s || buf->len <= 0 || ctype.len < 0 || contact.len < 0
			|| buf->len <= ctype.len+contact.len+14 /*Content-Type: */
				+CRLF_LEN+CONTACT_PREFIX_LEN+CONTACT_SUFFIX_LEN)
		goto error;

	p = buf->s;
	if(ctype.len > 0)
	{
		strncpy(p, "Content-Type: ", 14);
		p += 14;
		strncpy(p, ctype.s, ctype.len);
		p += ctype.len;
		strncpy(p, CRLF, CRLF_LEN);
		p += CRLF_LEN;
	
	}
	if(contact.len > 0)
	{
		strncpy(p, CONTACT_PREFIX, CONTACT_PREFIX_LEN);
		p += CONTACT_PREFIX_LEN;
		strncpy(p, contact.s, contact.len);
		p += contact.len;
		strncpy(p, CONTACT_SUFFIX, CONTACT_SUFFIX_LEN);
		p += CONTACT_SUFFIX_LEN;
	}
	buf->len = p - buf->s;	
	return 0;
error:
	return -1;
}

/** build MESSAGE body --- add incoming time and 'from' 
 *
 * expects - max buf len of the resulted body in body->len
 *         - body->s MUST be allocated
 * #return: 0 OK ; -1 error
 * */
int m_build_body(str *body, time_t date, str msg, time_t sdate)
{
	char *p;
	
	if(!body || !(body->s) || body->len <= 0 || msg.len <= 0
			|| date < 0 || msg.len < 0 || (46+msg.len > body->len) )
		goto error;
	
	p = body->s;

	if(ms_add_date!=0)
	{
		if(sdate!=0)
		{
			strncpy(p, "[Reminder message - ", 20);
			p += 20;
		
			strncpy(p, ctime(&sdate), 24);
			p += 24;

			*p++ = ']';
		} else {
			strncpy(p, "[Offline message - ", 19);
			p += 19;
	
			strncpy(p, ctime(&date), 24);
			p += 24;

			*p++ = ']';
		}
		*p++ = ' ';
	}
	
	strncpy(p, msg.s, msg.len);
	p += msg.len;

	body->len = p - body->s;
	
	return 0;
error:
	return -1;
}

/* return time stamp of YYYYMMDDHHMMSS */
int ms_extract_time(str *time_str, int *time_val)
{
	struct tm stm;
	int i;

	if(time_str==NULL || time_str->s==NULL  
			|| time_str->len<=0 || time_val==NULL)
	{
		LOG(L_ERR, "MSILO:m_extract_time: error - bad parameters\n");
		return -1;
	}
	
	memset(&stm, 0, sizeof(struct tm));
	for(i=0; i<time_str->len; i++)
	{
		if(time_str->s[i]<'0' || time_str->s[i]>'9')
		{
			LOG(L_ERR, "MSILO:m_extract_time: error - bad time [%.*s]\n",
					time_str->len, time_str->s);
			return -1;
		}
		switch(i)
		{
			case 0:
				if(time_str->s[i]<'2')
				{
					LOG(L_ERR,
						"MSILO:m_extract_time:error: bad year in time [%.*s]\n",
							time_str->len, time_str->s);
					return -1;
				}
				stm.tm_year += 1000*(time_str->s[i]-'0') - 1900;
			break;
			case 1:
				stm.tm_year += 100*(time_str->s[i]-'0');
			break;
			case 2:
				stm.tm_year += 10*(time_str->s[i]-'0');
			break;
			case 3:
				stm.tm_year += (time_str->s[i]-'0');
			break;
			case 4:
				if(time_str->s[i]>'1')
				{
					LOG(L_ERR,
						"MSILO:m_extract_time:error: bad month in time[%.*s]\n",
							time_str->len, time_str->s);
					return -1;
				}
				stm.tm_mon += 10*(time_str->s[i]-'0') - 1;
			break;
			case 5:
				if((time_str->s[i-1]=='0' && time_str->s[i]=='0')
						|| (time_str->s[i-1]=='1' && time_str->s[i]>'2'))
				{
					LOG(L_ERR,
						"MSILO:m_extract_time:error: bad month in time[%.*s]\n",
							time_str->len, time_str->s);
					return -1;
				}
				stm.tm_mon += (time_str->s[i]-'0');
			break;
			case 6:
				if(time_str->s[i]>'3')
				{
					LOG(L_ERR,
						"MSILO:m_extract_time:error: bad day in time [%.*s]\n",
							time_str->len, time_str->s);
					return -1;
				}
				stm.tm_mday += 10*(time_str->s[i]-'0');
			break;
			case 7:
				if((time_str->s[i-1]=='0' && time_str->s[i]=='0')
						|| (time_str->s[i-1]=='3' && time_str->s[i]>'1'))
				{
					LOG(L_ERR,
						"MSILO:m_extract_time:error: bad day in time [%.*s]\n",
							time_str->len, time_str->s);
					return -1;
				}
				stm.tm_mday += (time_str->s[i]-'0');
			break;
			case 8:
				if(time_str->s[i]>'2')
				{
					LOG(L_ERR,
						"MSILO:m_extract_time:error: bad hour in time [%.*s]\n",
							time_str->len, time_str->s);
					return -1;
				}
				stm.tm_hour += 10*(time_str->s[i]-'0');
			break;
			case 9:
				if(time_str->s[i-1]=='2' && time_str->s[i]>'3')
				{
					LOG(L_ERR,
						"MSILO:m_extract_time:error: bad hour in time [%.*s]\n",
							time_str->len, time_str->s);
					return -1;
				}
				stm.tm_hour += (time_str->s[i]-'0');
			break;
			case 10:
				if(time_str->s[i]>'5')
				{
					LOG(L_ERR,
						"MSILO:m_extract_time:error: bad min in time [%.*s]\n",
							time_str->len, time_str->s);
					return -1;
				}
				stm.tm_min += 10*(time_str->s[i]-'0');
			break;
			case 11:
				stm.tm_min += (time_str->s[i]-'0');
			break;
			case 12:
				if(time_str->s[i]>'5')
				{
					LOG(L_ERR,
						"MSILO:m_extract_time:error: bad sec in time [%.*s]\n",
							time_str->len, time_str->s);
					return -1;
				}
				stm.tm_sec += 10*(time_str->s[i]-'0');
			break;
			case 13:
				stm.tm_sec += (time_str->s[i]-'0');
			break;
			default:
				LOG(L_ERR,
					"MSILO:m_extract_time:error: time spec too long [%.*s]\n",
						time_str->len, time_str->s);
				return -1;
		}
	}
	*time_val = (int)mktime(&stm);

	return 0;
}
