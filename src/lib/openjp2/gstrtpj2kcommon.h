/* GStreamer */


#ifndef __GST_RTP_J2K_COMMON_H__
#define __GST_RTP_J2K_COMMON_H__


/*
* RtpJ2KMarker:
* @GST_J2K_MARKER: Prefix for JPEG 2000 marker
* @GST_J2K_MARKER_SOC: Start of Codestream
* @GST_J2K_MARKER_SOT: Start of tile
* @GST_J2K_MARKER_EOC: End of Codestream
*
* Identifers for markers in JPEG 2000 codestreams
*/
typedef enum
{
	GST_J2K_MARKER = 0xFF,
	GST_J2K_MARKER_SOC = 0x4F,
	GST_J2K_MARKER_SOT = 0x90,
	GST_J2K_MARKER_SOP = 0x91,
	GST_J2K_MARKER_EPH = 0x92,
	GST_J2K_MARKER_SOD = 0x93,
	GST_J2K_MARKER_EOC = 0xD9
} GstRtpJ2KMarker;


#define GST_RTP_J2K_HEADER_SIZE 8


#endif /* __GST_RTP_J2K_COMMON_H__ */
