/*
 * Purple's oscar protocol plugin
 * This file is the legal property of its developers.
 * Please see the AUTHORS file distributed alongside this file.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
 * AIM Callback Types
 *
 */
#ifndef _SNACTYPES_H_
#define _SNACTYPES_H_

/*
 * SNAC Families.
 */
#define SNAC_FAMILY_OSERVICE   0x0001
#define SNAC_FAMILY_LOCATE     0x0002
#define SNAC_FAMILY_BUDDY      0x0003
#define SNAC_FAMILY_ICBM       0x0004
#define SNAC_FAMILY_ADVERT     0x0005
#define SNAC_FAMILY_INVITE     0x0006
#define SNAC_FAMILY_ADMIN      0x0007
#define SNAC_FAMILY_POPUP      0x0008
#define SNAC_FAMILY_BOS        0x0009
#define SNAC_FAMILY_USERLOOKUP 0x000a
#define SNAC_FAMILY_STATS      0x000b
#define SNAC_FAMILY_TRANSLATE  0x000c
#define SNAC_FAMILY_CHATNAV    0x000d
#define SNAC_FAMILY_CHAT       0x000e
#define SNAC_FAMILY_ODIR       0x000f
#define SNAC_FAMILY_BART       0x0010
#define SNAC_FAMILY_FEEDBAG    0x0013
#define SNAC_FAMILY_ICQ        0x0015
#define SNAC_FAMILY_AUTH       0x0017
#define SNAC_FAMILY_ALERT      0x0018

#define AIM_CB_FAM_SPECIAL 0xffff /* Internal libfaim use */

/*
 * SNAC Family: Ack.
 *
 * Not really a family, but treating it as one really
 * helps it fit into the libfaim callback structure better.
 *
 */
#define AIM_CB_ACK_ACK 0x0001

/*
 * SNAC Family: General.
 */
#define SNAC_SUBTYPE_OSERVICE_ERROR 0x0001
#define SNAC_SUBTYPE_OSERVICE_CLIENTREADY 0x0002
#define SNAC_SUBTYPE_OSERVICE_SERVERREADY 0x0003
#define SNAC_SUBTYPE_OSERVICE_SERVICEREQ 0x0004
#define SNAC_SUBTYPE_OSERVICE_REDIRECT 0x0005
#define SNAC_SUBTYPE_OSERVICE_RATEINFOREQ 0x0006
#define SNAC_SUBTYPE_OSERVICE_RATEINFO 0x0007
#define SNAC_SUBTYPE_OSERVICE_RATEINFOACK 0x0008
#define SNAC_SUBTYPE_OSERVICE_RATECHANGE 0x000a
#define SNAC_SUBTYPE_OSERVICE_SERVERPAUSE 0x000b
#define SNAC_SUBTYPE_OSERVICE_SERVERRESUME 0x000d
#define SNAC_SUBTYPE_OSERVICE_REQSELFINFO 0x000e
#define SNAC_SUBTYPE_OSERVICE_SELFINFO 0x000f
#define SNAC_SUBTYPE_OSERVICE_EVIL 0x0010
#define SNAC_SUBTYPE_OSERVICE_SETIDLE 0x0011
#define SNAC_SUBTYPE_OSERVICE_MIGRATIONREQ 0x0012
#define SNAC_SUBTYPE_OSERVICE_MOTD 0x0013
#define SNAC_SUBTYPE_OSERVICE_SETPRIVFLAGS 0x0014
#define SNAC_SUBTYPE_OSERVICE_WELLKNOWNURL 0x0015
#define SNAC_SUBTYPE_OSERVICE_NOP 0x0016
#define SNAC_SUBTYPE_OSERVICE_DEFAULT 0xffff

/*
 * SNAC Family: Location Services.
 */
#define SNAC_SUBTYPE_LOCATE_ERROR 0x0001
#define SNAC_SUBTYPE_LOCATE_REQRIGHTS 0x0002
#define SNAC_SUBTYPE_LOCATE_RIGHTSINFO 0x0003
#define SNAC_SUBTYPE_LOCATE_SETUSERINFO 0x0004
#define SNAC_SUBTYPE_LOCATE_REQUSERINFO 0x0005
#define SNAC_SUBTYPE_LOCATE_USERINFO 0x0006
#define SNAC_SUBTYPE_LOCATE_WATCHERSUBREQ 0x0007
#define SNAC_SUBTYPE_LOCATE_WATCHERNOT 0x0008
#define SNAC_SUBTYPE_LOCATE_GOTINFOBLOCK 0xfffd
#define SNAC_SUBTYPE_LOCATE_DEFAULT 0xffff

/*
 * SNAC Family: Buddy List Management Services.
 */
#define SNAC_SUBTYPE_BUDDY_ERROR 0x0001
#define SNAC_SUBTYPE_BUDDY_REQRIGHTS 0x0002
#define SNAC_SUBTYPE_BUDDY_RIGHTSINFO 0x0003
#define SNAC_SUBTYPE_BUDDY_ADDBUDDY 0x0004
#define SNAC_SUBTYPE_BUDDY_REMBUDDY 0x0005
#define SNAC_SUBTYPE_BUDDY_REJECT 0x000a
#define SNAC_SUBTYPE_BUDDY_ONCOMING 0x000b
#define SNAC_SUBTYPE_BUDDY_OFFGOING 0x000c
#define SNAC_SUBTYPE_BUDDY_DEFAULT 0xffff

/*
 * SNAC Family: Messaging Services.
 */
#define SNAC_SUBTYPE_ICBM_ERROR 0x0001
#define SNAC_SUBTYPE_ICBM_PARAMINFO 0x0005
#define SNAC_SUBTYPE_ICBM_INCOMING 0x0007
#define SNAC_SUBTYPE_ICBM_EVIL 0x0009
#define SNAC_SUBTYPE_ICBM_MISSEDCALL 0x000a
#define SNAC_SUBTYPE_ICBM_CLIENTAUTORESP 0x000b
#define SNAC_SUBTYPE_ICBM_ACK 0x000c
#define SNAC_SUBTYPE_ICBM_MTN 0x0014
#define SNAC_SUBTYPE_ICBM_DEFAULT 0xffff

/*
 * SNAC Family: Advertisement Services
 */
#define SNAC_SUBTYPE_ADVERT_ERROR 0x0001
#define SNAC_SUBTYPE_ADVERT_DEFAULT 0xffff

/*
 * SNAC Family: Invitation Services.
 */
#define SNAC_SUBTYPE_INVITE_ERROR 0x0001
#define SNAC_SUBTYPE_INVITE_DEFAULT 0xffff

/*
 * SNAC Family: Administrative Services.
 */
#define SNAC_SUBTYPE_ADMIN_ERROR 0x0001
#define SNAC_SUBTYPE_ADMIN_INFOCHANGE_REPLY 0x0005
#define SNAC_SUBTYPE_ADMIN_DEFAULT 0xffff

/*
 * SNAC Family: Popup Messages
 */
#define SNAC_SUBTYPE_POPUP_ERROR 0x0001
#define SNAC_SUBTYPE_POPUP_DEFAULT 0xffff

/*
 * SNAC Family: Misc BOS Services.
 */
#define SNAC_SUBTYPE_BOS_ERROR 0x0001
#define SNAC_SUBTYPE_BOS_RIGHTSQUERY 0x0002
#define SNAC_SUBTYPE_BOS_RIGHTS 0x0003
#define SNAC_SUBTYPE_BOS_DEFAULT 0xffff

/*
 * SNAC Family: User Lookup Services
 */
#define SNAC_SUBTYPE_USERLOOKUP_ERROR 0x0001
#define SNAC_SUBTYPE_USERLOOKUP_DEFAULT 0xffff

/*
 * SNAC Family: User Status Services
 */
#define SNAC_SUBTYPE_STATS_ERROR 0x0001
#define SNAC_SUBTYPE_STATS_SETREPORTINTERVAL 0x0002
#define SNAC_SUBTYPE_STATS_REPORTACK 0x0004
#define SNAC_SUBTYPE_STATS_DEFAULT 0xffff

/*
 * SNAC Family: Translation Services
 */
#define SNAC_SUBTYPE_TRANSLATE_ERROR 0x0001
#define SNAC_SUBTYPE_TRANSLATE_DEFAULT 0xffff

/*
 * SNAC Family: Chat Navigation Services
 */
#define SNAC_SUBTYPE_CHATNAV_ERROR 0x0001
#define SNAC_SUBTYPE_CHATNAV_CREATE 0x0008
#define SNAC_SUBTYPE_CHATNAV_INFO 0x0009
#define SNAC_SUBTYPE_CHATNAV_DEFAULT 0xffff

/*
 * SNAC Family: Chat Services
 */
#define SNAC_SUBTYPE_CHAT_ERROR 0x0001
#define SNAC_SUBTYPE_CHAT_ROOMINFOUPDATE 0x0002
#define SNAC_SUBTYPE_CHAT_USERJOIN 0x0003
#define SNAC_SUBTYPE_CHAT_USERLEAVE 0x0004
#define SNAC_SUBTYPE_CHAT_OUTGOINGMSG 0x0005
#define SNAC_SUBTYPE_CHAT_INCOMINGMSG 0x0006
#define SNAC_SUBTYPE_CHAT_DEFAULT 0xffff

/*
 * SNAC Family: "New" Search
 */
#define SNAC_SUBTYPE_ODIR_ERROR 0x0001
#define SNAC_SUBTYPE_ODIR_SEARCH 0x0002
#define SNAC_SUBTYPE_ODIR_RESULTS 0x0003

/*
 * SNAC Family: Buddy icons
 */
#define SNAC_SUBTYPE_BART_ERROR 0x0001
#define SNAC_SUBTYPE_BART_REQUEST 0x0004
#define SNAC_SUBTYPE_BART_RESPONSE 0x0005

/*
 * SNAC Family: Server-Stored Buddy Lists
 */
#define SNAC_SUBTYPE_FEEDBAG_ERROR 0x0001
#define SNAC_SUBTYPE_FEEDBAG_REQRIGHTS 0x0002
#define SNAC_SUBTYPE_FEEDBAG_RIGHTSINFO 0x0003
#define SNAC_SUBTYPE_FEEDBAG_REQDATA 0x0004
#define SNAC_SUBTYPE_FEEDBAG_REQIFCHANGED 0x0005
#define SNAC_SUBTYPE_FEEDBAG_LIST 0x0006
#define SNAC_SUBTYPE_FEEDBAG_ACTIVATE 0x0007
#define SNAC_SUBTYPE_FEEDBAG_ADD 0x0008
#define SNAC_SUBTYPE_FEEDBAG_MOD 0x0009
#define SNAC_SUBTYPE_FEEDBAG_DEL 0x000A
#define SNAC_SUBTYPE_FEEDBAG_SRVACK 0x000E
#define SNAC_SUBTYPE_FEEDBAG_NOLIST 0x000F
#define SNAC_SUBTYPE_FEEDBAG_EDITSTART 0x0011
#define SNAC_SUBTYPE_FEEDBAG_EDITSTOP 0x0012
#define SNAC_SUBTYPE_FEEDBAG_SENDAUTH 0x0014
#define SNAC_SUBTYPE_FEEDBAG_RECVAUTH 0x0015
#define SNAC_SUBTYPE_FEEDBAG_SENDAUTHREQ 0x0018
#define SNAC_SUBTYPE_FEEDBAG_RECVAUTHREQ 0x0019
#define SNAC_SUBTYPE_FEEDBAG_SENDAUTHREP 0x001a
#define SNAC_SUBTYPE_FEEDBAG_RECVAUTHREP 0x001b
#define SNAC_SUBTYPE_FEEDBAG_ADDED 0x001c

/*
 * SNAC Family: ICQ
 *
 * Most of these are actually special.
 */
#define SNAC_SUBTYPE_ICQ_ERROR 0x0001
#define SNAC_SUBTYPE_ICQ_OFFLINEMSG 0x00f0
#define SNAC_SUBTYPE_ICQ_OFFLINEMSGCOMPLETE 0x00f1
#define SNAC_SUBTYPE_ICQ_INFO 0x00f2
#define SNAC_SUBTYPE_ICQ_ALIAS 0x00f3
#define SNAC_SUBTYPE_ICQ_DEFAULT 0xffff

/*
 * SNAC Family: Authorizer
 *
 * Used only in protocol versions three and above.
 *
 */
#define SNAC_SUBTYPE_AUTH_ERROR 0x0001
#define SNAC_SUBTYPE_AUTH_LOGINREQEST 0x0002
#define SNAC_SUBTYPE_AUTH_LOGINRESPONSE 0x0003
#define SNAC_SUBTYPE_AUTH_AUTHREQ 0x0006
#define SNAC_SUBTYPE_AUTH_AUTHRESPONSE 0x0007
#define SNAC_SUBTYPE_AUTH_SECURID_REQUEST 0x000a
#define SNAC_SUBTYPE_AUTH_SECURID_RESPONSE 0x000b

/*
 * SNAC Family: Email
 *
 * Used for getting information on the email address
 * associated with your screen name.
 *
 */
#define SNAC_SUBTYPE_ALERT_ERROR 0x0001
#define SNAC_SUBTYPE_ALERT_SENDCOOKIES 0x0006
#define SNAC_SUBTYPE_ALERT_MAILSTATUS 0x0007
#define SNAC_SUBTYPE_ALERT_INIT 0x0016

/*
 * SNAC Family: Internal Messages
 *
 * This isn't truly a SNAC family either, but using
 * these, we can integrated non-SNAC services into
 * the SNAC-centered libfaim callback structure.
 *
 */
#define AIM_CB_SPECIAL_CONNERR 0x0003
#define AIM_CB_SPECIAL_CONNINITDONE 0x0006

/* SNAC flags */
#define AIM_SNACFLAGS_DESTRUCTOR 0x0001

#endif /* _SNACTYPES_H_ */
