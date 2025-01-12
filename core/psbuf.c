/**
 *	@file    psbuf.c
 *	@version $Format:%h%d$
 *
 *      Implementation of API for handling buffers containing binary data.
 */
/*
 *	Copyright (c) 2016 INSIDE Secure Corporation
 *	All Rights Reserved
 *
 *	The latest version of this code is available at http://www.matrixssl.org
 *
 *	This software is open source; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This General Public License does NOT permit incorporating this software
 *	into proprietary programs.  If you are unable to comply with the GPL, a
 *	commercial license for this software may be purchased from INSIDE at
 *	http://www.insidesecure.com/
 *
 *	This program is distributed in WITHOUT ANY WARRANTY; without even the
 *	implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *	See the GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *	http://www.gnu.org/copyleft/gpl.html
 */
/******************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "coreApi.h"

/* Omit debug printouts. */
#define debugf(...) do { } while(0)

/* This address is indicator for static allocations pool, and does not
   need to point to any valid pool. The pool cannot be NULL, because
   NULL is reserved for "default memory pool". */
psPool_t * const psStaticAllocationsPool =
	(psPool_t *)&psStaticAllocationsPool;

void *psBufInit(psPool_t *pool, psBuf_t *buf, size_t capacity)
{
	buf->buf = psMalloc(pool, capacity);
	buf->start = buf->buf;
	buf->end = buf->start;
	buf->size = buf->buf ? capacity : 0;
	return buf->buf;
}

void psBufUninit(psPool_t *pool, psBuf_t *buf) {
	if (pool != psStaticAllocationsPool)
		psFree(buf->buf, pool);
	buf->buf = NULL;
	buf->start = NULL;
	buf->end = NULL;
	buf->size = 0;
}

void *psBufDetach(psPool_t *pool, psBuf_t *buf, size_t *len_p) {
	void * new;
	size_t len = buf->end - buf->start;
	new = psMalloc(pool, len ? len : 1);
	if (new) {
		memcpy(new, buf->start, len);
		*len_p = len;
	}
	psBufUninit(pool, buf);
	return new;
}

int32_t psBufFromData(psPool_t *pool, psBuf_t *buf, const void *data, size_t len)
{
	void *new = NULL;
	buf->buf = buf->start = buf->end = NULL;
	buf->size = 0;

	if (data != NULL) {
		new = psMalloc(pool, len ? len : 1);
		if (new != NULL) {
			buf->buf = buf->start = buf->end = new;
			buf->size = len;
			buf->end += len;
			memcpy(new, data, len);
		}
	}
	return new ? PS_SUCCESS : PS_MEM_FAIL;
}

static int32_t psBufFromStaticData(psBuf_t *buf, const void *data, size_t len)
{
	if (!data)
		len = 0;
	buf->buf = buf->start = buf->end = (void *) data;
	buf->size = len;
	buf->end += len;
	return data ? PS_SUCCESS : PS_ARG_FAIL;
}

char *psBufAsHex(psPool_t *pool, const psBuf_t *buf)
{
	char * hexstr;
	size_t len = buf->end - buf->start;
	hexstr = psMalloc(pool, len * 2 + 1);
	if (hexstr) {
		size_t i;
		hexstr[0] = 0;
		for(i = 0; i < len; i++)
			sprintf(hexstr + i * 2, "%02x", buf->start[i]);
	}
	return hexstr;
}

void *psBufAppendSize(psBuf_t *buf, size_t sz)
{
	unsigned char *loc = buf->end;
	unsigned char *bufend = buf->buf + buf->size;
	if (loc + sz <= bufend) {
		buf->end += sz;
	} else {
		loc = NULL;
	}
	return loc;
}

void psBufReservePrepend(psBuf_t *buf, size_t sz)
{
	unsigned char *bufend = buf->buf + buf->size;
	unsigned char *loc = buf->end;

	assert(buf->start == buf->end); /* Must be done at the beginning. */

	if (loc + sz <= bufend) {
		buf->start += sz;
		buf->end += sz;
	}
}

void *psBufPrependSize(psBuf_t *buf, size_t sz)
{
	if (buf->buf <= buf->start && buf->buf + sz <= buf->start) {
		buf->start -= sz;
		return buf->start;
	}

	return NULL;
}

void *psDynBufInit(psPool_t *pool, psDynBuf_t *db, size_t capacity)
{
	void *mem = psBufInit(pool, &db->buf, capacity);
	db->pool = pool;
	db->err = (mem == NULL);
	db->master = NULL;
	return mem;
}

void psDynBufUninit(psDynBuf_t *db)
{
	psBufUninit(db->pool, &db->buf);
	db->err = 0;
	db->pool = NULL;
	db->master = NULL;
}

void *psDynBufDetach(psDynBuf_t *db, size_t *len_p)
{
	void *new;

	if (db->err) {
		psDynBufUninit(db);
		return NULL;
	}

	new = psBufDetach(db->pool, &db->buf, len_p);
	db->pool = NULL;
	return new;
}

void assert_subbuf(psDynBuf_t *sub)
{
	const psDynBuf_t *db;
	/* Has master. */
	assert(sub->master != NULL);

	db = sub->master;

	/* Does not have pool */
	assert(sub->pool == NULL);

	/* The buf begin and is within master allocated data area. */
	assert(sub->buf.buf >= db->buf.start && sub->buf.buf <= db->buf.end);
	assert(sub->buf.buf + sub->buf.size >= db->buf.start &&
	       sub->buf.buf + sub->buf.size <= db->buf.end);

	/* For debugging: Mark head and tail visually. */
	memset(sub->buf.buf, '(', sub->buf.start - sub->buf.buf);
	memset(sub->buf.end, ')', sub->buf.buf + sub->buf.size - sub->buf.end);
}

static void *psDynBufGrow(psDynBuf_t *db, size_t head_sz, size_t tail_sz)
{
	void *alloc;
	void *loc;
	psBuf_t new;
	size_t headroom = db->buf.start - db->buf.buf;
	size_t tailroom = (db->buf.buf + db->buf.size) - db->buf.end;
	size_t filled = db->buf.end - db->buf.start;
	size_t offset;
	size_t offset_tail;

	if (db->err)
		return NULL;

	if (head_sz != 0 && head_sz < PS_DYNBUF_GROW)
		head_sz = PS_DYNBUF_GROW;
	if (tail_sz < PS_DYNBUF_GROW)
		tail_sz = PS_DYNBUF_GROW;

	if (db->master) {
		offset = db->buf.buf - db->master->buf.start;
		offset_tail = db->master->buf.end - (db->buf.buf + db->buf.size);
		debugf("Sub Grow: %zu+%zu+%zu => %zu+%zu+%zu; sub @ pos=%zd...-%zd\n",
		       headroom, filled, tailroom, headroom + head_sz, filled, tailroom + tail_sz, offset, offset_tail);
		assert_subbuf(db);

		/* For debugging: */
		memset(db->buf.buf, '{', headroom);
		memset(db->buf.end, '}', tailroom);

		loc = psDynBufGrow(db->master, 0, head_sz + tail_sz);
		if (loc) {
			db->master->buf.end += head_sz + tail_sz;
			if (offset_tail)
				memmove(db->master->buf.end - offset_tail,
					db->master->buf.end - offset_tail - head_sz - tail_sz,
					offset_tail);

			db->buf.buf = db->master->buf.start + offset;
			db->buf.start = db->buf.buf + headroom + head_sz;
			if (head_sz > 0)
				memmove(db->buf.start,
					db->buf.start - head_sz,
					filled);
			db->buf.end = db->buf.start + filled;
			db->buf.size = head_sz + headroom + filled +
				tailroom + tail_sz;

			debugf("Sub Grown: sub @ pos=%d, %zd bytes (%zd+%zd+%zd)\n",
			       (int) (db->buf.buf - db->master->buf.start),
			       db->buf.size, db->buf.start - db->buf.buf,
			       db->buf.end - db->buf.start, db->buf.buf + db->buf.size - db->buf.end);

			/* For debugging: */
			memset(db->buf.buf, '<', head_sz + headroom);
			memset(db->buf.end, '>', tail_sz + tailroom);
		} else {
			db->err++;
		}
		assert_subbuf(db);

		return loc;
	}

	head_sz += headroom;
	tail_sz += tailroom;

	debugf("Grow: %zu+%zu+%zu => %zu+%zu+%zu\n",
	       headroom, filled, tailroom, head_sz, filled, tail_sz);

	alloc = psBufInit(db->pool, &new, head_sz + filled + tail_sz);
	if (alloc) {
		psBufReservePrepend(&new, head_sz);
		loc = psBufAppendSize(&new, filled);
		/* Just allocated so there is space. */
		assert(loc != NULL);
		memcpy(loc, db->buf.start, filled);
		psBufUninit(db->pool, &db->buf);
		db->buf.buf = new.buf;
		db->buf.start = new.start;
		db->buf.end = new.end;
		db->buf.size = new.size;
	} else {
		db->err++;
		loc = NULL;
	}

	return loc;
}

void *psDynBufAppendSize(psDynBuf_t *db, size_t sz)
{
	unsigned char *loc = psBufAppendSize(&db->buf, sz);
	if (loc == NULL)
		if (psDynBufGrow(db, 0, sz)) {
			loc = psBufAppendSize(&db->buf, sz);
			assert(loc != NULL);
		}
	return loc;
}

void *psDynBufAppendUtf8(psDynBuf_t *db, int chr)
{
	unsigned char *enc;
	unsigned int ch = (unsigned int) chr;
	/* Do not encode characters outside valid UTF-8 range. */
	if (ch > 0x1FFFF) {
		db->err++;
		return NULL;
	}
	if (ch < 128) {
		enc = psDynBufAppendSize(db, 1);
		if (enc)
			*enc = (unsigned char) ch;
	} else if (ch <= 0x7FF) {
		/* Two byte encoding. */
		enc = psDynBufAppendSize(db, 2);
		if (enc) {
			enc[0] = (unsigned char) (0xC0 | (ch >> 6));
			enc[1] = (unsigned char) (0x80 | (ch & 63));
		}
	} else if (ch <= 0xffff) {
		/* Three byte encoding. */
		enc = psDynBufAppendSize(db, 3);
		if (enc) {
			enc[0] = (unsigned char) (0xE0 | (ch >> 12));
			enc[1] = (unsigned char) (0x80 | ((ch >> 6) & 63));
			enc[2] = (unsigned char) (0x80 | (ch & 63));
		}
	} else {
		/* Four byte encoding. */
		enc = psDynBufAppendSize(db, 4);
		if (enc) {
			enc[0] = (unsigned char) (0xF0 | (ch >> 18));
			enc[1] = (unsigned char) (0x80 | ((ch >> 12) & 63));
			enc[2] = (unsigned char) (0x80 | ((ch >> 6) & 63));
			enc[3] = (unsigned char) (0x80 | (ch & 63));
		}
	}
	return enc;
}


void psDynBufReservePrepend(psDynBuf_t *db, size_t sz)
{
	/* This function only performs action if nothing has been pushed.
	   The logic for prepending will make sure prepend succeeds even
	   if there is no head room. */
	if (db->buf.start == db->buf.end)
		psBufReservePrepend(&db->buf, sz);
}

void *psDynBufPrependSize(psDynBuf_t *db, size_t sz)
{
	unsigned char *loc = psBufPrependSize(&db->buf, sz);
	if (loc == NULL)
		if (psDynBufGrow(db, sz, 0)) {
			loc = psBufPrependSize(&db->buf, sz);
			assert(loc != NULL);
		}
	return loc;
}

void *psDynBufSubInit(psDynBuf_t *db, psDynBuf_t *sub, size_t capacity)
{
	void *mem = psDynBufAppendSize(db, capacity);
	if (mem) {
		sub->buf.buf = db->buf.end - capacity;
		sub->buf.start = sub->buf.buf;
		sub->buf.end = sub->buf.buf;
		sub->buf.size = capacity;
		sub->pool = NULL;
		sub->master = db;
		sub->err = 0;
		memset(sub->buf.buf, '#', capacity);
		assert_subbuf(sub);
	} else {
		sub->buf.buf = sub->buf.start = sub->buf.end = NULL;
		sub->buf.size = 0;
		sub->pool = NULL;
		sub->err = 1;
		db->err++;
		sub->master = db;
	}

	return mem;
}

void *psDynBufSubInitAt(psDynBuf_t *db, psDynBuf_t *sub, size_t at,
			size_t length)
{
	size_t len = db->buf.end - db->buf.start;
	if (db->err == 0 && at + length <= len) {
		sub->buf.buf = db->buf.start + at;
		sub->buf.start = sub->buf.buf;
		sub->buf.end = sub->buf.buf;
		sub->buf.size = length;
		sub->pool = NULL;
		sub->master = db;
		sub->err = 0;
		memset(sub->buf.buf, '#', length);
		assert_subbuf(sub);
	} else {
		sub->buf.buf = sub->buf.start = sub->buf.end = NULL;
		sub->buf.size = 0;
		sub->pool = NULL;
		sub->err++;
		db->err++;
		sub->master = db;
	}

	return sub->buf.buf;
}

void *psDynBufSubFinish(psDynBuf_t *sub)
{
	void *loc = NULL;
	psDynBuf_t *db = sub->master;
	if (sub->err) {
		db->err += sub->err;
	} else {
		size_t total = sub->buf.size;
		size_t filled = sub->buf.end - sub->buf.start;
		size_t offset_tail;

		offset_tail = db->buf.end - (sub->buf.buf + sub->buf.size);

		assert_subbuf(sub);
		if (sub->buf.buf != sub->buf.start && filled > 0) {
			memmove(sub->buf.buf, sub->buf.start, filled);
		}
		if (offset_tail > 0) {
			memmove(db->buf.end - total + filled - offset_tail,
				db->buf.end - offset_tail, offset_tail);
		}
		db->buf.end -= total;
		db->buf.end += filled;
		loc = sub->buf.buf;
	}
	sub->buf.buf = NULL;
	psDynBufUninit(sub);
	return loc;
}

static size_t len_of_tag_and_len(size_t len)
{
	size_t lentaglen;
	if (len < 128)
		lentaglen = 2;
	else if (len < 256)
		lentaglen = 3;
	else if (len < 65536)
		lentaglen = 4;
	else if (len < 16777216)
		lentaglen = 5;
	else
		lentaglen = 6; /* Supports up-to 32-bit sizes. */
	return lentaglen;
}

static void output_len(size_t len, unsigned char *target)
{
	if (len < 128)
		target[1] = (unsigned char) len;
	else if (len < 256) {
		target[1] = 0x81;
		target[2] = (unsigned char) len;
	} else if (len < 65536) {
		target[1] = 0x82;
		target[2] = (unsigned char) (len >> 8);
		target[3] = (unsigned char) len;
	} else if (len < 16777216) {
		target[1] = 0x83;
		target[2] = (unsigned char) (len >> 16);
		target[3] = (unsigned char) (len >> 8);
		target[4] = (unsigned char) len;
	} else {
		target[1] = 0x84;
		target[2] = (unsigned char) (len >> 24);
		target[3] = (unsigned char) (len >> 16);
		target[4] = (unsigned char) (len >> 8);
		target[5] = (unsigned char) len;
	}
}

char *psDynBufAppendAsn1TagGen(psDynBuf_t *db, unsigned char tag,
			       const unsigned char *bytes, size_t len)
{
	size_t extralen = len_of_tag_and_len(len);
	unsigned char *target = psDynBufAppendSize(db, len + extralen);
	if (target) {
		target[0] = tag;
		output_len(len, target);
		memcpy(target + extralen, bytes, len);
	}
	return (char *)target;
}

char *psDynBufBeginConstructedTag(psDynBuf_t *db, psDynBuf_t *sub)
{
	char *target = psDynBufSubInit(db, sub, 20);
	if (target) {
		psDynBufReservePrepend(sub, 4);
	}
	return target;
}

char *psDynBufEndConstructedTag(psDynBuf_t *sub, unsigned char tag)
{
	size_t len = sub->buf.end - sub->buf.start;
	size_t extralen = len_of_tag_and_len(len);
	unsigned char *target = psDynBufPrependSize(sub, extralen);
	if (target) {
		target[0] = tag;
		output_len(len, target);
	}
	psDynBufSubFinish(sub);
	return (char*)target;
}

int32_t psParseBufFromStaticData(psParseBuf_t *pb, const void *data, size_t len)
{
	int32_t rc = psBufFromStaticData(&pb->buf, data, len);
	pb->pool = psStaticAllocationsPool;
	pb->err = rc != PS_SUCCESS;
	pb->master = NULL;
	return rc;
}

/* The maximum supported PS_PARSE_MAXIMUM_TAG_CONTENT: */
#define PS_PARSE_MAXIMUM_TAG_CONTENT 0x40000000U /* 1 gigabyte. */
/* Note: If the value needs to be extended, the limit for function
   psParseBufGetTagLen() on 32-bit hosts is 0xFFFFFFFFU - 5. */
size_t psParseBufGetTagLen(const psParseBuf_t *pb,
			   unsigned char tag,
			   size_t *hdrLen_p)
{
	unsigned char lenlen;
	size_t len_at = 1;
	size_t len_hdr = 2;
	size_t len_content;
	size_t len_out;
	const unsigned char *ptr = pb->buf.start;
	size_t bytes = pb->buf.end - pb->buf.start;

	if (bytes < 2)
		return 0;
	if (tag != 0 && ptr[0] != tag)
		return 0;

	/* Check size tag: long input lengths. */
	lenlen = ptr[1]; /* Use lenlen temporarily to parse length field. */
	if (lenlen >= 0x80) {
		if (bytes < 0x83)
			return 0;

		if (lenlen == 0x81 && ptr[2] < 0x80)
			return 0;

		if (lenlen == 0x82 && ptr[2] == 0x00)
			return 0;

		if (lenlen == 0x83 && ptr[2] == 0x00)
			return 0;

		if (lenlen == 0x84 && ptr[2] == 0x00)
			return 0;

		if (lenlen == 0x80 || lenlen > 0x84)
			return 0; /* Too large or indefinite len. */
		len_at++;
		lenlen -= 0x80;
		len_hdr += lenlen;
	} else {
		lenlen = 1;
	}

	/* len_at and lenlen now express length and of the len. */
	/* additionally, its known that all the length bytes are accessible. */
	len_content = 0;
	while(lenlen) {
		len_content <<= 8;
		len_content += ptr[len_at];
		len_at++;
		lenlen--;
	}

	if (len_content > PS_PARSE_MAXIMUM_TAG_CONTENT)
		return 0;

	len_out = len_content + len_hdr;

	if (len_out > bytes)
		return 0;

	if (hdrLen_p)
		*hdrLen_p = len_hdr;

	return len_out;
}

int psParseBufCanGetTag(const psParseBuf_t *pb, unsigned char tag)
{
	return psParseBufGetTagLen(pb, tag, NULL) > 0;
}

size_t psParseBufTrySkipBytes(psParseBuf_t *pb,
			      const unsigned char *bytes,
			      size_t numbytes)
{
	size_t skip_bytes = 0;
	if (psParseCanRead(pb, numbytes) &&
	    memcmp(bytes, pb->buf.start, numbytes) == 0)
		skip_bytes = numbytes;
	pb->buf.start += skip_bytes;
	return skip_bytes;
}

size_t psParseBufSkipBytes(psParseBuf_t *pb, const unsigned char *bytes,
			    size_t numbytes)
{
	size_t sz = psParseBufTrySkipBytes(pb, bytes, numbytes);
	if (sz == 0)
		pb->err ++;
	return sz;
}

size_t psParseBufTryReadTagSub(const psParseBuf_t *pb,
			       psParseBuf_t *content, unsigned char tag)
{
	size_t hdrlen;
	size_t len = psParseBufGetTagLen(pb, tag, &hdrlen);
	size_t len_content;
	psParseBuf_t content_tmp; /* Allows calling the function with
				     content == NULL to skip the tag. */

	if (!content)
		content = & content_tmp;
	if (!len) {
		content->buf.buf = content->buf.start = content->buf.end = NULL;
		content->buf.size = 0;
		content->pool = NULL;
		/* Mark state of content as error. */
		content->err = 1;
		/* Do not raise error status of main pb. */
		content->master = (psParseBuf_t *)pb;
		return 0;
	}

	len_content = len - hdrlen;
	content->buf.start = content->buf.buf = pb->buf.start + hdrlen;
	content->buf.size = len_content;
	content->buf.end = content->buf.start + len_content;
	content->pool = NULL;
	content->master = (psParseBuf_t *)pb;
	content->err = 0;

	return len;
}

size_t psParseBufReadTagSub(psParseBuf_t *pb,
			    psParseBuf_t *content, unsigned char tag)
{
	size_t len = psParseBufTryReadTagSub(pb, content, tag);
	if (len == 0) {
		/* Mark this also as an error in main parse buffer. */
		pb->err++;

		/* Initialize sub as the same memory than main parse buffer,
		   to allow parsing using it (typically unsuccessfully)
		   in following parsing operations. */
		memcpy(&(content->buf), &(pb->buf), sizeof content->buf);
	}
	return len;
}

size_t psParseBufReadTagRef(psParseBuf_t *pb,
			    psBuf_t *ref, unsigned char tag)
{
	psParseBuf_t content;
	size_t len = psParseBufReadTagSub(pb, &content, tag);
	if (len) {
		memcpy(ref, &content.buf, sizeof(psBuf_t));
		pb->buf.start += len;
	}
	return len;
}

size_t psParseBufTrySkipTag(psParseBuf_t *pb, unsigned char tag)
{
	psParseBuf_t sub;
	size_t sz = psParseBufTryReadTagSub(pb, &sub, tag);
	if (sz)
		(void) psParseBufFinish(&sub);
	return sz;
}

size_t psParseBufSkipTag(psParseBuf_t *pb, unsigned char tag)
{
	psParseBuf_t sub;
	size_t sz = psParseBufReadTagSub(pb, &sub, tag);
	if (sz)
		(void) psParseBufFinish(&sub);
	return sz;
}

int32_t psParseBufCopyAll(const psParseBuf_t *pb, unsigned char *target,
			  size_t *targetlen)
{
	size_t len = pb->buf.end - pb->buf.start;

	if (pb->err != 0)
		return PS_FAILURE;

	if (target == NULL) {
		*targetlen = len;
		return PS_OUTPUT_LENGTH;
	}

	if (len > *targetlen) {
		*targetlen = len;
		return PS_OUTPUT_LENGTH;
	}

	memcpy(target, pb->buf.start, len);
	return PS_SUCCESS;
}

int psParseBufEq(const psParseBuf_t *pb1, const psParseBuf_t *pb2)
{
	if (pb1->err || pb2->err)
		return 0;

	return psBufEq(&pb1->buf, &pb2->buf);
}

int32_t psParseBufCheckState(const psParseBuf_t *pb)
{
	return pb->err == 0 ? PS_SUCCESS : PS_FAILURE;
}

int32_t psParseBufFinish(psParseBuf_t *pb)
{
	int32_t rc;
	if (pb->master) {
		if (pb->err)
			/* Signal master on error. */
			pb->master->err++;
		else
			/* Advance master. */
			pb->master->buf.start = pb->buf.buf + pb->buf.size;

		pb->buf.buf = NULL; /* Do not free any data. */
	}

	/* Free state. */
	rc = psParseBufCheckState(pb);
	psBufUninit(pb->pool, &pb->buf);
	pb->master = NULL;
	pb->err = 0;
	pb->pool = NULL;
	return rc;
}

void psParseBufCancel(psParseBuf_t *pb)
{
	/* Free state. */
	if (pb->master)
		pb->buf.buf = NULL; /* Do not free any data. */
	psBufUninit(pb->pool, &pb->buf);
	pb->master = NULL;
	pb->err = 0;
	pb->pool = NULL;
}

/* end of file psbuf.c */
