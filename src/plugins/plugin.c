/*! \file   plugin.h
 * \author Lorenzo Miniero <lorenzo@meetecho.com>
 * \copyright GNU General Public License v3
 * \brief  Plugin-Core communication (implementation)
 * \details  Implementation of the janus_plugin_result stuff: all the
 * important things related to the actual plugin API is in plugin.h.
 *
 * \ingroup pluginapi
 * \ref pluginapi
 */

#include "plugin.h"

#include <jansson.h>
#include <netdb.h>

#include "../apierror.h"
#include "../debug.h"

/* Plugin results */
janus_plugin_result *janus_plugin_result_new(janus_plugin_result_type type, const char *text, json_t *content) {
	JANUS_LOG(LOG_HUGE, "Creating plugin result...\n");
	janus_plugin_result *result = g_malloc(sizeof(janus_plugin_result));
	result->type = type;
	result->text = text;
	result->content = content;
	return result;
}

void janus_plugin_result_destroy(janus_plugin_result *result) {
	JANUS_LOG(LOG_HUGE, "Destroying plugin result...\n");
	result->text = NULL;
	if(result->content)
		json_decref(result->content);
	result->content = NULL;
	g_free(result);
}

/* RTP, RTCP and data packets initialization */
void janus_plugin_rtp_extensions_reset(janus_plugin_rtp_extensions *extensions) {
	if(extensions) {
		/* By extensions are not added to packets */
		extensions->audio_level = -1;
		extensions->audio_level_vad = FALSE;
		extensions->video_rotation = -1;
		extensions->video_back_camera = FALSE;
		extensions->video_flipped = FALSE;
		extensions->min_delay = -1;
		extensions->max_delay = -1;
		extensions->dd_len = 0;
		memset(extensions->dd_content, 0, sizeof(extensions->dd_content));
	}
}
void janus_plugin_rtp_reset(janus_plugin_rtp *packet) {
	if(packet) {
		memset(packet, 0, sizeof(janus_plugin_rtp));
		packet->mindex = -1;
		janus_plugin_rtp_extensions_reset(&packet->extensions);
	}
}
janus_plugin_rtp *janus_plugin_rtp_duplicate(janus_plugin_rtp *packet) {
	janus_plugin_rtp *p = NULL;
	if(packet) {
		p = g_malloc(sizeof(janus_plugin_rtp));
		p->mindex = packet->mindex;
		p->video = packet->video;
		if(packet->buffer == NULL || packet->length == 0) {
			p->buffer = NULL;
			p->length = 0;
		} else {
			p->buffer = g_malloc(packet->length);
			memcpy(p->buffer, packet->buffer, packet->length);
			p->length = packet->length;
		}
		p->extensions = packet->extensions;
	}
	return p;
}
void janus_plugin_rtcp_reset(janus_plugin_rtcp *packet) {
	if(packet) {
		memset(packet, 0, sizeof(janus_plugin_rtcp));
		packet->mindex = -1;
	}
}
void janus_plugin_data_reset(janus_plugin_data *packet) {
	if(packet)
		memset(packet, 0, sizeof(janus_plugin_data));
}

void init_janus_streaming_context(janus_streaming_context *sctx, int count) {
	if (!sctx || count <= 0)
		return;

	sctx->buf = (char*)g_malloc0(MTU * count);
	sctx->mmsgs = (struct mmsghdr*)g_malloc0(sizeof(struct mmsghdr) * count);
	sctx->iovecs = (struct iovec*)g_malloc0(sizeof(struct iovec) * count);
	sctx->packets = (janus_plugin_rtp*)g_malloc0(sizeof(janus_plugin_rtp) * count);
	sctx->cms = (struct cmsghdr**)g_malloc(sizeof(struct cmsghdr*) * count);
	sctx->msg_controls = (janus_streaming_cmsghdr*)g_malloc0(sizeof(janus_streaming_cmsghdr) * count);
	for (int i = 0; i < count; i++) {
		sctx->mmsgs[i].msg_hdr.msg_iov = &sctx->iovecs[i];
		sctx->mmsgs[i].msg_hdr.msg_iovlen = 1;
		sctx->mmsgs[i].msg_hdr.msg_control = &sctx->msg_controls[i];
		sctx->mmsgs[i].msg_hdr.msg_controllen = sizeof(janus_streaming_cmsghdr);
		sctx->mmsgs[i].msg_hdr.msg_flags = 0;
		sctx->cms[i] = CMSG_FIRSTHDR(&sctx->mmsgs[i].msg_hdr);
		sctx->cms[i]->cmsg_level = IPPROTO_UDP;
		sctx->cms[i]->cmsg_type = UDP_SEGMENT;
		sctx->cms[i]->cmsg_len = CMSG_LEN(sizeof(uint16_t));
	}
}

void free_janus_streaming_context(janus_streaming_context *sctx) {
	if (!sctx)
		return;

	g_free(sctx->buf);
	g_free(sctx->mmsgs);
	g_free(sctx->iovecs);
	g_free(sctx->packets);
	g_free(sctx->cms);
	g_free(sctx->msg_controls);
}

void align_janus_streaming_context(janus_streaming_context *sctx) {
	uint16_t max_length = 0;
	for (int i = 0; i < sctx->count; i++) {
		if (sctx->packets[i].length > max_length) {
			max_length = sctx->packets[i].length;
		}
	}
	
	janus_plugin_rtp temp;
	int start = 0;
	for (int i = 0; i < sctx->count; i++) {
		if (sctx->packets[i].length == max_length) {
			if (i != start) {
				temp = sctx->packets[i];
				sctx->packets[i] = sctx->packets[start];
				sctx->packets[start] = temp;
			}
			start += 1;
		}
	}

}