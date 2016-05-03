/* GStreamer
 * Copyright (C) 2009 Wim Taymans <wim.taymans@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-rtpj2kpay
 *
 * Payload encode JPEG 2000 pictures into RTP packets according to RFC 5371.
 * For detailed information see: http://www.rfc-editor.org/rfc/rfc5371.txt
 *
 * The payloader takes a JPEG 2000 picture, scans the header for packetization
 * units and constructs the RTP packet header followed by the actual JPEG 2000
 * codestream.
 */

#define RTP_J2K_SIM


#include "rtp-sim.h"
#include "gstrtpj2kpaydepay.h"
#include "gstrtpj2kpay.h"

typedef struct
{
	guint tp : 2;
	guint MHF : 2;
	guint mh_id : 3;
	guint T : 1;
	guint priority : 8;
	guint tile : 16;
	guint offset : 24;
} RtpJ2KHeader;


static GstRtpJ2KPay* GST_RTP_J2K_PAY(GstRTPBasePayload* base_pay) {
	return (GstRtpJ2KPay*)base_pay;
}


static guint gst_rtp_j2k_pay_header_size (const guint8 * data, guint offset)
{
  return data[offset] << 8 | data[offset + 1];
}

static RtpJ2KMarker gst_rtp_j2k_pay_scan_marker (const guint8 * data, guint size, guint * offset)
{
  while ((data[(*offset)++] != J2K_MARKER) && ((*offset) < size));

  if (G_UNLIKELY ((*offset) >= size)) {
    return J2K_MARKER_EOC;
  } else {
    guint8 marker = data[(*offset)++];
    return (RtpJ2KMarker)marker;
  }
}

typedef struct
{
  RtpJ2KHeader header;
  gboolean bitstream;
  guint n_tiles;
  guint next_sot;
  gboolean force_packet;
} RtpJ2KState;

static guint find_pu_end (GstRtpJ2KPay * pay, const guint8 * data, guint size,
							guint offset, RtpJ2KState * state)
{
  gboolean cut_sop = FALSE;
  RtpJ2KMarker marker;

  /* parse the j2k header for 'start of codestream' */
  GST_LOG_OBJECT (pay, "checking from offset %u", offset);
  while (offset < size) {
    marker = gst_rtp_j2k_pay_scan_marker (data, size, &offset);

    if (state->bitstream) {
      /* parsing bitstream, only look for SOP */
      switch (marker) {
        case J2K_MARKER_SOP:
          GST_LOG_OBJECT (pay, "found SOP at %u", offset);
          if (cut_sop)
            return offset - 2;
          cut_sop = TRUE;
          break;
        case J2K_MARKER_EPH:
          /* just skip over EPH */
          GST_LOG_OBJECT (pay, "found EPH at %u", offset);
          break;
        default:
          if (offset >= state->next_sot) {
            GST_LOG_OBJECT (pay, "reached next SOT at %u", offset);
            state->bitstream = FALSE;
            state->force_packet = TRUE;
            if (marker == J2K_MARKER_EOC && state->next_sot + 2 <= size)
              /* include EOC but never go past the max size */
              return state->next_sot + 2;
            else
              return state->next_sot;
          }
          break;
      }
    } else {
      switch (marker) {
        case J2K_MARKER_SOC:
          GST_LOG_OBJECT (pay, "found SOC at %u", offset);
          state->header.MHF = 1;
          break;
        case J2K_MARKER_SOT:
        {
          guint len, Psot;

          GST_LOG_OBJECT (pay, "found SOT at %u", offset);
          /* we found SOT but also had a header first */
          if (state->header.MHF) {
            state->force_packet = TRUE;
            return offset - 2;
          }

          /* parse SOT but do some sanity checks first */
          len = gst_rtp_j2k_pay_header_size (data, offset);
          GST_LOG_OBJECT (pay, "SOT length %u", len);
          if (len < 8)
            return size;
          if (offset + len >= size)
            return size;

          if (state->n_tiles == 0)
            /* first tile, T is valid */
            state->header.T = 0;
          else
            /* more tiles, T becomes invalid */
            state->header.T = 1;
          state->header.tile = GST_READ_UINT16_BE (&data[offset + 2]);
          state->n_tiles++;

          /* get offset of next tile, if it's 0, it goes all the way to the end of
           * the data */
          Psot = GST_READ_UINT32_BE (&data[offset + 4]);
          if (Psot == 0)
            state->next_sot = size;
          else
            state->next_sot = offset - 2 + Psot;

          offset += len;
          GST_LOG_OBJECT (pay, "Isot %u, Psot %u, next %u", state->header.tile,
              Psot, state->next_sot);
          break;
        }
        case J2K_MARKER_SOD:
          GST_LOG_OBJECT (pay, "found SOD at %u", offset);
          /* can't have more tiles now */
          state->n_tiles = 0;
          /* go to bitstream parsing */
          state->bitstream = TRUE;
          /* cut at the next SOP or else include all data */
          cut_sop = TRUE;
          /* force a new packet when we see SOP, this can be optional but the
           * spec recommends packing headers separately */
          state->force_packet = TRUE;
          break;
        case J2K_MARKER_EOC:
          GST_LOG_OBJECT (pay, "found EOC at %u", offset);
          return offset;
        default:
        {
          guint len = gst_rtp_j2k_pay_header_size (data, offset);
          GST_LOG_OBJECT (pay, "skip 0x%02x len %u", marker, len);
          offset += len;
          break;
        }
      }
    }
  }
  GST_DEBUG_OBJECT (pay, "reached end of data");
  return size;
}

static GstFlowReturn gst_rtp_j2k_pay_handle_buffer (GstRTPBasePayload * basepayload,
													 GstBuffer * buffer)
{
  GstRtpJ2KPay *pay;
  GstClockTime timestamp;
  GstFlowReturn ret = GST_FLOW_ERROR;
  RtpJ2KState state;
  GstBufferList *list = NULL;
  GstMapInfo map;
  guint mtu, max_size;
  guint offset;
  guint end, pos;

  pay = GST_RTP_J2K_PAY (basepayload);
  mtu = GST_RTP_BASE_PAYLOAD_MTU (pay);

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  timestamp = GST_BUFFER_PTS (buffer);
  offset = pos = end = 0;

  GST_LOG_OBJECT (pay,
      "got buffer size %llu, timestamp %llu",
      map.size, GST_TIME_ARGS (timestamp));

  /* do some header defaults first */
  state.header.tp = 0;          /* only progressive scan */
  state.header.MHF = 0;         /* no header */
  state.header.mh_id = 0;       /* always 0 for now */
  state.header.T = 1;           /* invalid tile */
  state.header.priority = 255;  /* always 255 for now */
  state.header.tile = 0;        /* no tile number */
  state.header.offset = 0;      /* offset of 0 */
  state.bitstream = FALSE;
  state.n_tiles = 0;
  state.next_sot = 0;
  state.force_packet = FALSE;

  /* get max packet length */
  max_size = gst_rtp_buffer_calc_payload_len (mtu - HEADER_SIZE, 0, 0);

  list = gst_buffer_list_new_sized ((mtu / max_size) + 1);

  do {
    GstBuffer *outbuf;
    guint8 *header;
    guint payload_size;
    guint pu_size;
#ifdef RTP_J2K_SIM
	GstRTPBuffer rtp;
#else
    GstRTPBuffer rtp = { NULL };
#endif

    /* try to pack as much as we can */
    do {
      /* see how much we have scanned already */
      pu_size = end - offset;
      GST_DEBUG_OBJECT (pay, "scanned pu size %u", pu_size);

      /* we need to make a new packet */
      if (state.force_packet) {
        GST_DEBUG_OBJECT (pay, "need to force a new packet");
        state.force_packet = FALSE;
        pos = end;
        break;
      }

      /* else see if we have enough */
      if (pu_size > max_size) {
        if (pos != offset)
          /* the packet became too large, use previous scanpos */
          pu_size = pos - offset;
        else
          /* the already scanned data was already too big, make sure we start
           * scanning from the last searched position */
          pos = end;

        GST_DEBUG_OBJECT (pay, "max size exceeded pu_size %u", pu_size);
        break;
      }

      pos = end;

      /* exit when finished */
      if (pos == map.size)
        break;

      /* scan next packetization unit and fill in the header */
      end = find_pu_end (pay, map.data, (guint)map.size, pos, &state);
    } while (TRUE);

    while (pu_size > 0) {
      guint packet_size, data_size;
      GstBuffer *paybuf;

      /* calculate the packet size */
      packet_size =
          gst_rtp_buffer_calc_packet_len (pu_size + HEADER_SIZE, 0, 0);

      if (packet_size > mtu) {
        GST_DEBUG_OBJECT (pay, "needed packet size %u clamped to MTU %u",
            packet_size, mtu);
        packet_size = mtu;
      } else {
        GST_DEBUG_OBJECT (pay, "needed packet size %u fits in MTU %u",
            packet_size, mtu);
      }

      /* get total payload size and data size */
      payload_size = gst_rtp_buffer_calc_payload_len (packet_size, 0, 0);
      data_size = payload_size - HEADER_SIZE;

      /* make buffer for header */
      outbuf = gst_rtp_buffer_new_allocate (HEADER_SIZE, 0, 0);

      GST_BUFFER_PTS (outbuf) = timestamp;

      gst_rtp_buffer_map (outbuf, GST_MAP_WRITE, &rtp);

      /* get pointer to header */
      header = gst_rtp_buffer_get_payload (&rtp);

      pu_size -= data_size;
      if (pu_size == 0) {
        /* reached the end of a packetization unit */
        if (state.header.MHF) {
          /* we were doing a header, see if all fit in one packet or if
           * we had to fragment it */
          if (offset == 0)
            state.header.MHF = 3;
          else
            state.header.MHF = 2;
        }
        if (end >= map.size)
          gst_rtp_buffer_set_marker (&rtp, TRUE);
      }

      /*
       * RtpJ2KHeader:
       * @tp: type (0 progressive, 1 odd field, 2 even field)
       * @MHF: Main Header Flag
       * @mh_id: Main Header Identification
       * @T: Tile field invalidation flag
       * @priority: priority
       * @tile number: the tile number of the payload
       * @reserved: set to 0
       * @fragment offset: the byte offset of the current payload
       *
       *  0                   1                   2                   3
       *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       * |tp |MHF|mh_id|T|     priority  |           tile number         |
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       * |reserved       |             fragment offset                   |
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       */
      header[0] = (state.header.tp << 6) | (state.header.MHF << 4) |
          (state.header.mh_id << 1) | state.header.T;
      header[1] = state.header.priority;
      header[2] = state.header.tile >> 8;
      header[3] = state.header.tile & 0xff;
      header[4] = 0;
      header[5] = state.header.offset >> 16;
      header[6] = (state.header.offset >> 8) & 0xff;
      header[7] = state.header.offset & 0xff;

      gst_rtp_buffer_unmap (&rtp);

      /* make subbuffer of j2k data */
      paybuf = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL,
          offset, data_size);
#ifndef RTP_J2K_SIM
      gst_rtp_copy_meta (GST_ELEMENT_CAST (basepayload), outbuf, paybuf,
          g_quark_from_static_string (GST_META_TAG_VIDEO_STR));
#endif
      outbuf = gst_buffer_append (outbuf, paybuf);

      gst_buffer_list_add (list, outbuf);

      /* reset header for next round */
      state.header.MHF = 0;
      state.header.T = 1;
      state.header.tile = 0;

      offset += data_size;
      state.header.offset = offset;
    }
    offset = pos;
  } while (offset < map.size);

  gst_buffer_unref (buffer);

  /* push the whole buffer list at once */
  ret = gst_rtp_base_payload_push_list (basepayload, list);

  return ret;
}


void* sim_payload(uint8_t* buffer, size_t len) {
	printf("Simulate Payload\n\n");
	auto payload = new GstRTPBasePayload();
	auto input = new GstBuffer();
	input->time_stamp = 0;
	GstMemory mem(buffer, len);
	input->memory.push_back(mem);
	gst_rtp_j2k_pay_handle_buffer(payload, input);
	printf("\n\n");
	return payload;
}
