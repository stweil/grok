/* GStreamer */


#ifndef __GST_RTP_J2K_PAYDEPAY_H__
#define __GST_RTP_J2K_PAYDEPAY_H__


/*
* RtpJ2KMarker:
* @J2K_MARKER: Prefix for JPEG 2000 marker
* @J2K_MARKER_SOC: Start of Codestream
* @J2K_MARKER_SOT: Start of tile
* @J2K_MARKER_EOC: End of Codestream
*
* Identifers for markers in JPEG 2000 codestreams
*/
typedef enum
{
	J2K_MARKER = 0xFF,
	J2K_MARKER_SOC = 0x4F,
	J2K_MARKER_SOT = 0x90,
	J2K_MARKER_SOP = 0x91,
	J2K_MARKER_EPH = 0x92,
	J2K_MARKER_SOD = 0x93,
	J2K_MARKER_EOC = 0xD9
} RtpJ2KMarker;

enum
{
	PROP_0,
	PROP_LAST
};


#define HEADER_SIZE 8


#endif /* __GST_RTP_J2K_PAYDEPAY_H__ */
