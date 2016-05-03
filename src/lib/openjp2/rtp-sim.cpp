#include "rtp-sim.h"
#include <cmath>
#include <algorithm>
#include <cassert>


void GST_LOG_OBJECT(void* A, const char* format, ...) {
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf("\n");
}
void GST_DEBUG_OBJECT(void* A, const char* format, ...) {
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf("\n");
}

GList* g_list_next(GList* list) {
	if (list->impl.size() != 0 &&
			list->index < list->impl.size() - 1) {
		list->index++;
		list->data = list->impl[list->index];
		return list;
	}
	return nullptr;
}

void g_list_free(GList* list) {
	delete list;
}

// GST /////////////////////////////////////////////////

guint GST_READ_UINT16_BE(const guint8* data) {
	return ((guint)data[0] << 8) | (guint)data[1];
}
guint GST_READ_UINT32_BE(const guint8* data) {
	return ((guint)data[0] << 24) | ((guint)data[1] << 16) | ((guint)data[2] << 8) | (guint)data[3];
}

void GST_WRITE_UINT32_BE(guint8* data, guint32 num) {
	data[0] = (num >> 24) & 0xFF;
	data[1] = (num >> 16) & 0xFF;
	data[2] = (num >> 8) & 0xFF;
	data[3] = (num) & 0xFF;
}

gboolean gst_buffer_map(GstBuffer *buffer,
						GstMapInfo *info,
						GstMapFlags flags) {
	info->data = buffer->memory[0].data+ buffer->memory[0].offset;
	info->size = buffer->memory[0].size;
	return TRUE;
}

void gst_buffer_unmap(GstBuffer* buffer, GstMapInfo* map) {
	map->data = nullptr;
	map->size = 0;
}
gsize gst_buffer_get_size(GstBuffer *buffer) {
	gsize rc = 0;
	for (auto mem : buffer->memory) {
		rc += mem.size;
	}
	return rc;
}

GstBuffer * gst_buffer_ref(GstBuffer *buf) {
	buf->ref();
	return buf;
}

// assume buffer allocated on heap
void gst_buffer_unref(GstBuffer* buffer) {
	if (!buffer->unref()) {
		delete buffer;
	}
}
GstBufferList* gst_buffer_list_new_sized(guint size) {
	return new GstBufferList(size);
}

GstBuffer * gst_buffer_new_and_alloc(guint len) {
	GstBuffer* buf = new GstBuffer();
	GstMemory mem(len);
	buf->memory.push_back(mem);
	return buf;
}

// Copy size bytes from src to buffer at offset .
// (let's assume that offset lands at the beginning of one of the memory objects)
gsize gst_buffer_fill(GstBuffer *buffer,
						gsize offset,
						gconstpointer src,
						gsize size) {
	gsize off = 0;
	for (auto mem : buffer->memory) {
		if (off == offset) {
			guint8* bytesrc = (guint8*)src;
			assert(mem.size == size);
			memcpy(mem.data+mem.offset, bytesrc, size);
			break;
		}
		off += mem.size;
	}
	return size;
}

/*
Append all the memory from buf2 to buf1. 
The result buffer will contain a concatenation of the memory of buf1 and buf2 .

*/
GstBuffer *  gst_buffer_append(GstBuffer *buf1, GstBuffer *buf2) {
	gsize totalSize = gst_buffer_get_size(buf1) + gst_buffer_get_size(buf2);
	if (totalSize == 0)
		return nullptr;

	GstMemory newMem(totalSize);

	gsize dest_offset = 0;
	for (auto mem : buf1->memory) {
		memcpy(newMem.data + dest_offset, mem.data + mem.offset, mem.size);
		dest_offset += mem.size;
	}
	for (auto mem : buf2->memory) {
		memcpy(newMem.data + dest_offset, mem.data + mem.offset, mem.size);
		dest_offset += mem.size;
	}
	gst_buffer_unref(buf2);
	buf1->clean_memory();
	buf1->memory.push_back(newMem);
	return buf1;
}

void gst_buffer_list_add(GstBufferList* list, GstBuffer* buffer) {
	list->_list.push_back(buffer);
}

/*
Creates a sub-buffer from parent at offset and size .
This sub-buffer uses the actual memory space of the parent buffer.
This function will copy the offset and timestamp fields when the offset is 0. 
If not, they will be set to GST_CLOCK_TIME_NONE and GST_BUFFER_OFFSET_NONE. 
If offset equals 0 and size equals the total size of buffer ,
the duration and offset end fields are also copied. 
If not they will be set to GST_CLOCK_TIME_NONE and GST_BUFFER_OFFSET_NONE.

*/

GstBuffer * gst_buffer_copy_region(GstBuffer *parent,
									GstBufferCopyFlags flags,
									gsize offset,
									gsize size) {

	auto buf = new GstBuffer();
	gsize src_offset = 0;
	gsize dest_offset = offset;
	gsize bytesToCopy = size;
	for (auto mem : parent->memory) {
		// ignore memory below offset
		if (src_offset + mem.size < offset) {
			src_offset += mem.size;
			dest_offset -= mem.size;
			continue;
		}

		auto copied = std::min(bytesToCopy, mem.size);

		auto newMem = mem;
		newMem.owns_data = false;
		newMem.offset = dest_offset;
		newMem.size = copied;
		buf->memory.push_back(newMem);
		bytesToCopy -= copied;
		if (bytesToCopy == 0)
			break;
		src_offset += copied;
	}
	return buf;
}

///////////////////////////////////////////////////////////////////////////////////////

gsize gst_adapter_available(GstAdapter *adapter) {
	gsize rc = 0;
	for (auto buf : adapter->buffers) {
		rc += gst_buffer_get_size(buf);
	}
	return rc;
}

/*
Copies size bytes of data starting at offset out of the buffers contained in GstAdapter
into an array dest provided by the caller.

The array dest should be large enough to contain size bytes.
The user should check that the adapter has(offset + size) bytes available
before calling this function.

*/
void gst_adapter_copy(GstAdapter *adapter,
					gpointer dest,
					gsize offset,
					gsize size) {
	if (gst_adapter_available(adapter) < offset + size)
	{
		printf("Warning: gst_adapter_copy: not enough bytes\n");
		return;
	}
	gsize dest_offset = 0;
	gsize bytesToCopy = size;
	for (auto buf : adapter->buffers) {
		for (auto mem : buf->memory) {
			// ignore memory below offset
			if (dest_offset + mem.size < offset) {
				dest_offset += mem.size;
				continue;
			}

			auto len = std::min(bytesToCopy, mem.size);
			memcpy((uint8_t*)dest+ dest_offset,
							mem.data + mem.offset,
							len);
			bytesToCopy -= len;
			if (bytesToCopy == 0)
				return;
			dest_offset += len;
		}
	}
}

// Adds the data from buf to the data stored inside adapter
// and takes ownership of the buffer.
void gst_adapter_push(GstAdapter *adapter, GstBuffer *buf) {
	gst_buffer_ref(buf);
	adapter->buffers.push_back(buf);
}

// Removes all buffers from adapter .
void gst_adapter_clear(GstAdapter *adapter) {
	for (auto buf : adapter->buffers) {
		gst_buffer_unref(buf);
	}
	adapter->buffers.clear();
}

GstAdapter * gst_adapter_new(void) {
	return new GstAdapter();
}

/*

Returns a GstBuffer containing the first nbytes bytes of the adapter . 
The returned bytes will be flushed from the adapter. 
This function is potentially more performant than gst_adapter_take() 
since it can reuse the memory in pushed buffers by subbuffering or merging. 
This function will always return a buffer with a single memory region.

Note that no assumptions should be made as to whether certain buffer flags
such as the DISCONT flag are set on the returned buffer, or not. 
The caller needs to explicitly set or unset flags that should be set or unset.

Since 1.6 this will also copy over all GstMeta of the input buffers
except for meta with the GST_META_FLAG_POOLED flag or with the "memory" tag.

Caller owns a reference to the returned buffer. gst_buffer_unref() after usage.

Free-function: gst_buffer_unref

*/
GstBuffer* gst_adapter_take_buffer(GstAdapter *adapter, gsize nbytes) {
	auto dest = gst_buffer_new_and_alloc((guint)nbytes);
	
	gsize bytesToCopy = nbytes;
	gsize dest_offset = 0;
	for (auto buf : adapter->buffers) {
		for (auto mem : buf->memory) {
			gsize len = std::min(bytesToCopy, mem.size);
			memcpy(dest->memory[0].data+ dest_offset, 
								mem.data + mem.offset, len);
			assert(dest->memory[0].data + dest_offset != mem.data + mem.offset);
			bytesToCopy -= len;
			if (bytesToCopy == 0)
				return dest;
			dest_offset += len;
		}
	}

	return dest;
}

/*
Returns a GList of buffers containing the first nbytes bytes of the adapter .
The returned bytes will be flushed from the adapter. 
When the caller can deal with individual buffers, this function is more performant
because no memory should be copied.

Caller owns returned list and contained buffers.
gst_buffer_unref() each buffer in the list before freeing the list after usage.

*/
GList * gst_adapter_take_list(GstAdapter *adapter, gsize nbytes) {
	GList* list = new GList();
	if (nbytes == 0)
		return list;

	gsize bytesToCopy = nbytes;
	gsize buffersToRemove = 0;
	for (auto buf : adapter->buffers) {
		auto len = gst_buffer_get_size(buf);
		buffersToRemove++;
		list->impl.push_back(buf);
		bytesToCopy -= len;
		if (bytesToCopy == 0)
			break;
	}

	if (!list->impl.empty())
		list->data = list->impl[0];

	// now remove buffers from adapter
	for (int i = 0; i < buffersToRemove; ++i) {
		adapter->buffers.erase(adapter->buffers.begin());
	}
	return list;
}


// RTP  /////////////////////////////////////////////////////////////////////

// Note: simulated packets only have payloads, no headers or padding etc.

GstBuffer * gst_rtp_buffer_new_allocate(guint payload_len,
										guint8 pad_len,
										guint8 csrc_count) {
	return gst_buffer_new_and_alloc(payload_len);
}

gboolean  gst_rtp_buffer_map(GstBuffer *buffer,
							GstMapFlags flags,
							GstRTPBuffer *rtp) {
	rtp->buffer = buffer;
	return TRUE;
}

void gst_rtp_buffer_unmap(GstRTPBuffer *rtp) {
	rtp->buffer = nullptr;
}

// assume payload is stored without header in buffer with single memory region
guint8* gst_rtp_buffer_get_payload(GstRTPBuffer *rtp) {
	if (!rtp || !rtp->buffer || !rtp->buffer->memory.size())
		return nullptr;
	assert(rtp->buffer->memory.size() == 1);
	return rtp->buffer->memory[0].data + rtp->buffer->memory[0].offset;
}

/*
Create a subbuffer of the payload of the RTP packet in buffer .
offset bytes are skipped in the payload and the subbuffer will be of size len .
If len is -1 the total payload starting from offset is subbuffered.

*/
GstBuffer *gst_rtp_buffer_get_payload_subbuffer(GstRTPBuffer *rtp,
												guint offset,
												guint len) {
	if (len == -1)
		len = gst_rtp_buffer_get_payload_len(rtp) - offset;
	auto payload = gst_rtp_buffer_get_payload(rtp);
	auto buf = new GstBuffer();
	GstMemory mem(payload + offset, len);
	buf->memory.push_back(mem);
	return buf;
}

guint gst_rtp_buffer_get_payload_len(GstRTPBuffer *rtp) {
	if (!rtp || !rtp->buffer || !rtp->buffer->memory.size())
		return 0;
	assert(rtp->buffer->memory.size() == 1);
	return (guint)rtp->buffer->memory[0].size;
}

guint gst_rtp_buffer_calc_payload_len(guint packet_len,
	guint8 pad_len,
	guint8 csrc_count) {
	return packet_len;
}

// simulated packets have no headers
guint  gst_rtp_buffer_calc_packet_len(guint payload_len, guint8 pad_len, guint8 csrc_count) {
	return payload_len;
}

void gst_rtp_buffer_set_marker(GstRTPBuffer *rtp, gboolean marker) {
	rtp->state = marker ? 1 : 0;
}
gboolean gst_rtp_buffer_get_marker(GstRTPBuffer *rtp) {
	return rtp->state != 0;
}

guint32 gst_rtp_buffer_get_timestamp(GstRTPBuffer *rtp) {
	return 0;
}
GstFlowReturn gst_rtp_base_payload_push_list(GstRTPBasePayload *payload,
												GstBufferList *list) {
	payload->bufferLists.push_back(list);
	return GST_FLOW_OK;
}

GstFlowReturn gst_rtp_base_depayload_push(GstRTPBaseDepayload *filter,
													GstBuffer *out_buf) {
	if (filter->bufferLists.empty())
		filter->bufferLists.back()->_list.push_back(out_buf);
	return GST_FLOW_OK;
}