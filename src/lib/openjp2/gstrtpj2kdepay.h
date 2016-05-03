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

#ifndef __GST_RTP_J2K_DEPAY_H__
#define __GST_RTP_J2K_DEPAY_H__


struct GstRtpJ2KDepay : GstRTPBaseDepayload
{
	GstRtpJ2KDepay() : last_rtptime(-1),
		last_mh_id(-1),
		last_tile(-1),
		pu_MHF(-1),
		pu_adapter(new GstAdapter()),
		t_adapter(new GstAdapter()),
		f_adapter(new GstAdapter()),
		next_frag(-1),
		have_sync(false)
	{
		for (int i = 0; i < 8; ++i)
			MH[i] = nullptr;
	}

	guint64 last_rtptime;
	guint last_mh_id;
	guint last_tile;

	GstBuffer *MH[8];

	guint pu_MHF;
	GstAdapter *pu_adapter;
	GstAdapter *t_adapter;
	GstAdapter *f_adapter;

	guint next_frag;
	gboolean have_sync;
};


#endif /* __GST_RTP_J2K_DEPAY_H__ */
