/* GStreamer
 * Copyright (C) <2009> Wim Taymans <wim.taymans@gmail.com>
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
 * SECTION:element-rtpj2kdepay
 *
 * Depayload an RTP-payloaded JPEG 2000 image into RTP packets according to RFC 5371
 * and RFC 5372.
 * For detailed information see: https://datatracker.ietf.org/doc/rfc5371/
 * and https://datatracker.ietf.org/doc/rfc5372/
 */

#define RTP_J2K_SIM


#include "rtp-sim.h"
#include <assert.h>
#include "gstrtpj2kcommon.h"
#include "gstrtpj2kdepay.h"


static GstRtpJ2KDepay* GST_RTP_J2K_DEPAY(GstRTPBaseDepayload* base_pay) {
	return (GstRtpJ2KDepay*)base_pay;
}

static void store_mheader (GstRtpJ2KDepay * rtpj2kdepay, guint idx, GstBuffer * buf)
{
   assert(idx < 8);

  GstBuffer *old;

  GST_DEBUG_OBJECT (rtpj2kdepay, "storing main header %p at index %u", buf,
      idx);
  if ((old = rtpj2kdepay->MH[idx]))
    gst_buffer_unref (old);
  rtpj2kdepay->MH[idx] = buf;
}

static void clear_mheaders (GstRtpJ2KDepay * rtpj2kdepay)
{
  for (guint i = 0; i < 8; i++)
    store_mheader (rtpj2kdepay, i, NULL);
}

static void gst_rtp_j2k_depay_clear_pu (GstRtpJ2KDepay * rtpj2kdepay)
{
  gst_adapter_clear (rtpj2kdepay->pu_adapter);
  rtpj2kdepay->have_sync = FALSE;
}

static GstFlowReturn gst_rtp_j2k_depay_flush_pu (GstRTPBaseDepayload * depayload)
{
  GstRtpJ2KDepay *rtpj2kdepay;
  GstBuffer *mheader;
  guint avail, MHF, mh_id;

  rtpj2kdepay = GST_RTP_J2K_DEPAY (depayload);

  /* take all available buffers */
  avail = (guint)gst_adapter_available (rtpj2kdepay->pu_adapter);
  if (avail == 0)
    goto done;

  MHF = rtpj2kdepay->pu_MHF;
  mh_id = rtpj2kdepay->last_mh_id;

  GST_DEBUG_OBJECT (rtpj2kdepay, "flushing PU of size %u", avail);

  if (MHF == 0) {
    GList *packets, *walk;

    packets = gst_adapter_take_list (rtpj2kdepay->pu_adapter, avail);
    /* append packets */
    for (walk = packets; walk; walk = g_list_next (walk)) {
      GstBuffer *buf = GST_BUFFER_CAST (walk->data);
      GST_DEBUG_OBJECT (rtpj2kdepay,
          "append pu packet of size %llu",
          gst_buffer_get_size (buf));
      gst_adapter_push (rtpj2kdepay->t_adapter, buf);
    }
    g_list_free (packets);
  } else {
    /* we have a header */
    GST_DEBUG_OBJECT (rtpj2kdepay, "keeping header %u", mh_id);
    /* we managed to see the start and end of the header, take all from
     * adapter and keep in header  */
    mheader = gst_adapter_take_buffer (rtpj2kdepay->pu_adapter, avail);

    store_mheader (rtpj2kdepay, mh_id, mheader);
  }

done:
  rtpj2kdepay->have_sync = FALSE;

  return GST_FLOW_OK;
}

static GstFlowReturn gst_rtp_j2k_depay_flush_tile (GstRTPBaseDepayload * depayload)
{
  GstRtpJ2KDepay *rtpj2kdepay;
  guint avail, mh_id;
  GList *packets, *walk;
  guint8 end[2];
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo map;
  GstBuffer *buf;

  rtpj2kdepay = GST_RTP_J2K_DEPAY (depayload);

  /* flush pending PU */
  gst_rtp_j2k_depay_flush_pu (depayload);

  /* take all available buffers */
  avail = (guint)gst_adapter_available (rtpj2kdepay->t_adapter);
  if (avail == 0)
    goto done;

  mh_id = rtpj2kdepay->last_mh_id;

  GST_DEBUG_OBJECT (rtpj2kdepay, "flushing tile of size %u", avail);

  if (gst_adapter_available (rtpj2kdepay->f_adapter) == 0) {
    GstBuffer *mheader;

    /* we need a header now */
    if ((mheader = rtpj2kdepay->MH[mh_id]) == NULL)
      goto waiting_header;

    /* push header in the adapter */
    GST_DEBUG_OBJECT (rtpj2kdepay, "pushing header %u", mh_id);
    gst_adapter_push (rtpj2kdepay->f_adapter, gst_buffer_ref (mheader));
  }

  /* check for last bytes */
  gst_adapter_copy (rtpj2kdepay->t_adapter, end, avail - 2, 2);

  /* now append the tile packets to the frame */
  packets = gst_adapter_take_list (rtpj2kdepay->t_adapter, avail);
  for (walk = packets; walk; walk = g_list_next (walk)) {
    buf = GST_BUFFER_CAST (walk->data);

    if (walk == packets) {
      /* first buffer should contain the SOT */
      gst_buffer_map (buf, &map, GST_MAP_READ);

      if (map.size < 12)
        goto invalid_tile;

      if (map.data[0] == GST_J2K_MARKER && map.data[1] == GST_J2K_MARKER_SOT) {
        guint Psot, nPsot;

        if (end[0] == GST_J2K_MARKER && end[1] == GST_J2K_MARKER_EOC)
          nPsot = avail - 2;
        else
          nPsot = avail;

        Psot = GST_READ_UINT32_BE (&map.data[6]);
        if (Psot != nPsot && Psot != 0) {
          /* Psot must match the size of the tile */
          GST_DEBUG_OBJECT (rtpj2kdepay, "set Psot from %u to %u", Psot, nPsot);
          gst_buffer_unmap (buf, &map);

          buf = gst_buffer_make_writable (buf);

          gst_buffer_map (buf, &map, GST_MAP_WRITE);
          GST_WRITE_UINT32_BE (&map.data[6], nPsot);
        }
      }
      gst_buffer_unmap (buf, &map);
    }

    GST_DEBUG_OBJECT (rtpj2kdepay, "append pu packet of size %llu",
        gst_buffer_get_size (buf));
    gst_adapter_push (rtpj2kdepay->f_adapter, buf);
  }
  g_list_free (packets);

done:
  rtpj2kdepay->last_tile = -1;

  return ret;

  /* errors */
waiting_header:
  {
    GST_DEBUG_OBJECT (rtpj2kdepay, "waiting for header %u", mh_id);
    gst_adapter_clear (rtpj2kdepay->t_adapter);
    rtpj2kdepay->last_tile = -1;
    return ret;
  }
invalid_tile:
  {
    //GST_ELEMENT_WARNING (rtpj2kdepay, STREAM, DECODE, 
	  printf("Invalid tile");
			//, (NULL));
    gst_buffer_unmap (buf, &map);
    gst_adapter_clear (rtpj2kdepay->t_adapter);
    rtpj2kdepay->last_tile = -1;
    return ret;
  }
}

static GstFlowReturn gst_rtp_j2k_depay_flush_frame (GstRTPBaseDepayload * depayload)
{
  GstRtpJ2KDepay *rtpj2kdepay;
  guint8 end[2];
  guint avail;

  GstFlowReturn ret = GST_FLOW_OK;

  rtpj2kdepay = GST_RTP_J2K_DEPAY (depayload);

  /* flush pending tile */
  gst_rtp_j2k_depay_flush_tile (depayload);

  /* last buffer take all data out of the adapter */
  avail = (guint)gst_adapter_available (rtpj2kdepay->f_adapter);
  if (avail == 0)
    goto done;

  if (avail > 2) {
    GstBuffer *outbuf;

    /* take the last bytes of the JPEG 2000 data to see if there is an EOC
     * marker */
    gst_adapter_copy (rtpj2kdepay->f_adapter, end, avail - 2, 2);

    if (end[0] != GST_J2K_MARKER && end[1] != GST_J2K_MARKER_EOC) {
      end[0] = GST_J2K_MARKER;
      end[1] = GST_J2K_MARKER_EOC;

      GST_DEBUG_OBJECT (rtpj2kdepay, "no EOC marker, adding one");

      /* no EOC marker, add one */
      outbuf = gst_buffer_new_and_alloc (2);
      gst_buffer_fill (outbuf, 0, end, 2);

      gst_adapter_push (rtpj2kdepay->f_adapter, outbuf);
      avail += 2;
    }

    GST_DEBUG_OBJECT (rtpj2kdepay, "pushing buffer of %u bytes", avail);
    outbuf = gst_adapter_take_buffer (rtpj2kdepay->f_adapter, avail);
#ifndef RTP_J2K_SIM
    gst_rtp_drop_meta (GST_ELEMENT_CAST (depayload),
        outbuf, g_quark_from_static_string (GST_META_TAG_VIDEO_STR));
#endif
    ret = gst_rtp_base_depayload_push (depayload, outbuf);
  } else {
    //GST_WARNING_OBJECT (rtpj2kdepay, 
		printf("empty packet");
    gst_adapter_clear (rtpj2kdepay->f_adapter);
  }

  /* we accept any mh_id now */
  rtpj2kdepay->last_mh_id = -1;

  /* reset state */
  rtpj2kdepay->next_frag = 0;
  rtpj2kdepay->have_sync = FALSE;

done:
  /* we can't keep headers with mh_id of 0 */
  store_mheader (rtpj2kdepay, 0, NULL);

  return ret;
}

static GstBuffer* gst_rtp_j2k_depay_process (GstRTPBaseDepayload * depayload, GstRTPBuffer * rtp)
{
  GstRtpJ2KDepay *rtpj2kdepay;
  guint8 *payload;
  guint MHF, mh_id, frag_offset, tile, payload_len, j2klen;
  gint gap;
  guint32 rtptime;

  rtpj2kdepay = GST_RTP_J2K_DEPAY (depayload);

  payload = gst_rtp_buffer_get_payload (rtp);
  payload_len = gst_rtp_buffer_get_payload_len (rtp);

  /* we need at least a header */
  if (payload_len < GST_RTP_J2K_HEADER_SIZE)
    goto empty_packet;

  rtptime = gst_rtp_buffer_get_timestamp (rtp);

  /* new timestamp marks new frame */
  if (rtpj2kdepay->last_rtptime != rtptime) {
    rtpj2kdepay->last_rtptime = rtptime;
    /* flush pending frame */
    gst_rtp_j2k_depay_flush_frame (depayload);
  }

  /*
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |tp |MHF|mh_id|T|     priority  |           tile number         |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |reserved       |             fragment offset                   |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */
  MHF = (payload[0] & 0x30) >> 4;
  mh_id = (payload[0] & 0xe) >> 1;

  if (rtpj2kdepay->last_mh_id == -1)
    rtpj2kdepay->last_mh_id = mh_id;
  else if (rtpj2kdepay->last_mh_id != mh_id)
    goto wrong_mh_id;

  tile = (payload[2] << 8) | payload[3];
  frag_offset = (payload[5] << 16) | (payload[6] << 8) | payload[7];
  j2klen = payload_len - GST_RTP_J2K_HEADER_SIZE;

  GST_DEBUG_OBJECT (rtpj2kdepay, "MHF %u, tile %u, frag %u, expected %u", MHF,
      tile, frag_offset, rtpj2kdepay->next_frag);

  /* calculate the gap between expected frag */
  gap = frag_offset - rtpj2kdepay->next_frag;
  /* calculate next frag */
  rtpj2kdepay->next_frag = frag_offset + j2klen;

  if (gap != 0) {
    GST_DEBUG_OBJECT (rtpj2kdepay, "discont of %d, clear PU", gap);
    /* discont, clear pu adapter and resync */
    gst_rtp_j2k_depay_clear_pu (rtpj2kdepay);
  }

  /* check for sync code */
  if (j2klen > 2 && payload[GST_RTP_J2K_HEADER_SIZE] == 0xff) {
    guint marker = payload[GST_RTP_J2K_HEADER_SIZE+1];

    /* packets must start with SOC, SOT or SOP */
    switch (marker) {
      case GST_J2K_MARKER_SOC:
        GST_DEBUG_OBJECT (rtpj2kdepay, "found SOC packet");
        /* flush the previous frame, should have happened when the timestamp
         * changed above. */
        gst_rtp_j2k_depay_flush_frame (depayload);
        rtpj2kdepay->have_sync = TRUE;
        break;
      case GST_J2K_MARKER_SOT:
        /* flush the previous tile */
        gst_rtp_j2k_depay_flush_tile (depayload);
        GST_DEBUG_OBJECT (rtpj2kdepay, "found SOT packet");
        rtpj2kdepay->have_sync = TRUE;
        /* we sync on the tile now */
        rtpj2kdepay->last_tile = tile;
        break;
      case GST_J2K_MARKER_SOP:
        GST_DEBUG_OBJECT (rtpj2kdepay, "found SOP packet");
        /* flush the previous PU */
        gst_rtp_j2k_depay_flush_pu (depayload);
        if (rtpj2kdepay->last_tile != tile) {
          /* wrong tile, we lose sync and we need a new SOT or SOC to regain
           * sync. First flush out the previous tile if we have one. */
          if (rtpj2kdepay->last_tile != -1)
            gst_rtp_j2k_depay_flush_tile (depayload);
          /* now we have no more valid tile and no sync */
          rtpj2kdepay->last_tile = -1;
          rtpj2kdepay->have_sync = FALSE;
        } else {
          rtpj2kdepay->have_sync = TRUE;
        }
        break;
      default:
        GST_DEBUG_OBJECT (rtpj2kdepay, "no sync packet 0x%02d", marker);
        break;
    }
  }

  if (rtpj2kdepay->have_sync) {
    GstBuffer *pu_frag;

    if (gst_adapter_available (rtpj2kdepay->pu_adapter) == 0) {
      /* first part of pu, record state */
      GST_DEBUG_OBJECT (rtpj2kdepay, "first PU");
      rtpj2kdepay->pu_MHF = MHF;
    }
    /* and push in pu adapter */
    GST_DEBUG_OBJECT (rtpj2kdepay, "push pu of size %u in adapter", j2klen);
    pu_frag = gst_rtp_buffer_get_payload_subbuffer (rtp, GST_RTP_J2K_HEADER_SIZE, -1);
    gst_adapter_push (rtpj2kdepay->pu_adapter, pu_frag);

    if (MHF & 2) {
      /* last part of main header received, we can flush it */
      GST_DEBUG_OBJECT (rtpj2kdepay, "header end, flush pu");
      gst_rtp_j2k_depay_flush_pu (depayload);
    }
  } else {
    GST_DEBUG_OBJECT (rtpj2kdepay, "discard packet, no sync");
  }

  /* marker bit finishes the frame */
  if (gst_rtp_buffer_get_marker (rtp)) {
    GST_DEBUG_OBJECT (rtpj2kdepay, "marker set, last buffer");
    /* then flush frame */
    gst_rtp_j2k_depay_flush_frame (depayload);
  }

  return NULL;

  /* ERRORS */
empty_packet:
  {
    //GST_ELEMENT_WARNING (rtpj2kdepay, STREAM, DECODE,
	  printf("Empty Payload.");
	  ///, (NULL));
    return NULL;
  }
wrong_mh_id:
  {
    //GST_ELEMENT_WARNING (rtpj2kdepay, STREAM, DECODE,
	  printf("Invalid mh_id %u, expected %u", mh_id, rtpj2kdepay->last_mh_id);
     //   (NULL));
    gst_rtp_j2k_depay_clear_pu (rtpj2kdepay);
    return NULL;
  }
}


void sim_depayload(void* input, uint8_t** out, size_t* len) {
	printf("Simulate Depayload\n\n");
	GstRTPBasePayload* payload = (GstRTPBasePayload*)input;
	auto depayload = new GstRtpJ2KDepay();
	for (auto bufList : payload->bufferLists) {
		for (auto buf : bufList->_list) {
			auto rtp = new GstRTPBuffer(buf);
			gst_rtp_j2k_depay_process(depayload, rtp);
			rtp->unref();
			delete rtp;
		}
	}
	*len = 0;
	*out = nullptr;
	// get size of buffer list
	for (auto buf : depayload->pu_adapter->buffers) {
		for (auto mem : buf->memory) {
			*len += mem.size;
		}
	}

	if (!(*len))
		return;

	*out = new uint8_t[*len];
	size_t off = 0;
	for (auto buf : depayload->pu_adapter->buffers) {
		for (auto mem : buf->memory) {
			memcpy(*out + off, mem.data+mem.offset, mem.size);
			off += mem.size;
		}
	}
	printf("\n\n");
}
