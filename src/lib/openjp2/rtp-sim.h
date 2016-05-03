#pragma once

// GST //////////////////////////////////////////////

#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <cstdarg>

// types
typedef int64_t gint64;
typedef uint64_t guint64;

typedef int32_t gint;
typedef uint32_t guint;

typedef int32_t gint32;
typedef uint32_t guint32;

typedef uint8_t guint8;
typedef uint8_t gchar;

typedef bool gboolean;

typedef size_t gsize;
typedef void* gpointer;
typedef const void *gconstpointer;


struct GList {
	GList() : index(-1), data(nullptr) {}

	std::vector<void*> impl;
	gsize index;
	void* data;
};


#define TRUE true
#define FALSE false

typedef guint64 GstClockTime;
typedef guint GstMapFlags;

enum GstFlowReturn {
	GST_FLOW_ERROR,
	GST_FLOW_OK
};


const guint GST_MAP_READ = (2);
const guint GST_MAP_WRITE = (3);


GList* g_list_next(GList* list);
void g_list_free(GList* list);

#define G_UNLIKELY(expr) (expr)
void GST_LOG_OBJECT(void* A, const char* format, ...);
void GST_DEBUG_OBJECT(void* A, const char* format, ...);

guint GST_READ_UINT16_BE(const guint8* data);
guint GST_READ_UINT32_BE(const guint8* data);
void GST_WRITE_UINT32_BE(guint8* data, guint32 num);


#define G_GSIZE_FORMAT "lu"
#define GST_TIME_FORMAT "u:%02u:%02u.%09u"
#define GST_TIME_ARGS(t) (t)

struct RefCounted {
	RefCounted() : _ref(1) {}

	size_t _ref;
	size_t ref() {
		return ++_ref;
	}
	virtual size_t unref() {
		return --_ref;
	}
};

struct GstMemory  {
	GstMemory() : data(nullptr), 
					maxsize(-1),
					align(0),
					offset(0),
					size(-1),
					owns_data(false) {}

	GstMemory(gsize len) : data(new guint8[len]),
							maxsize(len),
							align(0),
							offset(0),
							size(len),
							owns_data(true){
	}

	GstMemory(guint8* buf, gsize len) : data(buf),
										maxsize(len),
										align(0),
										offset(0),
										size(len),
										owns_data(false) {}

	guint8			*data;
	gsize           maxsize;

	gsize           align;
	gsize           offset;
	gsize           size;

	bool owns_data;
};

struct GstBuffer : RefCounted {
	GstBuffer() : len(-1), time_stamp(-1) {}
	std::vector<GstMemory> memory;
	guint64 len;
	GstClockTime time_stamp;

	size_t unref() {
		RefCounted::unref();
		if (_ref == 0) {
			clean_memory();
		}
		return _ref;
	}
	void clean_memory() {
		for (auto mem : memory) {
			if (mem.owns_data && mem.data)
				delete[] mem.data;
		}
		memory.clear();
	}
} ;

struct GstMapInfo : RefCounted {
	GstMapInfo() : flags(0), data(nullptr), size(-1), maxsize(-1) {}

	std::vector<GstMemory> memory;  //memory to be mapped
	GstMapFlags flags;
	guint8 *data;    // valid offset
	gsize size;		 // valid size
	gsize maxsize;
};

#define GST_BUFFER_CAST(a)  ((GstBuffer*)(a))

struct GstAdapter : RefCounted {
	std::vector<GstBuffer*> buffers;
};

gsize gst_adapter_available(GstAdapter *adapter);

void gst_adapter_push(GstAdapter *adapter,	GstBuffer *buf);

void gst_adapter_clear(GstAdapter *adapter);

void gst_adapter_copy(GstAdapter *adapter,
						gpointer dest,
						gsize offset,
						gsize size);

GstAdapter * gst_adapter_new(void);

GList * gst_adapter_take_list(GstAdapter *adapter,	gsize nbytes);

GstBuffer* gst_adapter_take_buffer(GstAdapter *adapter, gsize nbytes);

////////////////////////////////////////////////////////////////////////////////

gboolean gst_buffer_map(GstBuffer *buffer,
						GstMapInfo *info,
						GstMapFlags flags);

void gst_buffer_unmap(GstBuffer* buffer, GstMapInfo* map);

GstBuffer * gst_buffer_ref(GstBuffer *buf);

void gst_buffer_unref(GstBuffer* buffer);

gsize gst_buffer_get_size(GstBuffer *buffer);

GstBuffer *         gst_buffer_new_and_alloc(guint size);

GstBuffer *  gst_buffer_append(GstBuffer *buf1, 	GstBuffer *buf2);

gsize gst_buffer_fill(GstBuffer *buffer,
						gsize offset,
						gconstpointer src,
						gsize size);


enum GstBufferCopyFlags {
	GST_BUFFER_COPY_NONE,
	GST_BUFFER_COPY_FLAGS,
	GST_BUFFER_COPY_TIMESTAMPS,
	GST_BUFFER_COPY_META,
	GST_BUFFER_COPY_MEMORY,
	GST_BUFFER_COPY_MERGE,
	GST_BUFFER_COPY_DEEP,
};

GstBuffer * gst_buffer_copy_region(GstBuffer *parent,
									GstBufferCopyFlags flags,
									gsize offset,
									gsize size);

#define GST_BUFFER_COPY_METADATA (GST_BUFFER_COPY_META)
#define GST_BUFFER_COPY_ALL  ((GstBufferCopyFlags)(GST_BUFFER_COPY_METADATA | GST_BUFFER_COPY_MEMORY))

#define   gst_buffer_make_writable(buf)   (buf)

struct GstBufferList {
	GstBufferList(guint size) {
		_list.reserve(size);
	}
	std::vector<GstBuffer*> _list;

} ;

void gst_buffer_list_add(GstBufferList* list, GstBuffer* buffer);


/// RTP ////////////////////////////////////////////////

// hard code MTU to 1500
#define GST_RTP_BASE_PAYLOAD_MTU(pay) (1500)

struct GstRTPBuffer : RefCounted {
	GstRTPBuffer() : buffer(nullptr), state(0) {

	}
	GstRTPBuffer(GstBuffer* buf) : buffer(buf), state(0) {
		buf->ref();
	}
	GstBuffer   *buffer;
	guint        state;
	gpointer     data[4];
	gsize        size[4];
	GstMapInfo   map[4];

	size_t unref() {
		if (!RefCounted::unref()) {
			if (!buffer->unref()) {
				delete buffer;
				buffer = nullptr;
			}
		}
		return _ref;
	}

};



struct GstRTPBasePayload {
	std::vector<GstBufferList*> bufferLists;
};


struct GstRTPBaseDepayload
{
	std::vector<GstBufferList*> bufferLists;

};


GstBuffer * gst_rtp_buffer_new_allocate(guint payload_len,
										guint8 pad_len,
										guint8 csrc_count);


GstBuffer *gst_rtp_buffer_get_payload_subbuffer(GstRTPBuffer *rtp,
												guint offset,
												guint len);

GstBufferList* gst_buffer_list_new_sized(guint size);

guint8*  gst_rtp_buffer_get_payload(GstRTPBuffer *rtp);

guint gst_rtp_buffer_get_payload_len(GstRTPBuffer *rtp);

gboolean gst_rtp_buffer_map(GstBuffer *buffer,
	GstMapFlags flags,
	GstRTPBuffer *rtp);

void gst_rtp_buffer_unmap(GstRTPBuffer *rtp);

void gst_rtp_buffer_set_marker(GstRTPBuffer *rtp,	gboolean marker);

gboolean gst_rtp_buffer_get_marker(GstRTPBuffer *rtp);

guint32 gst_rtp_buffer_get_timestamp(GstRTPBuffer *rtp);

guint gst_rtp_buffer_calc_payload_len(guint packet_len,
										guint8 pad_len,
										guint8 csrc_count);
guint  gst_rtp_buffer_calc_packet_len(guint payload_len, guint8 pad_len, guint8 csrc_count);

#define GST_BUFFER_PTS(buffer) (buffer->time_stamp)

GstFlowReturn gst_rtp_base_payload_push_list(GstRTPBasePayload *payload,GstBufferList *list);

GstFlowReturn gst_rtp_base_depayload_push(GstRTPBaseDepayload *filter,
											GstBuffer *out_buf);

