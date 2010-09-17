#include "c-angband.h"

#include "../common/net-basics.h"
#include "../common/net-imps.h"

/* List heads */
eptr first_connection = NULL;
eptr first_listener = NULL;
eptr first_caller = NULL;
eptr first_timer = NULL;

/* Pointers */
eptr meta_caller = NULL;
eptr server_caller = NULL;
eptr server_connection = NULL;

connection_type *serv = NULL;

/* Global Flags */
s16b connected = 0;
s16b state = 0;

static int		(*handlers[256])(connection_type *ct);
static cptr		(schemes[256]);

byte next_pkt;
cptr next_scheme;

server_setup_t serv_info;

byte indicator_refs[256]; /* PKT to ID: Indicators */
byte stream_ref[256]; ;/* PKT to ID: Streams */

/* Init */
void setup_tables(void);
void setup_network_client()
{
	/* Prepare FD_SETS */
	network_reset();
	/* Setup tables..? */
	// can do it later
}
void cleanup_network_client()
{
	e_release_all(first_connection, 0, 1);
	e_release_all(first_caller, 0, 1);
}

/* Iteration of the Loop */
void network_loop()
{
	//first_listener = handle_listeners(first_listener);
	first_connection = handle_connections(first_connection);
	first_caller = handle_callers(first_caller);
	//first_timer = handle_timers(first_timer, static_timer(0));

	network_pause(1000); /* 0.001 ms "sleep" */
}

int client_close(int data1, data data2) {
	connection_type *ct = (connection_type*)data2;
	/* 0_0` */
	quit(NULL);
	return 0;
}

int client_read(int data1, data data2) { /* return -1 on error */
	cq queue;
	connection_type *ct = (connection_type *)data2;

	/* parse */
	int result = 1;
	int start_pos;
	while (	cq_len(&ct->rbuf) )
	{
		/* save */
		start_pos = ct->rbuf.pos; 
		next_pkt = CQ_GET(&ct->rbuf);
		next_scheme = schemes[next_pkt];

		result = (*handlers[next_pkt])(ct);

		/* Unable to continue */
		if (result != 1) break;
	}
	/* Not enough bytes */
	if (result == 0) 
	{
		/* load */
		ct->rbuf.pos = start_pos;
	}
	/* Slide buffer to the left */
#if 0
	/* Fast version */
	else if (result == 1)
	{
		CQ_CLEAR(&ct->rbuf);
	}
#else
	/* Slow, but safer version */
	if (ct->rbuf.pos)
	{
		char buf[PD_SMALL_BUFFER];
		strncpy(buf, &ct->rbuf.buf[ct->rbuf.pos], ct->rbuf.len);
		strncpy(ct->rbuf.buf, buf, ct->rbuf.len);
		ct->rbuf.len -= ct->rbuf.pos;
		ct->rbuf.pos = 0;
	}	
#endif

	return result;
}

					/* data1 is (int)fd */
int connected_to_server(int data1, data data2) {
	int fd = (int)data1;

	/* Unset 'caller' */
	server_caller = NULL;

	/* Setup 'connection' */
	server_connection = add_connection(first_connection, fd, client_read, client_close);
	if (!first_connection) first_connection = server_connection;

	/* Disable Nagle's algorithm */
	denaglefd(fd);

	/* Set usability pointer */	
	serv = (connection_type *)server_connection->data2;
	
	/* Prepare packet-handling tables */
	setup_tables();

	/* Is connected! */
	connected = 1;

	/* OK */	
	return 1;
}

/* Return 1 to continue, 0 to cancel */
int failed_connection_to_server(int data1, data data2) {
	/* Ask user */ 
	int r = client_failed();
	if (r == 0) connected = -1;
	return r;  
}

int call_server(char *server_name, int server_port)
{
	server_caller = add_caller(first_caller, server_name, server_port, connected_to_server, failed_connection_to_server);
	if (first_caller == NULL) first_caller = server_caller;

	/* Unset */	
	connected = 0;

	/* Try */	
	while (!connected) 
	{
		network_loop();
	}

	/* Will be either 1 either -1 */
	return connected;
}


int send_play(byte mode) {
	return cq_printf(&serv->wbuf, "%c%c", PKT_PLAY, mode);
}

int send_char_info() {
	int	n, i;
	if (n = cq_printf(&serv->wbuf, "%c%ud%ud%ud", PKT_CHAR_INFO, race, pclass, sex) <= 0)
	{
		return n;
	}

	/* Send the desired stat order */
	for (i = 0; i < 6; i++)
	{
		n = cq_printf(&serv->wbuf, "%d", stat_order[i]);
		if (n < 0) 
		{
			return n;
		}
	}

	return 1;
}

int send_login(u16b version, char* real_name, char* host_name, char* user_name, char* pass_word) {
	return cq_printf(&serv->wbuf, "%c%ud%s%s%s%s", PKT_LOGIN, version, real_name, host_name, user_name, pass_word);
}

int send_handshake(u16b conntype) {
	return cq_printf(&serv->wbuf, "%ud", conntype);
}

int send_keepalive(u32b last_keepalive) {
	return cq_printf(&serv->wbuf, "%c%l", PKT_KEEPALIVE, last_keepalive);
}

int send_request(byte mode, u16b id) {
	return cq_printf(&serv->wbuf, "%c%c%ud", PKT_BASIC_INFO, mode, id);
}

int send_stream_size(byte id, int rows, int cols) {
	return cq_printf(&serv->wbuf, "%c%c%c%c", PKT_RESIZE, id, (byte)rows, (byte)cols);
}

int send_visual_info(byte type) {
	int	n, i, size;
	byte *attr_ref;
	char *char_ref;
	switch (type) 
	{
		case VISUAL_INFO_FLVR:
			size = MAX_FLVR_IDX;
			attr_ref = Client_setup.flvr_x_attr;
			char_ref = Client_setup.flvr_x_char;
			break;
		case VISUAL_INFO_F:
			size = z_info.f_max;
			attr_ref = Client_setup.f_attr;
			char_ref = Client_setup.f_char;
			break;
		case VISUAL_INFO_K:
			size = z_info.k_max;
 			attr_ref = Client_setup.k_attr;
  			char_ref = Client_setup.k_char;
  			break;
  		case VISUAL_INFO_R:
			size = z_info.r_max;
			attr_ref = Client_setup.r_attr;
			char_ref = Client_setup.r_char;
			break;
		case VISUAL_INFO_TVAL:
	 		size = 128;
			attr_ref = Client_setup.tval_attr;
			char_ref = Client_setup.tval_char;
			break;
		case VISUAL_INFO_MISC:
			size = 256;
			attr_ref =	Client_setup.misc_attr;
			char_ref =	Client_setup.misc_char;
			break;
		case VISUAL_INFO_PR:
			size = (z_info.c_max + 1) * z_info.p_max;
			attr_ref =	p_ptr->pr_attr;
			char_ref =	p_ptr->pr_char;
			break;
		default:
			return 0;
	}

	if (cq_printf(&serv->wbuf, "%c%c%d", PKT_VISUAL_INFO, type, size) <= 0)
	{
		return 0;
	}
	if (cq_printac(&serv->wbuf, RLE_NONE, attr_ref, char_ref, size) <= 0)
	{
		return 0;
	}

	return 1;
}

int send_msg(cptr message)
{
	return cq_printf(&serv->wbuf, "%c%S", PKT_MESSAGE, message);
}

/* Gameplay commands */
int send_walk(char dir)
{
	return cq_printf(&serv->wbuf, "%c%c", PKT_WALK, dir);
}

/* Custom command */
int send_custom_command(byte i, char item, char dir, s32b value, char *entry)
{
	custom_command_type *cc_ptr = &custom_command[i];
	int 	n;

	/* Command header */
	if (cc_ptr->pkt == (char)PKT_COMMAND)
		n = cq_printf(&serv->wbuf, "%c%c", PKT_COMMAND, i);
	else
		n = cq_printf(&serv->wbuf, "%c", cc_ptr->pkt);
	if (n <= 0) /* Error ! */
		return 0;

#define S_START case SCHEME_EMPTY: n = (1
#define S_WRITE(A) ); break; case SCHEME_ ## A: n = cq_printf(&serv->wbuf, ( CCS_ ## A ),
#define S_DONE ); break;

	/* Command body */
	switch (cc_ptr->scheme)
	{
		S_START \
		S_WRITE(ITEM)           	item\
		S_WRITE(DIR)            	dir\
		S_WRITE(VALUE)          	value\
		S_WRITE(SMALL)          	(byte)value\
		S_WRITE(STRING)         	entry\
		S_WRITE(CHAR)           	entry[0]\
		S_WRITE(DIR_VALUE)      	dir, value\
		S_WRITE(DIR_SMALL)      	dir, (byte)value\
		S_WRITE(DIR_STRING)     	dir, entry\
		S_WRITE(DIR_CHAR)       	dir, entry[0]\
		S_WRITE(VALUE_STRING)   	value, entry\
		S_WRITE(VALUE_CHAR)     	value, entry[0]\
		S_WRITE(SMALL_STRING)   	(byte)value, entry\
		S_WRITE(SMALL_CHAR)     	(byte)value, entry[0]\
		S_WRITE(ITEM_DIR)       	item, dir\
		S_WRITE(ITEM_VALUE)     	item, value\
		S_WRITE(ITEM_SMALL)     	item, (byte)value\
		S_WRITE(ITEM_STRING)    	item, entry\
		S_WRITE(ITEM_CHAR)      	item, entry[0]\
		S_WRITE(ITEM_DIR_VALUE) 	item, dir, value\
		S_WRITE(ITEM_DIR_SMALL) 	item, dir, (byte)value\
		S_WRITE(ITEM_DIR_STRING) 	item, dir, entry\
		S_WRITE(ITEM_DIR_CHAR)  	item, dir, entry[0]\
		S_WRITE(ITEM_VALUE_STRING)	item, value, entry\
		S_WRITE(ITEM_VALUE_CHAR) 	item, value, entry[0]\
		S_WRITE(ITEM_SMALL_STRING)	item, (byte)value, entry\
		S_WRITE(ITEM_SMALL_CHAR) 	item, (byte)value, entry[0]\
		S_WRITE(FULL)           	item, dir, value, entry\
		S_DONE

	}
	if (n <= 0) /* Error ! */
		return 0;

#undef S_START
#undef S_WRITE
#undef S_DONE

	return 1;
}

/* Undefined packet "handler" */
int recv_undef(connection_type *ct) {

	printf("Undefined packet %d came from server!\n", next_pkt);

	/* Disconnect client! */
	return -1;

}

/* Keepalive packet "handler" */
int recv_keepalive(connection_type *ct) {

	s32b
		cticks = 0;
	if (cq_scanf(&ct->rbuf, "%l", &cticks) < 1)
	{
		return 0;
	}
#if 0
	/* make sure it's the same one we sent... */
	if(cticks == last_keepalive) {
		if (conn_state == CONN_PLAYING) {
			lag_mark = (mticks - last_sent);
			p_ptr->redraw |= PR_LAG_METER;
		} 
		last_keepalive=0;
	};
#endif
#if 1

#endif 
	/* Ok */
	return 1;
}

/* Quit packet, server is disconnecting us */
int recv_quit(connection_type *ct) {
	char
		reason[MSG_LEN];

	if (cq_scanf(&ct->rbuf, "%S", reason) < 1) 
	{
		strcpy(reason, "unknown reason");
	}

	quit(format("Quitting: %s", reason));
	return 1;
}

int recv_basic_info(connection_type *ct) {

	if (cq_scanf(&ct->rbuf, "%b%b%b%b", &serv_info.val1, &serv_info.val2, &serv_info.val3, &serv_info.val4) < 4) 
	{
		/* Not enough bytes */
		return 0;
	}
	if (cq_scanf(&ct->rbuf, "%ul%ul%ul%ul", &serv_info.val9, &serv_info.val10, &serv_info.val11, &serv_info.val12) < 4) 
	{
		/* Not enough bytes */
		return 0;
	}

	z_info.k_max = serv_info.val9;
	z_info.r_max = serv_info.val10;
	z_info.f_max = serv_info.val11;

	/* Ok */
	return 1;
}

/* Play packet, server is promoting us */
int recv_play(connection_type *ct) {
	byte
		mode = 0;

	if (cq_scanf(&ct->rbuf, "%b", &mode) < 1) 
	{
		/* Not enough bytes */
		return 0;
	}

	/* React */
	if (mode == PLAYER_EMPTY) client_login();
	else state = mode;

	/* Ok */
	return 1;
}

/* Character info packet, important at setup stage */
int recv_char_info(connection_type *ct) {
		state = 0;
		race = 0;
		pclass = 0;
		sex = 0;
	if (cq_scanf(&ct->rbuf, "%d%d%d%d", &state, &race, &pclass, &sex) < 4)
	{
		/* Not enough bytes */
		return 0;
	}

	p_ptr->state = state;
	p_ptr->prace = (byte)race;
	p_ptr->pclass = (byte)pclass;
	p_ptr->male = (byte)sex;

	/* Ok */
	return 1;
}

/* */
int recv_struct_info(connection_type *ct)
{
	char 	ch;
	int 	i, n;
	byte 	typ;
	u16b 	max;
	char 	name[MAX_CHARS];
	u32b 	off, fake_name_size, fake_text_size;
	byte	spell_book;

	off = fake_name_size = fake_text_size = max = typ = 0;

	if (cq_scanf(&ct->rbuf, "%c%ud%ul%ul", &typ, &max, &fake_name_size, &fake_text_size) < 4)
	{
		/* Not ready */
		return 0;
	}

	/* Which struct */
	switch (typ)
	{
#if 0	
		/* Option groups */
		case STRUCT_INFO_OPTGROUP:
			/* Alloc */
			C_MAKE(option_group, max, cptr);
			options_groups_max = max;
			
			/* Fill */
			for (i = 0; i < max; i++)
			{
				if ((n = Packet_scanf(&rbuf, "%s", name)) <= 0)
				{
					return n;
				}
				
				/* Transfer */
				option_group[i] = string_make(name);
			}
		break;
		/* Options */
		case STRUCT_INFO_OPTION:
			/* Alloc */
			C_MAKE(option_info, max, option_type);
			options_max = max;
			
			/* Fill */
			for (i = 0; i < max; i++)
			{
				option_type *opt_ptr;
				byte opt_page;
				char desc[MAX_CHARS];

				opt_ptr = &option_info[i];
				opt_page = 0;
				
				
				if ((n = Packet_scanf(&rbuf, "%c%s%s", &opt_page, name, desc)) <= 0)
				{
					return n;
				}
				
				/* Transfer */
				opt_ptr->o_page = opt_page;
				opt_ptr->o_text = string_make(name);
				opt_ptr->o_desc = string_make(desc);
				opt_ptr->o_set = 0;
				/* Link to local */
				for (n = 0; local_option_info[n].o_desc; n++)
				{
					if (!strcmp(local_option_info[n].o_text, name))
					{
						local_option_info[n].o_set = i;
						opt_ptr->o_set = n;
					}				
				}
			}
		break;
#endif		
		/* Player Races */
		case STRUCT_INFO_RACE:
			/* Alloc */
			C_MAKE(p_name, fake_name_size, char);
			C_MAKE(race_info, max, player_race);
			z_info.p_max = max;
			
			/* Fill */
			for (i = 0; i < max; i++) 
			{
				player_race *pr_ptr = NULL;
				pr_ptr = &race_info[i];
	
				off = 0;
						
				if (cq_scanf(&ct->rbuf, "%s%ul", &name, &off) < 2)
				{
					return 0;
				}

				strcpy(p_name + off, name);
			
				pr_ptr->name = off;
				/* Transfer other fields here */
			}
		break;
		/* Player Classes */
		case STRUCT_INFO_CLASS:
			/* Alloc */
			C_MAKE(c_name, fake_name_size, char);
			C_MAKE(c_info, max, player_class);
			z_info.c_max = max;
			
			/* Fill */
			for (i = 0; i < max; i++) 
			{
				player_class *pc_ptr = NULL;
				pc_ptr = &c_info[i];
	
				off = spell_book = 0;
						
				if (cq_scanf(&ct->rbuf, "%s%ul%c", &name, &off, &spell_book) < 3)
				{
					return 0;
				}

				strcpy(c_name + off, name);
			
				pc_ptr->name = off;
				/* Transfer other fields here */
				pc_ptr->spell_book = spell_book;
			}
		break;
	}	
	
	return 1;
}

/* Read and update specific indicator */
int recv_indicator(connection_type *ct) {

	indicator_type *i_ptr; 
	int id = indicator_refs[next_pkt];
	int i, coff;

	signed char tiny_c;
	s16b normal_c;
	s32b large_c;
	char* text_c;

	/* Error -- unknown indicator */
	if (id > known_indicators) return -1;

	i_ptr = &indicators[id];
	coff = coffer_refs[id];

	/* Read (i_ptr->coffer) values of type (s16b/byte) */
	for (i = 0; i < i_ptr->coffer; i++) 
	{
		/* Read */
		s16b val = 0, n = 0;
		if (i_ptr->tiny  == INDITYPE_TINY)
		{
			n = cq_scanf(&ct->rbuf, "%c", &tiny_c);
			val = (s32b)tiny_c;
		} 
		else if (i_ptr->tiny == INDITYPE_NORMAL)
		{
			n = cq_scanf(&ct->rbuf, "%d", &normal_c);
			val = (s32b)normal_c;
		}
		else if (i_ptr->tiny == INDITYPE_LARGE)
		{
			n = cq_scanf(&ct->rbuf, "%l", &large_c);
			val = (s32b)large_c;
		}

		/* Error ? */
		if (n < 1) return 0;

		/* Save */
		coffers[coff + i] = val;
	}

	/* Schedule redraw */
	p_ptr->redraw |= i_ptr->redraw;
	
	return 1;
}

int recv_indicator_str(connection_type *ct) {
	indicator_type *i_ptr;
	int id = indicator_refs[next_pkt];
	char buf[MAX_CHARS]; 

	/* Error -- unknown indicator */
	if (id > known_indicators) return -1;

	i_ptr = &indicators[id];

	/* Read the string */
	if (cq_scanf(&ct->rbuf, "%s", buf) < 1) return 0;

	/* Store the string in indicator's 'prompt' */
	strncpy(i_ptr->prompt, buf, MAX_CHARS);

	/* Schedule redraw */
	p_ptr->redraw |= i_ptr->redraw;
	
	return 1;
}

/* Learn about certain indicator from server */
int recv_indicator_info(connection_type *ct) {
	byte
		pkt = 0,
		tiny = 0,
		amnt = 0;
	char buf[MSG_LEN]; //TODO: check this 
	char mark[MAX_CHARS];
	s16b row = 0,
		col = 0;
	u32b flag = 0;
	int n;

	indicator_type *i_ptr;

	if (cq_scanf(&ct->rbuf, "%c%c%c%d%d%ul%S%s", &pkt, &tiny, &amnt, &row, &col, &flag, buf, mark) < 8) return 0;

	/* Check for errors */
	if (known_indicators >= MAX_INDICATORS)
	{
		plog("No more indicator slots! (MAX_INDICATORS)");
		return -1;
	}
	if (known_coffers + amnt + 1 >= MAX_COFFERS)
	{
		plog("Not enougth coffer slots! (MAX_COFFERS)");
		return -1;
	}

	/* Get it */
	i_ptr = &indicators[known_indicators];

	i_ptr->pkt = pkt;
	i_ptr->tiny = tiny;
	i_ptr->coffer = amnt;
	i_ptr->redraw = (1L << (known_indicators));
	i_ptr->row = row;
	i_ptr->col = col;
	i_ptr->flag = flag;

	i_ptr->mark = strdup(mark);
	i_ptr->prompt = strdup(buf);

	handlers[pkt] = ((tiny != INDITYPE_STRING) ? recv_indicator : recv_indicator_str);
	schemes[pkt] = NULL; /* HACK */

	indicator_refs[pkt] = known_indicators;
	coffer_refs[known_indicators] = known_coffers;

	known_coffers += amnt;
	known_indicators++;

	/* Register a possible local overload */
	register_indicator(known_indicators - 1);

	return 1;
}

/* ... */
int read_stream_char(byte st, byte addr, bool trn, bool mem, s16b y, s16b x)
{
	int 	n;
	byte
		a = 0,
		ta = 0;
	char
		c = 0,
		tc = 0;

	cave_view_type *dest = stream_cave(st, y);

	if (cq_scanf(&serv->rbuf, "%c%c", &a, &c) < 2) return 0;

	if (trn && cq_scanf(&serv->rbuf, "%c%c", &ta, &tc) < 2) return 0;

	dest[x].a = a;
	dest[x].c = c;

	if (y > last_remote_line[addr]) 
		last_remote_line[addr] = y; 

	if (addr == NTERM_WIN_OVERHEAD)
		show_char(y, x, a, c, ta, tc, mem);

	return 1;
}
int recv_stream(connection_type *ct) {
	s16b	cols, y = 0;
	s16b	*line;
	byte	addr, id;
	cave_view_type	*dest;

	stream_type 	*stream;

	if (cq_scanf(&ct->rbuf, "%d", &y) < 1) return 0;

	id = stream_ref[next_pkt];
	stream = &streams[id];
	addr = stream->addr;

	/* Hack -- single char */
	if (y & 0xFF00)	return 
		read_stream_char(id, addr, (stream->flag & SF_TRANSPARENT), !(stream->flag & SF_OVERLAYED), (y & 0x00FF), (y >> 8)-1 );

	cols = p_ptr->stream_wid[id];
	dest = p_ptr->stream_cave[id] + y * cols;
 	line = &last_remote_line[addr];

	/* Decode the secondary attr/char stream */
	if ((stream->flag & SF_TRANSPARENT))
	{
		if (cq_scanc(&ct->rbuf, stream->rle, p_ptr->trn_info[y], cols) <= 0) return -1;
	}
	/* OR clear it ! */ 
	else if (stream->flag & SF_OVERLAYED)
		caveclr(p_ptr->trn_info[y], cols);

	/* Decode the attr/char stream */
	if (cq_scanc(&ct->rbuf, stream->rle, dest, cols) <= 0) return -1;

	/* Check the min/max line count */
	if ((*line) < y)
		(*line) = y;
	/* TODO: test this approach -- else if (y == 0) (*line) = 0; */

	/* Put data to screen ? */		
	if (addr == NTERM_WIN_OVERHEAD)
		show_line(y, cols, !(stream->flag & SF_OVERLAYED));

	return 1;
}

int recv_stream_size(connection_type *ct) {
	byte
		stg = 0,
		x = 0,
		y = 0;
	byte	st, addr;

	if (cq_scanf(&ct->rbuf, "%c%c%c", &stg, &y, &x) < 3) return 0;

	/* Ensure it is valid and start from there */
	if (stg >= known_streams) { printf("invalid stream %d (known - %d)\n", stg, known_streams); return 1;}

	/* Fetch target "window" */	
	addr = streams[stg].addr;

	/* (Re)Allocate memory */
	if (remote_info[addr])
	{
		KILL(remote_info[addr]);
	}
	C_MAKE(remote_info[addr], (y+1) * x, cave_view_type);
	last_remote_line[addr] = 0;

	/* Affect the whole group */
	for (st = stg; st < known_streams; st++)
	{
		/* Stop when we move on to the next group */
		if (streams[st].addr != addr) break;

		/* Save new size */
		p_ptr->stream_wid[st] = x;
		p_ptr->stream_hgt[st] = y;

		/* Save pointer */
		p_ptr->stream_cave[st] = remote_info[addr];
	}

	/* HACK - Dungeon display resize */
	if (addr == NTERM_WIN_OVERHEAD)
	{
		/* Redraw status line */
		Term_erase(0, y-1, x);
		p_ptr->redraw |= PR_STATUS;
		/* Redraw compact */
		p_ptr->redraw |= PR_COMPACT;
	}


	return 1;
}

/* Learn about certain stream from server */
int recv_stream_info(connection_type *ct) {
	byte
		pkt = 0,
		addr = 0,
		flag = 0,
		rle = 0;
	byte
		min_col = 0,
		min_row = 0,
		max_col = 0,
		max_row = 0;
	char buf[MSG_LEN]; //TODO: check this 
	int n;

	stream_type *s_ptr;

	buf[0] = '\0';

	if (cq_scanf(&ct->rbuf, "%c%c%c%c%s%c%c%c%c", &pkt, &addr, &rle, &flag, buf, 
			&min_row, &min_col, &max_row, &max_col) < 9) return 0;

	/* Check for errors */
	if (known_streams >= MAX_STREAMS)
	{
		plog("No more stream slots! (MAX_STREAMS)");
		return -1;
	}

	/* Get it */
	s_ptr = &streams[known_streams];
	WIPE(s_ptr, stream_type);

	s_ptr->pkt = pkt;
	s_ptr->addr = addr;	
	s_ptr->rle = rle;

	s_ptr->flag = flag;

	/*s_ptr->scr = (!addr ? p_ptr->scr_info : remote_info[addr] );
	s_ptr->trn = (!trn ? NULL : p_ptr->trn_info);*/

	s_ptr->min_row = min_row;
	s_ptr->min_col = min_col;
	s_ptr->max_row = max_row;
	s_ptr->max_col = max_col;

	if (!STRZERO(buf))
	{
		s_ptr->mark = strdup(buf);
	}


	handlers[pkt] = recv_stream;
	schemes[pkt] = NULL; /* HACK */

	stream_ref[pkt] = known_streams;

	known_streams++;	


	return 1;
}

int recv_message(connection_type *ct) {
	char 
		mesg[80];
	u16b 
		type = 0;
	if (cq_scanf(&ct->rbuf, "%ud%s", &type, mesg) < 2) return 0;

	c_message_add(mesg, type);	

	return 1;
}

int recv_custom_command_info(connection_type *ct) {
	byte
		pkt = 0,
		tval = 0,
		scheme = 0;
	char buf[MSG_LEN]; //TODO: check this 
	s16b m_catch = 0;
	u32b flag = 0;
	int n;

	custom_command_type *cc_ptr;

	if (cq_scanf(&ct->rbuf, "%c%c%d%ul%c%S", &pkt, &scheme, &m_catch, &flag, &tval, buf) < 6) return 0;

	/* Check for errors */
	if (known_indicators >= MAX_CUSTOM_COMMANDS)
	{
		plog("No more command slots! (MAX_CUSTOM_COMMANDS)");
		return -1;
	}
	if (scheme >= MAX_SCHEMES)
	{
		plog("Undefined CC scheme!");
		return -1;
	}

	/* Get it */
	cc_ptr = &custom_command[custom_commands];
	WIPE(cc_ptr, custom_command_type);

	cc_ptr->m_catch = m_catch;
	cc_ptr->pkt = pkt;
	cc_ptr->scheme = scheme;
	cc_ptr->flag = flag;
	cc_ptr->tval = tval;

	buf[strlen(buf)+1] = '\0';
	for (n = 0; n < sizeof(buf); n++) 
	{
		if (buf[n] == '\n') buf[n] = '\0';
		cc_ptr->prompt[n] = buf[n];
	}

	custom_commands++;

	return 1;
}

int recv_item_tester_info(connection_type *ct) {
	byte
		id = 0,
		flag = 0,
		tval = 0;
	int n;

	item_tester_type *it_ptr;

	if (cq_scanf(&ct->rbuf, "%c%c", &id, &flag) < 2) return 0;

	/* Check for errors */
	if (id >= MAX_ITEM_TESTERS)
	{
		plog("No more item_tester slots! (MAX_ITEM_TESTERS)");
		return -1;
	}

	/* Get it */
	it_ptr = &item_tester[id];
	WIPE(it_ptr, item_tester_type);

	it_ptr->flag = flag;

	for (n = 0; n < MAX_ITH_TVAL; n++) 
	{
		tval = 0;
		if (cq_scanf(&ct->rbuf, "%c", &tval) < 1) return 0;
		it_ptr->tval[n] = tval;
	}

	if (id < known_item_testers) known_item_testers = id;

	return 1;
}

void setup_tables()
{
	/* Setup receivers */
	int i;
	for (i = 0; i < 256; i++) {
		handlers[i] = recv_undef;
		schemes[i] = NULL;	
	}

#define PACKET(PKT, SCHEME, FUNC) \
	handlers[PKT] = FUNC; \
	schemes[PKT] = SCHEME;
#include "net-client.h"
#undef PACKET

}



bool net_term_clamp(byte win, byte *y, byte *x)
{
	stream_type* st_ptr;
	s16b nx = (*x);
	s16b ny = (*y);
	s16b xoff = 0;
	s16b yoff = 0;

	st_ptr = &streams[window_to_stream[win]];

	/* Shift expectations */
	if (st_ptr->addr == NTERM_WIN_OVERHEAD) 
	{
		yoff = SCREEN_CLIP_L;
		if (st_ptr->flag & SF_KEEP_X)	xoff += DUNGEON_OFFSET_X;
		if (st_ptr->flag & SF_KEEP_Y)	yoff += DUNGEON_OFFSET_Y;
	} 
	else 
	{
		if (st_ptr->flag & SF_KEEP_X)	xoff = SCREEN_CLIP_X;
		if (st_ptr->flag & SF_KEEP_Y)	yoff = SCREEN_CLIP_Y;
	}	

	/* Perform actual clamping */
	if (nx < st_ptr->min_col + xoff) nx = st_ptr->min_col + xoff; 
	if (nx > st_ptr->max_col + xoff) nx = st_ptr->max_col + xoff; 

	if (ny < st_ptr->min_row + yoff) ny = st_ptr->min_row + yoff; 
	if (ny > st_ptr->max_row + yoff) ny = st_ptr->max_row + yoff;

	/* Compare old and new values */
	if (nx != (*x) || ny != (*y))
	{
		/* Update */
		(*x) = (byte)nx;
		(*y) = (byte)ny;
		/* Return change */
		return TRUE;
	}
	/* Return no change */
	return FALSE;
}
/*
 * Manage Stream Subscriptions.
 * This function tests ALL active windows for changes.
 *  The changes are provided in form of 2 sets of flags - old and new.  It is caller's 
 *  responsibility to prepare those sets. An empty (zero-filled) array for old_flags is 
 *  acceptable. Both arrays shouldn't exceed ANGBAND_TERM_MAX. 
 *  If 'clear' is set to TRUE, each affected window is also cleared.
 * Returns a local window update mask.
 *  Applying it to p-ptr->window should redraw all affected windows.
 */
u32b net_term_manage(u32b* old_flag, u32b* new_flag, bool clear)
{
	int j;
	int k;

	/* Track changes */
	s16b st_y[MAX_STREAMS];
	s16b st_x[MAX_STREAMS];
	u32b st_flag = 0;

	/* Clear changes */
	for (j = 0; j < MAX_STREAMS; j++)
	{
		/* Default change is 'unchanged' */
		st_y[j] = -1;
		st_x[j] = -1;
	}

	/* Now, find actual changes by comparing old and new */ 
	for (j = 0; j < ANGBAND_TERM_MAX; j++)
	{
		term *old = Term;

		/* Dead window */
		if (!ang_term[j]) continue;

		/* Activate */
		Term_activate(ang_term[j]);

		/* Determine stream groups affected by this window */
		for (k = 0; k < stream_groups; k++) 
		{
			byte st = stream_group[k];
			stream_type* st_ptr = &streams[st];

			/* The stream is unchanged or turned off */
			if (st_y[st] <= 0)
			{
				/* It's now active */
				if ((new_flag[j] & st_ptr->window_flag))
				{
					/* It wasn't active or it's size changed. Subscribe! */
					if (!(old_flag[j] & st_ptr->window_flag) || Term->wid != p_ptr->stream_wid[st] || Term->hgt != p_ptr->stream_hgt[st]) 
					{
						st_y[st] = Term->hgt;
						st_x[st] = Term->wid;
					}
				}
				/* Trying to turn it off */
				else if ((old_flag[j] & st_ptr->window_flag))
				{
					st_y[st] = 0;
					st_x[st] = 0;
				}
			}
		}

		/* Ignore visible changes */
		if (clear)
		{
			/* Erase */ 
			Term_clear();

			/* Refresh */
			Term_fresh();
		}

		/* Restore */
		Term_activate(old);
	}

	/* Send subscriptions */
	for (j = 0; j < known_streams; j++) 
	{
		/* A change is scheduled */
		if (st_y[j] != -1) 
		{
			/* We try to subscribe/resize */
			if (st_y[j]) 
			{ 
				/* HACK -- Dungeon Display Offsets */
				if (streams[j].addr == NTERM_WIN_OVERHEAD)
				{
					/* Compact display */
					st_x[j] = st_x[j] - DUNGEON_OFFSET_X;
					/* Status and top line */
					st_y[j] = st_y[j] - SCREEN_CLIP_L - DUNGEON_OFFSET_Y;
				}

				/* Test bounds */
				if (st_x[j] < streams[j].min_col) st_x[j] = streams[j].min_col;
				if (st_x[j] > streams[j].max_col) st_x[j] = streams[j].max_col;
				if (st_y[j] < streams[j].min_row) st_y[j] = streams[j].min_row;
				if (st_y[j] > streams[j].max_row) st_y[j] = streams[j].max_row;
				/* If we changed nothing, bail out */
				if (st_x[j] == p_ptr->stream_wid[j] && st_y[j] == p_ptr->stream_hgt[j]) continue;
			}

			/* Send it! */
			send_stream_size(j, st_y[j], st_x[j]);

			/* Toggle update flag */
			st_flag |= streams[j].window_flag;
		}
	}

	/* Return update flags */
	return st_flag;
}
/* Helper caller for "net_term_manage" */
u32b net_term_update(bool clear) { return net_term_manage(window_flag, window_flag, clear); }