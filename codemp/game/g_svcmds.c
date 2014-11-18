// Copyright (C) 1999-2000 Id Software, Inc.
//

// this file holds commands that can be executed by the server console, but not remote clients

#include "g_local.h"

/*
==============================================================================

PACKET FILTERING
 

You can add or remove addresses from the filter list with:

addip <ip>
removeip <ip>

The ip address is specified in dot format, and any unspecified digits will match any value, so you can specify an entire class C network with "addip 192.246.40".

Removeip will only remove an address specified exactly the same way.  You cannot addip a subnet, then removeip a single host.

listip
Prints the current list of filters.

g_filterban <0 or 1>

If 1 (the default), then ip addresses matching the current list will be prohibited from entering the game.  This is the default setting.

If 0, then only addresses matching the list will be allowed.  This lets you easily set up a private game, or a game that only allows players from your local network.


==============================================================================
*/

typedef struct ipFilter_s {
	uint32_t mask, compare;
} ipFilter_t;

#define	MAX_IPFILTERS (1024)

static ipFilter_t	ipFilters[MAX_IPFILTERS];
static int			numIPFilters;

/*
=================
StringToFilter
=================
*/
static qboolean StringToFilter( char *s, ipFilter_t *f ) {
	char num[128];
	int i, j;
	byteAlias_t b, m;

	b.ui = m.ui = 0u;

	for ( i=0; i<4; i++ ) {
		if ( *s < '0' || *s > '9' ) {
			if ( *s == '*' ) {
				// 'match any'
				// b[i] and m[i] to 0
				s++;
				if ( !*s )
					break;
				s++;
				continue;
			}
			trap->Print( "Bad filter address: %s\n", s );
			return qfalse;
		}

		j = 0;
		while ( *s >= '0' && *s <= '9' )
			num[j++] = *s++;

		num[j] = 0;
		b.b[i] = (byte)atoi( num );
		m.b[i] = 0xFF;

		if ( !*s )
			break;

		s++;
	}

	f->mask = m.ui;
	f->compare = b.ui;

	return qtrue;
}

/*
=================
UpdateIPBans
=================
*/
static void UpdateIPBans( void ) {
	byteAlias_t b, m;
	int i, j;
	char ip[NET_ADDRSTRMAXLEN], iplist_final[MAX_CVAR_VALUE_STRING];

	*iplist_final = 0;
	for ( i=0; i<numIPFilters; i++ ) {
		if ( ipFilters[i].compare == 0xFFFFFFFFu )
			continue;

		b.ui = ipFilters[i].compare;
		m.ui = ipFilters[i].mask;
		*ip = 0;
		for ( j=0; j<4; j++ ) {
			if ( m.b[j] != 0xFF )
				Q_strcat( ip, sizeof( ip ), "*" );
			else
				Q_strcat( ip, sizeof( ip ), va( "%i", (int)b.c[j] ) );
			Q_strcat( ip, sizeof( ip ), (j<3) ? "." : " " );
		}
		if ( strlen( iplist_final )+strlen( ip ) < MAX_CVAR_VALUE_STRING )
			Q_strcat( iplist_final, sizeof( iplist_final ), ip );
		else {
			Com_Printf( "g_banIPs overflowed at MAX_CVAR_VALUE_STRING\n" );
			break;
		}
	}

	trap->Cvar_Set( "g_banIPs", iplist_final );
}

/*
=================
G_FilterPacket
=================
*/
qboolean G_FilterPacket( char *from ) {
	int i;
	uint32_t in;
	byteAlias_t m;
	char *p;

	i = 0;
	p = from;
	while ( *p && i < 4 ) {
		m.b[i] = 0;
		while ( *p >= '0' && *p <= '9' ) {
			m.b[i] = m.b[i]*10 + (*p - '0');
			p++;
		}
		if ( !*p || *p == ':' )
			break;
		i++, p++;
	}
	
	in = m.ui;

	for ( i=0; i<numIPFilters; i++ ) {
		if ( (in & ipFilters[i].mask) == ipFilters[i].compare )
			return g_filterBan.integer != 0;
	}

	return g_filterBan.integer == 0;
}

/*
=================
AddIP
=================
*/
void AddIP( char *str ) {
	int i;

	for ( i=0; i<numIPFilters; i++ ) {
		if ( ipFilters[i].compare == 0xFFFFFFFFu )
			break; // free spot
	}
	if ( i == numIPFilters ) {
		if ( numIPFilters == MAX_IPFILTERS ) {
			trap->Print( "IP filter list is full\n" );
			return;
		}
		numIPFilters++;
	}
	
	if ( !StringToFilter( str, &ipFilters[i] ) )
		ipFilters[i].compare = 0xFFFFFFFFu;

	UpdateIPBans();
}

/*
=================
G_ProcessIPBans
=================
*/
void G_ProcessIPBans( void ) {
	char *s = NULL, *t = NULL, str[MAX_CVAR_VALUE_STRING] = {0};

	Q_strncpyz( str, g_banIPs.string, sizeof( str ) );

	for ( t=s=g_banIPs.string; *t; t=s ) {
		s = strchr( s, ' ' );
		if ( !s )
			break;

		while ( *s == ' ' )
			*s++ = 0;

		if ( *t )
			AddIP( t );
	}
}

/*
=================
Svcmd_AddIP_f
=================
*/
void Svcmd_AddIP_f (void)
{
	char		str[MAX_TOKEN_CHARS];

	if ( trap->Argc() < 2 ) {
		trap->Print("Usage: addip <ip-mask>\n");
		return;
	}

	trap->Argv( 1, str, sizeof( str ) );

	AddIP( str );
}

/*
=================
Svcmd_RemoveIP_f
=================
*/
void Svcmd_RemoveIP_f (void)
{
	ipFilter_t	f;
	int			i;
	char		str[MAX_TOKEN_CHARS];

	if ( trap->Argc() < 2 ) {
		trap->Print("Usage: removeip <ip-mask>\n");
		return;
	}

	trap->Argv( 1, str, sizeof( str ) );

	if (!StringToFilter (str, &f))
		return;

	for (i=0 ; i<numIPFilters ; i++) {
		if (ipFilters[i].mask == f.mask	&&
			ipFilters[i].compare == f.compare) {
			ipFilters[i].compare = 0xffffffffu;
			trap->Print ("Removed.\n");

			UpdateIPBans();
			return;
		}
	}

	trap->Print ( "Didn't find %s.\n", str );
}

void Svcmd_ListIP_f (void)
{
	int		i, count = 0;
	byteAlias_t b;

	for(i = 0; i < numIPFilters; i++) {
		if ( ipFilters[i].compare == 0xffffffffu )
			continue;

		b.ui = ipFilters[i].compare;
		trap->Print ("%i.%i.%i.%i\n", b.b[0], b.b[1], b.b[2], b.b[3]);
		count++;
	}
	trap->Print ("%i bans.\n", count);
}

/*
===================
Svcmd_EntityList_f
===================
*/
void	Svcmd_EntityList_f (void) {
	int			e;
	gentity_t		*check;

	check = g_entities;
	for (e = 0; e < level.num_entities ; e++, check++) {
		if ( !check->inuse ) {
			continue;
		}
		trap->Print("%3i:", e);
		switch ( check->s.eType ) {
		case ET_GENERAL:
			trap->Print("ET_GENERAL          ");
			break;
		case ET_PLAYER:
			trap->Print("ET_PLAYER           ");
			break;
		case ET_ITEM:
			trap->Print("ET_ITEM             ");
			break;
		case ET_MISSILE:
			trap->Print("ET_MISSILE          ");
			break;
		case ET_SPECIAL:
			trap->Print("ET_SPECIAL          ");
			break;
		case ET_HOLOCRON:
			trap->Print("ET_HOLOCRON         ");
			break;
		case ET_MOVER:
			trap->Print("ET_MOVER            ");
			break;
		case ET_BEAM:
			trap->Print("ET_BEAM             ");
			break;
		case ET_PORTAL:
			trap->Print("ET_PORTAL           ");
			break;
		case ET_SPEAKER:
			trap->Print("ET_SPEAKER          ");
			break;
		case ET_PUSH_TRIGGER:
			trap->Print("ET_PUSH_TRIGGER     ");
			break;
		case ET_TELEPORT_TRIGGER:
			trap->Print("ET_TELEPORT_TRIGGER ");
			break;
		case ET_INVISIBLE:
			trap->Print("ET_INVISIBLE        ");
			break;
		case ET_NPC:
			trap->Print("ET_NPC              ");
			break;
		case ET_BODY:
			trap->Print("ET_BODY             ");
			break;
		case ET_TERRAIN:
			trap->Print("ET_TERRAIN          ");
			break;
		case ET_FX:
			trap->Print("ET_FX               ");
			break;
		default:
			trap->Print("%-3i                ", check->s.eType);
			break;
		}

		if ( check->classname ) {
			trap->Print("%s", check->classname);
		}
		trap->Print("\n");
	}
}

qboolean StringIsInteger( const char *s );
/*
===================
ClientForString
===================
*/
gclient_t	*ClientForString( const char *s ) {
	gclient_t	*cl;
	int			idnum;
	char		cleanInput[MAX_STRING_CHARS];

	// numeric values could be slot numbers
	if ( StringIsInteger( s ) ) {
		idnum = atoi( s );
		if ( idnum >= 0 && idnum < level.maxclients ) {
			cl = &level.clients[idnum];
			if ( cl->pers.connected == CON_CONNECTED ) {
				return cl;
			}
		}
	}

	Q_strncpyz( cleanInput, s, sizeof(cleanInput) );
	Q_StripColor( cleanInput );

	// check for a name match
	for ( idnum=0,cl=level.clients ; idnum < level.maxclients ; idnum++,cl++ ) {
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( !Q_stricmp( cl->pers.netname_nocolor, cleanInput ) ) {
			return cl;
		}
	}

	trap->Print( "User %s is not on the server\n", s );
	return NULL;
}


void SanitizeString2(const char *in, char *out);
static int SV_ClientNumberFromString(const char *s) 
{
	gclient_t	*cl;
	int			idnum, i, match = -1;
	char		s2[MAX_STRING_CHARS];
	char		n2[MAX_STRING_CHARS];

	// numeric values are just slot numbers
	if (s[0] >= '0' && s[0] <= '9' && strlen(s) == 1) //changed this to only recognize numbers 0-31 as client numbers, otherwise interpret as a name, in which case sanitize2 it and accept partial matches (return error if multiple matches)
		{
		idnum = atoi( s );
		cl = &level.clients[idnum];
		if ( cl->pers.connected != CON_CONNECTED ) {
			trap->Print( "Client '%i' is not active\n", idnum);
			return -1;
		}
		return idnum;
	}

	else if (s[0] == '1' && s[0] == '2' && (s[1] >= '0' && s[1] <= '9' && strlen(s) == 2)) 
	{
		idnum = atoi( s );
		cl = &level.clients[idnum];
		if ( cl->pers.connected != CON_CONNECTED ) {
			trap->Print( "Client '%i' is not active\n", idnum);
			return -1;
		}
		return idnum;
	}

	else if (s[0] == '3' && (s[1] >= '0' && s[1] <= '1' && strlen(s) == 2)) 
	{
		idnum = atoi( s );
		cl = &level.clients[idnum];
		if ( cl->pers.connected != CON_CONNECTED ) {
			trap->Print( "Client '%i' is not active\n", idnum);
			return -1;
		}
		return idnum;
	}
	// check for a name match
	SanitizeString2( s, s2 );
	for ( idnum=0,cl=level.clients ; idnum < level.maxclients ; idnum++,cl++ ){
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		SanitizeString2( cl->pers.netname, n2 );

		for (i=0 ; i < level.numConnectedClients ; i++) 
		{
			cl=&level.clients[level.sortedClients[i]];
			SanitizeString2( cl->pers.netname, n2 );
			if (strstr(n2, s2)) 
			{
				if(match != -1)
				{ //found more than one match
					trap->Print( "More than one user '%s' on the server\n", s);
					return -2;
				}
				match = level.sortedClients[i];
			}
		}
		if (match != -1)//uhh
			return match;
	}
	trap->Print( "User '%s' is not on the server\n", s);
	return -1;
}

/*
===================
Svcmd_ForceTeam_f

forceteam <player> <team>
===================
*/
void Svcmd_ForceTeam_f( void ) {
	gclient_t	*cl;
	char		str[MAX_TOKEN_CHARS];

	if ( trap->Argc() < 3 ) {
		trap->Print("Usage: forceteam <player> <team>\n");
		return;
	}

	// find the player
	trap->Argv( 1, str, sizeof( str ) );
	cl = ClientForString( str );
	if ( !cl ) {
		return;
	}

	// set the team
	trap->Argv( 2, str, sizeof( str ) );
	SetTeam( &g_entities[cl - level.clients], str , qfalse);
}

void Svcmd_AmKick_f(void) {
		int clientid = -1; 
		char   arg[MAX_NETNAME]; 

		if (trap->Argc() != 2) {
			trap->Print( "Usage: /amKick <client>.\n");
            return; 
        } 
		trap->Argv(1, arg, sizeof(arg)); 
        clientid = SV_ClientNumberFromString(arg);

        if (clientid == -1 || clientid == -2)  
			return; 
		trap->SendConsoleCommand( EXEC_APPEND, va("clientkick %i", clientid) );

}

void Svcmd_AmBan_f(void) {
		int clientid = -1; 
		char   arg[MAX_NETNAME]; 

		if (trap->Argc() != 2) {
			trap->Print( "Usage: /amBan <client>.\n");
            return; 
        } 
		trap->Argv(1, arg, sizeof(arg)); 
        clientid = SV_ClientNumberFromString(arg);

        if (clientid == -1 || clientid == -2)  
			return; 
		AddIP(g_entities[clientid].client->sess.IP);
		trap->SendConsoleCommand( EXEC_APPEND, va("clientkick %i", clientid) );
}

void Svcmd_Amgrantadmin_f(void)
{
		char arg[MAX_NETNAME];
		int clientid = -1; 

		if (trap->Argc() != 3) {
			trap->Print( "Usage: /amGrantAdmin <client> <level>.\n");
			return; 
		}

		trap->Argv(1, arg, sizeof(arg)); 
		clientid = SV_ClientNumberFromString(arg);

		if (clientid == -1 || clientid == -2)  
			return;  

		if (!g_entities[clientid].client)
			return;

		trap->Argv(2, arg, sizeof(arg)); 
		Q_strlwr(arg);

		if (!Q_stricmp(arg, "none")) {
			g_entities[clientid].client->sess.juniorAdmin = qfalse;
			g_entities[clientid].client->sess.fullAdmin = qfalse;
		}
		else if (!Q_stricmp(arg, "junior")) {
			g_entities[clientid].client->sess.juniorAdmin = qtrue;
			g_entities[clientid].client->sess.fullAdmin = qfalse;
			trap->SendServerCommand( clientid, "print \"You have been granted Junior admin privileges.\n\"" );
		}
		else if (!Q_stricmp(arg, "full")) {
			g_entities[clientid].client->sess.juniorAdmin = qfalse;
			g_entities[clientid].client->sess.fullAdmin = qtrue;
			trap->SendServerCommand( clientid, "print \"You have been granted Full admin privileges.\n\"" );
		}
}

char *ConcatArgs( int start );
void Svcmd_Say_f( void ) {
	char *p = NULL;
	// don't let text be too long for malicious reasons
	char text[MAX_SAY_TEXT] = {0};

	if ( trap->Argc () < 2 )
		return;

	p = ConcatArgs( 1 );

	if ( strlen( p ) >= MAX_SAY_TEXT ) {
		p[MAX_SAY_TEXT-1] = '\0';
		G_SecurityLogPrintf( "Cmd_Say_f from -1 (server) has been truncated: %s\n", p );
	}

	Q_strncpyz( text, p, sizeof(text) );
	Q_strstrip( text, "\n\r", "  " );

	G_LogPrintf( "say: server: %s\n", text );
	trap->SendServerCommand( -1, va("print \"server: %s\n\"", text ) );
}

typedef struct bitInfo_S {
	const char	*string;
} bitInfo_T;

static bitInfo_T weaponTweaks[] = { // MAX_WEAPON_TWEAKS tweaks (24)
	{"Nonrandom DEMP2"},//1
	{"Increased DEMP2 primary damage"},//2
	{"Decreased disruptor alt damage"},//3
	{"Nonrandom bowcaster spread"},//4
	{"Increased repeater alt damage"},//5
	{"Nonrandom flechette primary spread"},//6
	{"Decreased flechette alt damage"},//7
	{"Nonrandom flechette alt spread"},//8
	{"Increased concussion rifle alt damage"},//9
	{"Removed projectile knockback"},//10
	{"Stun baton lightning gun"},//11
	{"Stun baton shocklance"},//12
	{"Projectile gravity"},//13
	{"Allow center muzzle"},//14
	{"Pseudo random weapon spread"},//15
	{"Rocket alt fire mortar"},//16
	{"Rocket alt fire redeemer"},//17
	{"Infinite ammo"},//18
	{"Stun baton heal gun"},//19
	{"Weapons can damage vehicles"},//20
	{"Reduced saberblock for MP damages"},//21
	{"Allow gunroll"},//22
	{"Fast weaponswitch"},//22
	{"Fixed saberswitch"}//23
};
static const int MAX_WEAPON_TWEAKS = ARRAY_LEN( weaponTweaks );

void Svcmd_ToggleTweakWeapons_f( void ) {
	if ( trap->Argc() == 1 ) {
		int i = 0;
		for ( i = 0; i < MAX_WEAPON_TWEAKS; i++ ) {
			if ( (g_tweakWeapons.integer & (1 << i)) ) {
				trap->Print( "%2d [X] %s\n", i, weaponTweaks[i].string );
			}
			else {
				trap->Print( "%2d [ ] %s\n", i, weaponTweaks[i].string );
			}
		}
		return;
	}
	else {
		char arg[8] = { 0 };
		int index;
		const uint32_t mask = (1 << MAX_WEAPON_TWEAKS) - 1;

		trap->Argv( 1, arg, sizeof(arg) );
		index = atoi( arg );

		if ( index < 0 || index >= MAX_WEAPON_TWEAKS ) {
			trap->Print( "tweakWeapons: Invalid range: %i [0, %i]\n", index, MAX_WEAPON_TWEAKS - 1 );
			return;
		}

		trap->Cvar_Set( "g_tweakWeapons", va( "%i", (1 << index) ^ (g_tweakWeapons.integer & mask ) ) );
		trap->Cvar_Update( &g_tweakWeapons );

		trap->Print( "%s %s^7\n", weaponTweaks[index].string, ((g_tweakWeapons.integer & (1 << index))
			? "^2Enabled" : "^1Disabled") );
	}
}

typedef struct svcmd_s {
	const char	*name;
	void		(*func)(void);
	qboolean	dedicated;
} svcmd_t;

int svcmdcmp( const void *a, const void *b ) {
	return Q_stricmp( (const char *)a, ((svcmd_t*)b)->name );
}

void G_CheckFields( void );
void G_CheckSpawns( void );
void Svcmd_ChangePass_f( void );
void Svcmd_Register_f( void );
void Svcmd_AccountInfo_f( void );
void Svcmd_DeleteAccount_f( void );
void Svcmd_ClearIP_f( void );
void Svcmd_DBInfo_f( void );

/* This array MUST be sorted correctly by alphabetical name field */
svcmd_t svcmds[] = {
	{ "accountInfo",				Svcmd_AccountInfo_f,				qfalse },

	{ "addbot",						Svcmd_AddBot_f,						qfalse },
	{ "addip",						Svcmd_AddIP_f,						qfalse },

	{ "amban",						Svcmd_AmBan_f,						qfalse },
	{ "amgrantadmin",				Svcmd_Amgrantadmin_f,				qfalse },
	{ "amkick",						Svcmd_AmKick_f,						qfalse },

	{ "botlist",					Svcmd_BotList_f,					qfalse },

	{ "changepassword",				Svcmd_ChangePass_f,					qfalse },

	{ "checkfields",				G_CheckFields,						qfalse },
	{ "checkspawns",				G_CheckSpawns,						qfalse },

	{ "clearIP",					Svcmd_ClearIP_f,					qfalse },
	{ "DBInfo",						Svcmd_DBInfo_f,						qfalse },
	{ "deleteAccount",				Svcmd_DeleteAccount_f,				qfalse },

	{ "entitylist",					Svcmd_EntityList_f,					qfalse },
	{ "forceteam",					Svcmd_ForceTeam_f,					qfalse },
	{ "game_memory",				Svcmd_GameMem_f,					qfalse },
	{ "listip",						Svcmd_ListIP_f,						qfalse },

	{ "register",					Svcmd_Register_f,					qfalse },

	{ "removeip",					Svcmd_RemoveIP_f,					qfalse },
	{ "say",						Svcmd_Say_f,						qtrue },
	{ "toggleuserinfovalidation",	Svcmd_ToggleUserinfoValidation_f,	qfalse },
	{ "tweakweapons",				Svcmd_ToggleTweakWeapons_f,			qfalse }
};
static const size_t numsvcmds = ARRAY_LEN( svcmds );

/*
=================
ConsoleCommand

=================
*/
qboolean	ConsoleCommand( void ) {
	char	cmd[MAX_TOKEN_CHARS] = {0};
	svcmd_t	*command = NULL;

	trap->Argv( 0, cmd, sizeof( cmd ) );

	command = (svcmd_t *)bsearch( cmd, svcmds, numsvcmds, sizeof( svcmds[0] ), svcmdcmp );
	if ( !command )
		return qfalse;

	if ( command->dedicated && !dedicated.integer )
		return qfalse;

	command->func();
	return qtrue;
}

