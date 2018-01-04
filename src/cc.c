#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <ctype.h>
#include <assert.h>
#include <libzvbi.h>
#include "dtvcc.h"
#include "list.h"

typedef enum {
	PIC_I = 1,
	PIC_P = 2,
	PIC_B = 3
} PicType;

struct CCData{
	struct list_head list;
	PicType ptype;
	int     ref;
	uint8_t data[16*1024];
	size_t  len;
};

static struct tvcc_decoder cc_dec;
static FILE    *fp;
static uint8_t *pes_buf;
static size_t   pes_len = 0;
static size_t   pes_cap = 0;
static struct CCData   cc_list;

static void display_cc()
{
	struct CCData *node;
	struct list_head *pos, *other;

	list_for_each_safe(pos, other, &cc_list.list) {
		node = list_entry(pos, struct CCData, list);
		tvcc_decode_data(&cc_dec, 0, node->data, node->len);
		list_del(pos);
		free(node);
	}
}

static void add_cc_data(struct CCData *cc)
{
	struct CCData *d, *node;
	struct list_head *pos;

	if (cc->ptype == PIC_I) {
		display_cc();
	}
	d = malloc(sizeof(struct CCData));
	memcpy(d, cc, sizeof(struct CCData));
	memcpy(d->data, cc->data, sizeof(cc->data));
	if (list_empty(&cc_list.list)) {
		list_add_tail(&d->list, &cc_list.list);
	} else {
		list_for_each(pos, &cc_list.list) {
			node = list_entry(pos, struct CCData, list);
			if (node->ref > d->ref) {
				list_add_tail(&d->list, &node->list);
				break;
			}
		}
		if (node->ref <= d->ref) {
			list_add_tail(&d->list, &cc_list.list);
		}
	}
}

static int seek_header(uint8_t **pp, size_t *pleft)
{
	uint8_t *p    = *pp;
	size_t   left = *pleft;

	while (left > 3) {
		if ((p[0] == 0) && (p[1] == 0) && (p[2] == 1)) {
			//if ((p[3] == 0xb3) || (p[3] == 0xb8) || (p[3] == 0))
			//	printf("start: %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3]);

			*pp    = p;
			*pleft = left;
			return 0;
		}

		p ++;
		left --;
	}

	return -1;
}

static void cc_data(uint8_t *p,
		           size_t   len,
				   struct CCData  *cc)
{
	uint8_t *pd;

	pd = &cc->data[cc->len];
	memcpy(pd, p, len);

	cc->len += len;
}

static void user_data(uint8_t *p,
		             size_t   len,
					 struct CCData  *cc)
{
	p   += 4;
	len -= 4;

	if (len < 5)
		return;

	if ((p[0] != 0x47) || (p[1] != 0x41) || (p[2] != 0x39) || (p[3] != 0x34))
		return;

	if (p[4] != 0x03)
		return;
	//if(p[5] != 0xc2)
		//return;
	cc_data(p + 4, len - 4, cc);
}

static void pic_data(uint8_t *p,
		            size_t   len,
					struct CCData *cc)
{
	int ref;
	int ptype;

	p   += 4;
	len -= 4;

	if (len < 2)
		return;

	ref   = ((p[1] >> 6) & 3) | (p[0] << 2);
	ptype = (p[1] >> 3) & 7;

	//printf("pic: %d %d\n", ref, ptype);
	cc->ref   = ref;
	cc->ptype = ptype;
}

static void pes_packet()
{
	uint8_t *p     = pes_buf;
	size_t   left  = pes_len;
	uint8_t *np    = p + 4;
	size_t   nleft = left - 4;
	int      r;
	struct CCData   cc;

	//printf("pes:\n");
	
	cc.len = 0;

	while (1) {
		size_t clen;

		r = seek_header(&np, &nleft);
		if (r < 0) {
			clen = left;
		} else {
			clen = np - p;
		}

		if (p[3] == 0xb2) {
			user_data(p, clen, &cc);
		} else if (p[3] == 0) {
			pic_data(p, clen, &cc);
		}

		if (r < 0)
			break;

		p     =  np;
		left  =  nleft;
		np    += 4;
		nleft -= 4;
	}

	add_cc_data(&cc);
}

static void parse_ts_packet(uint8_t *p, int input_pid)
{
	uint8_t  *pl;
	ssize_t   left;
	uint16_t  pid;
	int       start;
	int       afc;

	pid = ((p[1] << 8) | p[2]) & 0x1fff;
	if (pid != input_pid)
	//if (pid != 33)
	//if (pid != 481)
		return;

	start = p[1] & 0x40;
	afc   = (p[3] >> 4) & 3;

	if (start) {
		if (pes_len)
			pes_packet();

		pes_len = 0;
	}

	if (!(afc & 1))
		return;

	pl   = p + 4;
	left = 184;

	if (afc & 2) {
		uint8_t alen = pl[0];

		pl   += alen + 1;
		left -= alen + 1;
	}

	if (left < 0)
		return;

	if (pes_len + left >= pes_cap) {
		size_t size = pes_len + left;

		pes_buf = realloc(pes_buf, size);
		pes_cap = size;
	}

	memcpy(pes_buf + pes_len, pl, left);
	pes_len += left;
}

static void ts_packet(int pid)
{
	uint8_t buf[188];
	int     c, r;

	do {
		c = fgetc(fp);
	} while ((c != 0x47) && (c != EOF));

	if (c == EOF)
		return;

	buf[0] = 0x47;

	r = fread(buf + 1, 1, 187, fp);
	if (r != 187)
		return;

	parse_ts_packet(buf, pid);
}

static void draw_page(vbi_page *p)
{
	vbi_char *ac;
	int       i, j;
	int       empty = 1;

	for (i = 0; i < p->rows; i++) {
		for (j = 0; j < p->columns; j++) {
			ac = &p->text[i * p->columns + j];
			if (!isspace(ac->unicode & 0xff)) {
				empty = 0;
				break;
			}
		}
	}

	if (empty)
		return;

	for (i = 0; i < p->rows; i++) {
		for (j = 0; j < p->columns; j++) {
			ac = &p->text[i * p->columns + j];
			printf("%c", ac->unicode & 0xff);
		}
		printf("\n");
	}

	//exit(0);
}

static void vbi_evt_handler(vbi_event *event, 
		                   void *user_data)
{
	if (event->type == VBI_EVENT_CAPTION) {
		vbi_page pages[8];
		int      cnt  = 8;
		int      pgno = event->ev.caption.pgno;

		//if (pgno == 9) {
		if (pgno == 1) {
			int i;

			tvcc_fetch_page(&cc_dec, pgno, &cnt, pages);

			if (cnt) {
				for (i = 0; i < cnt; i ++) {
					printf("cc %d:\n", i);

					draw_page(&pages[i]);
				}
			} else {
				printf("cc\n");
			}
		}
	}
}

int main(int argc, char **argv)
{
	int times = 1, pid;
	struct CCData *node = NULL;
	struct list_head *pos, *other;
	char *name;


	if (argc < 3) {
		printf("usage: cc file_name pid\n");
		return 0;
	}
	name = argv[1];
	sscanf(argv[2], "%d", &pid);
	printf("file->%s, pid->%d\n", name, pid);

	fp = fopen(name, "rb");
	if (!fp) {
		printf("open %s failed, %s\n", name, strerror(errno));
		return 0;
	}

	tvcc_init(&cc_dec);

	INIT_LIST_HEAD(&cc_list.list);

	vbi_event_handler_add(cc_dec.vbi,
			VBI_EVENT_CAPTION,
			vbi_evt_handler,
			NULL);

	while (1) {
		ts_packet(pid);
		if (feof(fp)) {
			times --;
			if (!times)
				break;

			fseek(fp, 0, SEEK_SET);
		}
	}

	fclose(fp);

	if (pes_buf)
		free(pes_buf);

	tvcc_destroy(&cc_dec);
	list_for_each_safe(pos, other, &cc_list.list) {
		node = list_entry(pos, struct CCData, list);
		list_del(pos);
		free(node);
		node = NULL;
	}

	
	return 0;
}

