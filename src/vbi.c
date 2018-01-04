#include <stdio.h>
#include <libzvbi.h>
#include <string.h>
#include "dtvcc.h"

static FILE *fp;
static vbi_decoder *dec;

struct vbi_data_s {
	unsigned int vbi_type:8;
	unsigned int field_id:8;
	unsigned int tt_sys:8;/*tt*/
	unsigned int nbytes:16;
	unsigned int line_num:16;
	unsigned char b[42];         /* 42 for TT-625B */
};

static void draw_page (vbi_page *p)
{
	vbi_char *ac;
	int       i, j;

	printf("cc %d:\n", p->pgno);

	for (i = 0; i < p->rows; i++) {
		for (j = 0; j < p->columns; j++) {
			ac = &p->text[i * p->columns + j];
			printf("%c", ac->unicode & 0xff);
		}
		printf("\n");
	}
}

static void evt_handler (vbi_event *event, void *user_data)
{
	if (event->type == VBI_EVENT_CAPTION) {
		int pgno = event->ev.caption.pgno;
		vbi_page page;

		//printf("cc page %d\n", pgno);

		if (vbi_fetch_cc_page(dec, &page, pgno, 0)) {
			draw_page(&page);
		}
	}
}

int main (int argc, char **argv)
{
	struct vbi_data_s vbi;
	int r;

	if (argc < 2) {
		fprintf(stderr, "need filename\n");
		return 1;
	}

	fp = fopen(argv[1], "rb");
	if (!fp) {
		fprintf(stderr, "cannot open \"%s\"\n", argv[1]);
		return 1;
	}

	dec = vbi_decoder_new();
	printf("size:%lu \n", sizeof(vbi));

	vbi_event_handler_register(dec, VBI_EVENT_CAPTION, evt_handler, NULL);

	while (1) {
		vbi_sliced slice;
		int i;

		r = fread(&vbi, 1, sizeof(vbi), fp);
		if (r != sizeof(vbi))
			break;

		//if (vbi.line_num == 334)
		//	vbi.line_num = 21;

		/*
		printf("type:%d, field:%d line_num:%d\n", vbi.vbi_type, vbi.field_id, vbi.line_num);

		for (i = 0; i < vbi.nbytes; i ++) {
			printf("%02x ", vbi.b[i]);
		}
		printf("\n");*/

		slice.id    = VBI_SLICED_CAPTION_525|VBI_SLICED_CAPTION_625;
		slice.line  = vbi.line_num;
		memcpy(&slice.data, vbi.b, 42);

		//vbi_decode(dec, &slice, 1, 0);
		vbi_decode_caption(dec, vbi.line_num, slice.data);
	}

	vbi_decoder_delete(dec);

	fclose(fp);
	return 0;
}

