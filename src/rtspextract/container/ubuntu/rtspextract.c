#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "config.h"
#include "libavutil/imgutils.h"
#include "libavformat/rtsp.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
#include "libswscale/swscale.h"
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavformat/mux.h"
#include "libavformat/demux.h"
#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/parseutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"
#include "libavutil/timecode.h"
#include "libavutil/time_internal.h"
#include "libavutil/timestamp.h"
#include "libswresample/swresample.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/timestamp.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_IO_H
#include <io.h>
#endif
#define SEC_TO_NS(sec) ((sec)*1000000000)
#define STREAM_DURATION   10.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */
// a wrapper around a single output AVStream
typedef struct OutputStream {
	AVStream* st;
	AVCodecContext* enc;

	/* pts of the next frame that will be generated */
	int64_t next_pts;
	int samples_count;

	AVFrame* frame;
	AVFrame* tmp_frame;

	AVPacket* tmp_pkt;

	float t, tincr, tincr2;

	struct SwsContext* sws_ctx;
	struct SwrContext* swr_ctx;
} OutputStream;


typedef struct SegmentListEntry {
	int index;
	double start_time, end_time;
	int64_t start_pts;
	int64_t offset_pts;
	char* filename;
	struct SegmentListEntry* next;
	int64_t last_duration;
} SegmentListEntry;

typedef enum {
	LIST_TYPE_UNDEFINED = -1,
	LIST_TYPE_FLAT = 0,
	LIST_TYPE_CSV,
	LIST_TYPE_M3U8,
	LIST_TYPE_EXT, ///< deprecated
	LIST_TYPE_FFCONCAT,
	LIST_TYPE_NB,
} ListType;

#define SEGMENT_LIST_FLAG_CACHE 1
#define SEGMENT_LIST_FLAG_LIVE  2

typedef struct SegmentContext {
	const AVClass* class;  /**< Class for private options. */
	int segment_idx;       ///< index of the segment file to write, starting from 0
	int segment_idx_wrap;  ///< number after which the index wraps
	int segment_idx_wrap_nb;  ///< number of time the index has wraped
	int segment_count;     ///< number of segment files already written
	const AVOutputFormat* oformat;
	AVFormatContext* avf;
	char* format;              ///< format to use for output segment files
	AVDictionary* format_options;
	char* list;            ///< filename for the segment list file
	int   list_flags;      ///< flags affecting list generation
	int   list_size;       ///< number of entries for the segment list file

	int is_nullctx;       ///< whether avf->pb is a nullctx
	int use_clocktime;    ///< flag to cut segments at regular clock time
	int64_t clocktime_offset; //< clock offset for cutting the segments at regular clock time
	int64_t clocktime_wrap_duration; //< wrapping duration considered for starting a new segment
	int64_t last_val;      ///< remember last time for wrap around detection
	int cut_pending;
	int header_written;    ///< whether we've already called avformat_write_header

	char* entry_prefix;    ///< prefix to add to list entry filenames
	int list_type;         ///< set the list type
	AVIOContext* list_pb;  ///< list file put-byte context
	int64_t time;          ///< segment duration
	int use_strftime;      ///< flag to expand filename with strftime
	int increment_tc;      ///< flag to increment timecode if found

	char* times_str;       ///< segment times specification string
	int64_t* times;        ///< list of segment interval specification
	int nb_times;          ///< number of elments in the times array

	char* frames_str;      ///< segment frame numbers specification string
	int* frames;           ///< list of frame number specification
	int nb_frames;         ///< number of elments in the frames array
	int frame_count;       ///< total number of reference frames
	int segment_frame_count; ///< number of reference frames in the segment

	int64_t time_delta;
	int  individual_header_trailer; /**< Set by a private option. */
	int  write_header_trailer; /**< Set by a private option. */
	char* header_filename;  ///< filename to write the output header to

	int reset_timestamps;  ///< reset timestamps at the beginning of each segment
	int64_t initial_offset;    ///< initial timestamps offset, expressed in microseconds
	char* reference_stream_specifier; ///< reference stream specifier
	int   reference_stream_index;
	int   break_non_keyframes;
	int   write_empty;

	int use_rename;
	char temp_list_filename[1024];

	SegmentListEntry cur_entry;
	SegmentListEntry* segment_list_entries;
	SegmentListEntry* segment_list_entries_end;
} SegmentContext;


#define URL_SCHEME_CHARS                        \
    "abcdefghijklmnopqrstuvwxyz"                \
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"                \
    "0123456789+-."

const struct { const char* name; int level; } log_levels[] = {
	{ "quiet"  , AV_LOG_QUIET   },
	{ "panic"  , AV_LOG_PANIC   },
	{ "fatal"  , AV_LOG_FATAL   },
	{ "error"  , AV_LOG_ERROR   },
	{ "warning", AV_LOG_WARNING },
	{ "info"   , AV_LOG_INFO    },
	{ "verbose", AV_LOG_VERBOSE },
	{ "debug"  , AV_LOG_DEBUG   },
	{ "trace"  , AV_LOG_TRACE   },
};

typedef struct context {
	const char* input;
	const char* output;
	bool frame;
	bool video;
	int duration;
	int extract_index;
	int loglevel;
	bool tcp;
	bool wait_start_frame;
	bool wait_key_frame;
	const char* error_message;
} AppContext;
void exit_program(int ret)
{
	exit(ret);
}
uint64_t get_timestamp()
{
	/// Convert seconds to nanoseconds
	uint64_t nanoseconds;
	struct timespec ts;
	int return_code = clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	if (return_code == -1)
	{
		printf("Failed to obtain timestamp. errno = %i: %s\n", errno,
			strerror(errno));
		nanoseconds = UINT64_MAX; // use this to indicate error
	}
	else
	{
		// `ts` now contains your timestamp in seconds and nanoseconds! To 
		// convert the whole struct to nanoseconds, do this:
		nanoseconds = SEC_TO_NS((uint64_t)ts.tv_sec) + (uint64_t)ts.tv_nsec;
	}
}

double ntp_timestamp(AVFormatContext *pFormatCtx, uint32_t *last_rtcp_ts, double *base_time) {
	RTSPState* rtsp_state = (RTSPState*) pFormatCtx->priv_data;
	RTSPStream* rtsp_stream = rtsp_state->rtsp_streams[0];
	RTPDemuxContext* rtp_demux_context = (RTPDemuxContext*) rtsp_stream->transport_priv;

	
	av_log(NULL, AV_LOG_DEBUG,"====================================\n");
	av_log(NULL, AV_LOG_DEBUG,"RTSP timestamps:\n");
	av_log(NULL, AV_LOG_DEBUG,"timestamp:                %u\n", rtp_demux_context->timestamp);
	av_log(NULL, AV_LOG_DEBUG,"base_timestamp:           %u\n", rtp_demux_context->base_timestamp);
	av_log(NULL, AV_LOG_DEBUG,"last_rtcp_ntp_time:       %lu\n", rtp_demux_context->last_rtcp_ntp_time);
	av_log(NULL, AV_LOG_DEBUG,"last_rtcp_reception_time: %lu\n", rtp_demux_context->last_rtcp_reception_time);
	av_log(NULL, AV_LOG_DEBUG,"first_rtcp_ntp_time:      %lu\n", rtp_demux_context->first_rtcp_ntp_time);
	av_log(NULL, AV_LOG_DEBUG,"last_rtcp_timestamp:      %u\n", rtp_demux_context->last_rtcp_timestamp);
	av_log(NULL, AV_LOG_DEBUG,"diff: %d\n",(rtp_demux_context->timestamp-rtp_demux_context->base_timestamp));
	av_log(NULL, AV_LOG_DEBUG,"====================================\n");
	
	uint32_t new_rtcp_ts = rtp_demux_context->last_rtcp_timestamp;
	uint64_t last_ntp_time = 0;
	uint32_t seconds = 0;
	uint32_t fraction = 0;
	double useconds = 0;
	int32_t d_ts = 0;

	if(new_rtcp_ts != *last_rtcp_ts){
		*last_rtcp_ts=new_rtcp_ts;
		last_ntp_time = rtp_demux_context->last_rtcp_ntp_time;
		seconds = ((last_ntp_time >> 32) & 0xffffffff)-2208988800;
		fraction  = (last_ntp_time & 0xffffffff);
		useconds = ((double) fraction / 0xffffffff);
		*base_time = seconds+useconds;
	}

	d_ts = rtp_demux_context->timestamp-*last_rtcp_ts;
	return *base_time+d_ts/90000.0;
}


int decode(int* got_frame, AVFrame* pFrame, AVCodecContext* pCodecCtx, AVPacket* packet) {
	int ret = 0;
	*got_frame = 0;

	ret = avcodec_send_packet(pCodecCtx, packet);

	if (ret < 0)
		return ret == AVERROR_EOF ? 0 : ret;

	ret = avcodec_receive_frame(pCodecCtx, pFrame);

	if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
		return ret;

	if (ret >= 0)
		*got_frame = 1;

	return 0;
}

int write_jpeg(AVCodecContext *pCodecCtx, AVFrame *pFrame, const char* jpeg_filename)
{
	AVCodecContext         *pOCodecCtx;
	const AVCodec                *pOCodec;
	uint8_t                *Buffer;
	int                     BufSiz;
	enum AVPixelFormat      ImgFmt = AV_PIX_FMT_YUVJ420P;
	FILE                   *jpeg_file;

	BufSiz = av_image_get_buffer_size(ImgFmt, pCodecCtx->width, pCodecCtx->height, 1);
	Buffer = (uint8_t *)malloc ( BufSiz );
	if ( Buffer == NULL )
	{
		av_log(NULL, AV_LOG_ERROR, "malloc for image failed \n");
		free(Buffer);
		return (0);
	}
	memset ( Buffer, 0, BufSiz );

	pOCodecCtx = avcodec_alloc_context3 ( NULL );
	if ( !pOCodecCtx ) 
	{
		av_log(NULL, AV_LOG_ERROR, "no pOCodecCtx\n");
		free ( Buffer );
		return ( 0 );
	}

	av_log(NULL, AV_LOG_TRACE, "Preparing file %s\n", jpeg_filename);
	pOCodecCtx->bit_rate      = pCodecCtx->bit_rate;
	pOCodecCtx->width         = pCodecCtx->width;
	pOCodecCtx->height        = pCodecCtx->height;
	pOCodecCtx->pix_fmt       = ImgFmt;
	pOCodecCtx->codec_id      = AV_CODEC_ID_MJPEG;
	pOCodecCtx->codec_type    = AVMEDIA_TYPE_VIDEO;
//	pOCodecCtx->time_base.num = pCodecCtx->time_base.num;
//	pOCodecCtx->time_base.den = pCodecCtx->time_base.den;
	pOCodecCtx->time_base = pCodecCtx->time_base;
	
	if ((pOCodecCtx->time_base.num == 0) || (pOCodecCtx->time_base.den == 0))
	{
		av_log(NULL, AV_LOG_WARNING, "pOCodecCtx->time_base = 0\n");
		pOCodecCtx->time_base.num = 1;
		pOCodecCtx->time_base.den = 1;
	}
	pOCodec = avcodec_find_encoder ( pOCodecCtx->codec_id );
	if ( !pOCodec ) 
	{
		av_log(NULL, AV_LOG_ERROR, "no pOCodec\n");
		free ( Buffer );
		return ( 0 );
	}

	if ( avcodec_open2 ( pOCodecCtx, pOCodec, NULL ) < 0 )
	{
		av_log(NULL, AV_LOG_ERROR, "avcodec_open2 failed\n");
		free ( Buffer );
		return ( 0 );
	}
	
	pOCodecCtx->mb_lmin = pOCodecCtx->qmin * FF_QP2LAMBDA;
	pOCodecCtx->mb_lmax = pOCodecCtx->qmax * FF_QP2LAMBDA;

	pOCodecCtx->flags = AV_CODEC_FLAG_QSCALE;
	pOCodecCtx->global_quality = pOCodecCtx->qmin * FF_QP2LAMBDA;

	pFrame->pts = 1;
	pFrame->quality = pOCodecCtx->global_quality;

	AVPacket pOutPacket;
	pOutPacket.data = Buffer;
	pOutPacket.size = BufSiz;

	int got_packet_ptr = 0;
	int result = avcodec_send_frame(pOCodecCtx, pFrame);
	if (result == AVERROR_EOF)
	{
		av_log(NULL, AV_LOG_ERROR, "avcodec_send_frame failed: AVERROR_EOF\n");
		free(Buffer);
		return (0);
	}
	else if (result < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "avcodec_send_frame failed\n");
		free(Buffer);
		return (0);
	}
	else {
		av_log(NULL, AV_LOG_TRACE, "Opening file %s\n", jpeg_filename);
		jpeg_file = fopen (jpeg_filename, "wb" );
		if (jpeg_file) {
			AVPacket* pkt = av_packet_alloc();
			if (pkt != NULL) {
				while (avcodec_receive_packet(pOCodecCtx, pkt) == 0) {
					fwrite(pkt->data, 1, pkt->size, jpeg_file);
					av_packet_unref(pkt);
				}
				av_packet_free(&pkt);
				fclose(jpeg_file);
			}
			else {
				av_log(NULL, AV_LOG_ERROR, "av_packet_alloc failed\n");
				free(Buffer);
				return (0);
			}
		}
		else
		{
			av_log(NULL, AV_LOG_ERROR, "Error while opening file %s\n", jpeg_filename);
			free(Buffer);
			return (0);
		}
	}
	avcodec_close ( pOCodecCtx );
	free ( Buffer );
	return ( BufSiz );
}
const char* get_log_level(int level)
{
	int j = 0;
	for (j = 0; j < FF_ARRAY_ELEMS(log_levels); j++) {
		if (log_levels[j].level == level) {
			return log_levels[j].name;
		}
	}
	return log_levels[j].name;
}
int parse_command_line(int argc, char* argv[], AppContext* pContext)
{
	const char* token;
	const char* plevel;
	bool level_found = false;
	int  i = 1;
	int j = 0;


	if (pContext) {
		while ((i < argc) && (argv[i])) {			
			token = argv[i++];
			

			if (*token == '-' ) {
				switch (*(++token))
				{
				case 'i':
					if (i < argc) {
						pContext->input = av_strdup(argv[i++]);
					}
					break;
				case 'f':
					if (i < argc) {
						pContext->frame = true;
					}
					break;
				case 'c':
					if (i < argc) {
						pContext->video = true;
					}
					break;
				case 'o':
					if (i < argc) {;
						pContext->output = av_strdup(argv[i++]);
					}
					break;
				case 'e':
					if (i < argc) {
						pContext->extract_index = atoi(argv[i++]);
					}
					break;
				case 'd':
					if (i < argc) {
						pContext->duration = atoi(argv[i++]);
					}
					break;
				case 's':
					pContext->wait_start_frame = true;
					break;					
				case 'k':
					pContext->wait_key_frame = true;
					break;					
				case 'v':
					if (i < argc) {
						plevel = argv[i++];
						for (j = 0; j < FF_ARRAY_ELEMS(log_levels); j++) {
							if (!strcmp(log_levels[j].name, plevel)) {
								pContext->loglevel = log_levels[j].level;
								av_log_set_level(pContext->loglevel);
								level_found = true;
							}
						}
						if (level_found == false) {
							pContext->error_message = av_strdup("log level incorrect");
							return (0);
						}
					}
					break;
				default:
					pContext->error_message = av_strdup("Option not valid");
					return (0);
					break;

				};
			}
			else {
				pContext->error_message = av_strdup("Option expected: -");
				return (0);
			}
		}
		if((pContext->input)&&
			(((pContext->frame == true) && (pContext->video == false)) || ((pContext->frame == false) && (pContext->video == true)))&&
			(pContext->output))
			return ( 1 );
		else
		{
			pContext->error_message = av_strdup("input or output or frame option or chunk option parameters no set");
			return (0);
		}
	}
	return ( 0 );
}
int show_command_line(AppContext* pContext, const char* pVersion)
{
	if (pContext) {
		if (pContext->error_message) {
			printf("Extractframe error: %s\n", pContext->error_message);
		}
	}
	printf("rtspextract version: %s\nExtract frame Syntax: \n   rtspextract -f -i input -e rate -o output -k -s -v [quiet|panic|fatal|error|warning|info|verbose|debug|trace]\nExtract chunk Syntax:\n   rtspextract -c -i input -d duration -o output -k -s -v [quiet|panic|fatal|error|warning|info|verbose|debug|trace]\n", pVersion);
	
	return( 0 );
}
AppContext* av_context_alloc(void)
{
	AppContext* pctx = av_mallocz(sizeof(AppContext));
	if (!pctx)
		 return pctx;
	pctx->input = NULL;
	pctx->output = NULL;
	pctx->tcp = true;
	pctx->frame = false;
	pctx->video = false;
	pctx->duration = 10;
	pctx->wait_start_frame = false;
	pctx->wait_key_frame = false;
	pctx->extract_index = 30;
	pctx->loglevel = AV_LOG_INFO;
	pctx->error_message = NULL;
	return pctx;
}
int get_time_string(double ts, char* pbuf, int lbuf)
{
	struct timeval tv;
	struct tm *tm;
	uint32_t sec = 0;
	uint32_t usec = 0;
	double diff = 0;
	sec = (uint32_t) ts;
	diff = ts-sec;
	usec = diff*1000000;
	tv.tv_sec = sec;
	tv.tv_usec = usec;
	tm=localtime(&tv.tv_sec);
	snprintf(pbuf, lbuf,"%04d/%02d/%02d-%02d:%02d:%02d-%06ld", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min,
		tm->tm_sec, tv.tv_usec);
	return 1;
}
int isStartFrame(uint8_t *data[4])
{

	int start_frame = 1;
	for (int i = 0; i < 4; i++)
	{
		av_log(NULL, AV_LOG_DEBUG, "Index: %d - pointeur:         %p\n",i,data[i]);
		if(data[i])
		{
			for (int j = 0; j < 16; j++)
			{
				av_log(NULL, AV_LOG_DEBUG, "  Index: %d - value:            %02X\n",i,data[i][j]);
				if(data[i][j] != 0)
					return 0;					
			}
		}
		/*
		if( (*(data[0]+i)!=0) ||
			(*(data[0]+i)!=0) ||
			(*(data[2]+i)!=0)  ||
			(*(data[3]+i)!=0))
			return 0;
		*/
	}
	return 1;
}
int is_rtsp_source(const char* source)
{
	if((source[0] == 'r')&&
	(source[1] == 't')&&
	(source[2] == 's')&&
	(source[3] == 'p')&&
	(source[4] == ':'))
		return 1;
	return 0;
}
double get_frame_rate(AVFormatContext   *pFormatCtx){
	int VideoStreamIndx = -1;
	/* find first stream */
	for(int i=0; i<pFormatCtx->nb_streams ;i++ )
	{
		if( pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ) 
		/* if video stream found then get the index */
		{
		VideoStreamIndx = i;
		break;
		}
	}
	if(VideoStreamIndx == -1)
		return -1;
	return av_q2d(pFormatCtx->streams[VideoStreamIndx]->r_frame_rate);
}
static int extractchunk_init(AVFormatContext* s, int64_t t)
{
	SegmentContext* seg = s->priv_data;
	if (seg != NULL) {
		seg->use_clocktime = 0;
		seg->time = t;
		seg->use_strftime = 1;
		seg->time_delta = 0;
		seg->reset_timestamps = 0;
	}
	return 0;
}
/* Add an output stream. */
static void add_stream(OutputStream* ost, AVFormatContext* oc,
	const AVCodec** codec,
	enum AVCodecID codec_id)
{
	AVCodecContext* c;
	int i;

	/* find the encoder */
	*codec = avcodec_find_encoder(codec_id);
	if (!(*codec)) {
		fprintf(stderr, "Could not find encoder for '%s'\n",
			avcodec_get_name(codec_id));
		exit(1);
	}

	ost->tmp_pkt = av_packet_alloc();
	if (!ost->tmp_pkt) {
		fprintf(stderr, "Could not allocate AVPacket\n");
		exit(1);
	}

	ost->st = avformat_new_stream(oc, NULL);
	if (!ost->st) {
		fprintf(stderr, "Could not allocate stream\n");
		exit(1);
	}
	ost->st->id = oc->nb_streams - 1;
	c = avcodec_alloc_context3(*codec);
	if (!c) {
		fprintf(stderr, "Could not alloc an encoding context\n");
		exit(1);
	}
	ost->enc = c;

	switch ((*codec)->type) {
	case AVMEDIA_TYPE_AUDIO:
		c->sample_fmt = (*codec)->sample_fmts ?
			(*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
		c->bit_rate = 64000;
		c->sample_rate = 44100;
		if ((*codec)->supported_samplerates) {
			c->sample_rate = (*codec)->supported_samplerates[0];
			for (i = 0; (*codec)->supported_samplerates[i]; i++) {
				if ((*codec)->supported_samplerates[i] == 44100)
					c->sample_rate = 44100;
			}
		}
		av_channel_layout_copy(&c->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO);
		ost->st->time_base = (AVRational){ 1, c->sample_rate };
		break;

	case AVMEDIA_TYPE_VIDEO:
		c->codec_id = codec_id;

		c->bit_rate = 400000;
		/* Resolution must be a multiple of two. */
		c->width = 352;
		c->height = 288;
		/* timebase: This is the fundamental unit of time (in seconds) in terms
		 * of which frame timestamps are represented. For fixed-fps content,
		 * timebase should be 1/framerate and timestamp increments should be
		 * identical to 1. */
		ost->st->time_base = (AVRational){ 1, STREAM_FRAME_RATE };
		c->time_base = ost->st->time_base;

		c->gop_size = 12; /* emit one intra frame every twelve frames at most */
		c->pix_fmt = STREAM_PIX_FMT;
		if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
			/* just for testing, we also add B-frames */
			c->max_b_frames = 2;
		}
		if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
			/* Needed to avoid using macroblocks in which some coeffs overflow.
			 * This does not happen with normal video, it just happens here as
			 * the motion of the chroma plane does not match the luma plane. */
			c->mb_decision = 2;
		}
		break;

	default:
		break;
	}

	/* Some formats want stream headers to be separate. */
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* audio output */

static AVFrame* alloc_audio_frame(enum AVSampleFormat sample_fmt,
	const AVChannelLayout* channel_layout,
	int sample_rate, int nb_samples)
{
	AVFrame* frame = av_frame_alloc();
	int ret;

	if (!frame) {
		fprintf(stderr, "Error allocating an audio frame\n");
		exit(1);
	}

	frame->format = sample_fmt;
	av_channel_layout_copy(&frame->ch_layout, channel_layout);
	frame->sample_rate = sample_rate;
	frame->nb_samples = nb_samples;

	if (nb_samples) {
		ret = av_frame_get_buffer(frame, 0);
		if (ret < 0) {
			fprintf(stderr, "Error allocating an audio buffer\n");
			exit(1);
		}
	}

	return frame;
}
static void open_audio(AVFormatContext* oc, const AVCodec* codec,
	OutputStream* ost, AVDictionary* opt_arg)
{
	AVCodecContext* c;
	int nb_samples;
	int ret;
	AVDictionary* opt = NULL;

	c = ost->enc;

	/* open it */
	av_dict_copy(&opt, opt_arg, 0);
	ret = avcodec_open2(c, codec, &opt);
	av_dict_free(&opt);
	if (ret < 0) {
		fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
		exit(1);
	}

	/* init signal generator */
	ost->t = 0;
	ost->tincr = 2 * M_PI * 110.0 / c->sample_rate;
	/* increment frequency by 110 Hz per second */
	ost->tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;

	if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
		nb_samples = 10000;
	else
		nb_samples = c->frame_size;

	ost->frame = alloc_audio_frame(c->sample_fmt, &c->ch_layout,
		c->sample_rate, nb_samples);
	ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, &c->ch_layout,
		c->sample_rate, nb_samples);

	/* copy the stream parameters to the muxer */
	ret = avcodec_parameters_from_context(ost->st->codecpar, c);
	if (ret < 0) {
		fprintf(stderr, "Could not copy the stream parameters\n");
		exit(1);
	}

	/* create resampler context */
	ost->swr_ctx = swr_alloc();
	if (!ost->swr_ctx) {
		fprintf(stderr, "Could not allocate resampler context\n");
		exit(1);
	}

	/* set options */
	av_opt_set_chlayout(ost->swr_ctx, "in_chlayout", &c->ch_layout, 0);
	av_opt_set_int(ost->swr_ctx, "in_sample_rate", c->sample_rate, 0);
	av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
	av_opt_set_chlayout(ost->swr_ctx, "out_chlayout", &c->ch_layout, 0);
	av_opt_set_int(ost->swr_ctx, "out_sample_rate", c->sample_rate, 0);
	av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt", c->sample_fmt, 0);

	/* initialize the resampling context */
	if ((ret = swr_init(ost->swr_ctx)) < 0) {
		fprintf(stderr, "Failed to initialize the resampling context\n");
		exit(1);
	}
}
/**************************************************************/
/* video output */

static AVFrame* alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
	AVFrame* picture;
	int ret;

	picture = av_frame_alloc();
	if (!picture)
		return NULL;

	picture->format = pix_fmt;
	picture->width = width;
	picture->height = height;

	/* allocate the buffers for the frame data */
	ret = av_frame_get_buffer(picture, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate frame data.\n");
		exit(1);
	}

	return picture;
}

static void open_video(AVFormatContext* oc, const AVCodec* codec,
	OutputStream* ost, AVDictionary* opt_arg)
{
	int ret;
	AVCodecContext* c = ost->enc;
	AVDictionary* opt = NULL;

	av_dict_copy(&opt, opt_arg, 0);

	/* open the codec */
	ret = avcodec_open2(c, codec, &opt);
	av_dict_free(&opt);
	if (ret < 0) {
		fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
		exit(1);
	}

	/* allocate and init a re-usable frame */
	ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
	if (!ost->frame) {
		fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}

	/* If the output format is not YUV420P, then a temporary YUV420P
	 * picture is needed too. It is then converted to the required
	 * output format. */
	ost->tmp_frame = NULL;
	if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
		ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
		if (!ost->tmp_frame) {
			fprintf(stderr, "Could not allocate temporary picture\n");
			exit(1);
		}
	}

	/* copy the stream parameters to the muxer */
	ret = avcodec_parameters_from_context(ost->st->codecpar, c);
	if (ret < 0) {
		fprintf(stderr, "Could not copy the stream parameters\n");
		exit(1);
	}
}

static int extractchunk(AppContext* papp_context) {
	AVCodecParameters* origin_par = NULL;
	const AVCodec* pCodec = NULL;
	AVFormatContext* pFormatCtx = NULL;
	AVFormatContext* oFormatCtx = NULL;
	AVCodecContext* pCodecCtx = NULL;
	AVFrame* pFrame = NULL;
	AVPacket* pPacket;
	int               videoStream = -1;
	//uint32_t		  frame_size = 1920*1080*4;
	uint8_t			  network_mode = 1;
	char              time_buffer[256];
	int result;
	const AVOutputFormat* ff_segment_muxer;
	OutputStream video_st = { 0 }, audio_st = { 0 };
	int ret;
	int have_video = 0, have_audio = 0;
	int encode_video = 0, encode_audio = 0;
	const AVOutputFormat* fmt;
	AVFormatContext* oc;
	const AVCodec* audio_codec, * video_codec;
	const char* filename;
	AVDictionary* opt = NULL;
	const char* rtsp_source = papp_context->input;
	avformat_network_init();
	pPacket = av_packet_alloc();
	AVDictionary* opts = NULL;
	av_dict_set(&opts, "stimeout", "5000000", 0);

	if (network_mode == 0) {
		av_log(NULL, AV_LOG_INFO, "Opening UDP stream\n");
		result = avformat_open_input(&pFormatCtx, rtsp_source, NULL, &opts);
	}
	else {
		av_dict_set(&opts, "rtsp_transport", "tcp", 0);
		av_log(NULL, AV_LOG_INFO, "Opening TCP stream\n");
		result = avformat_open_input(&pFormatCtx, rtsp_source, NULL, &opts);
	}

	if (result < 0) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't open stream\n");
		return 0;
	}

	result = avformat_find_stream_info(pFormatCtx, NULL);
	if (result < 0) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't find stream information\n");
		return 0;
	}

	videoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (videoStream == -1) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't find video stream\n");
		return 0;
	}

	origin_par = pFormatCtx->streams[videoStream]->codecpar;
	pCodec = avcodec_find_decoder(origin_par->codec_id);

	if (pCodec == NULL) {
		av_log(NULL, AV_LOG_ERROR, "Unsupported codec\n");
		return 0;
	}

	pCodecCtx = avcodec_alloc_context3(pCodec);

	if (pCodecCtx == NULL) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't allocate codec context\n");
		return 0;
	}

	result = avcodec_parameters_to_context(pCodecCtx, origin_par);
	if (result) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't copy decoder context\n");
		return 0;
	}

	result = avcodec_open2(pCodecCtx, pCodec, NULL);
	if (result < 0) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't open decoder\n");
		return 0;
	}

	pFrame = av_frame_alloc();
	if (pFrame == NULL) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't allocate frame\n");
		return 0;
	}

	int byte_buffer_size = av_image_get_buffer_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, 16);
	//byte_buffer_size = byte_buffer_size < frame_size ? byte_buffer_size : frame_size;

	int got_frame;
	uint32_t last_rtcp_ts = 0;
	double base_time = 0;

	uint8_t* frame_data = NULL;

	FILE* output_file = NULL;
	AVFrame* pFrameRGB = NULL;
	int               numBytes;
	uint8_t* buffer = NULL;
	struct SwsContext* sws_ctx = NULL;
	int i = 0;
	bool start_frame_detected = false;
	double start_frame_ts = 0;
	// if wait_key_frame or wait_start_frame are true the capture will not start immmediatly
	if ((papp_context->wait_key_frame == true) || (papp_context->wait_start_frame == true))
		i = -1;

	// Allocate an AVFrame structure
	pFrameRGB = av_frame_alloc();
	if (pFrameRGB == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "Couldn't allocate RGB frame\n");
		return -1;
	}

	// Determine required buffer size and allocate buffer
	numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);
	buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

	// Assign appropriate parts of buffer to image planes in pFrameRGB
	// Note that pFrameRGB is an AVFrame, but AVFrame is a superset
	// of AVPicture
	av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer,
		AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);

	// initialize SWS context for software scaling
	sws_ctx = sws_getContext(pCodecCtx->width,
		pCodecCtx->height,
		pCodecCtx->pix_fmt,
		pCodecCtx->width,
		pCodecCtx->height,
		AV_PIX_FMT_RGB24,
		SWS_BILINEAR,
		NULL,
		NULL,
		NULL);

	bool rtsp = false;

	if (is_rtsp_source(rtsp_source)) {
		rtsp = true;
		av_log(NULL, AV_LOG_INFO, "RTSP source\n");
	}
	else {
		av_log(NULL, AV_LOG_INFO, "Not RTSP source\n");
	}
	double fps = get_frame_rate(pFormatCtx);

	start_frame_detected = false;
	
	av_log(NULL, AV_LOG_ERROR, "av_guess_format\n");
	ff_segment_muxer = av_guess_format("segment", NULL, NULL);
	if(ff_segment_muxer == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "Muxer for segment not found\n");
		return -1;
	}
	av_log(NULL, AV_LOG_ERROR, "Segment class name: %s\n", ff_segment_muxer->priv_class->class_name);
	av_log(NULL, AV_LOG_ERROR, "avformat_alloc_output_context2\n");
	
	//ret = avformat_alloc_output_context2(&oFormatCtx, ff_segment_muxer, NULL, papp_context->output);
	ret = avformat_alloc_output_context2(&pFormatCtx, ff_segment_muxer, NULL, papp_context->output);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Can't allocate Context with segment\n");
		return ret;
	}
//	av_log(NULL, AV_LOG_ERROR, "avformat_alloc_output_context2 success: url %s\n", oFormatCtx->url);
	av_log(NULL, AV_LOG_ERROR, "avformat_alloc_output_context2 success: url %s\n", pFormatCtx->url);
//	extractchunk_init(oFormatCtx, papp_context->duration);
	extractchunk_init(pFormatCtx, papp_context->duration);
	av_log(NULL, AV_LOG_ERROR, "extractchunk_init\n");

//	fmt = oFormatCtx->oformat;
	fmt = pFormatCtx->oformat;

	/* Add the audio and video streams using the default format codecs
	 * and initialize the codecs. */
	if((pFormatCtx->iformat != NULL)&&(pFormatCtx->iformat->name != NULL))
		av_log(NULL, AV_LOG_ERROR, "audio_codec id: %s\n", pFormatCtx->iformat->name);
	if ((pFormatCtx->iformat != NULL) && (pFormatCtx->iformat->codec_tag != NULL))
		av_log(NULL, AV_LOG_ERROR, "video_codec id: %d\n", (**pFormatCtx->iformat->codec_tag).id);
//	fmt->video_codec = pFormatCtx->video_codec_id;
//	fmt->audio_codec = pFormatCtx->audio_codec_id;

	av_log(NULL, AV_LOG_ERROR, "video_codec0\n");
	//if (fmt->video_codec != AV_CODEC_ID_NONE) {
	if (fmt->video_codec == AV_CODEC_ID_NONE) {
			av_log(NULL, AV_LOG_ERROR, "video_codec\n");
		//add_stream(&video_st, oc, &video_codec, fmt->video_codec);
		add_stream(&video_st, oc, &video_codec, AV_CODEC_ID_H264);
		have_video = 1;
		encode_video = 1;
		av_log(NULL, AV_LOG_ERROR, "video_codec done \n");
	}
	av_log(NULL, AV_LOG_ERROR, "audio_codec0\n");
	if (fmt->audio_codec != AV_CODEC_ID_NONE) {
		av_log(NULL, AV_LOG_ERROR, "audio_codec\n");
		add_stream(&audio_st, oc, &audio_codec, fmt->audio_codec);
		have_audio = 1;
		encode_audio = 1;
		av_log(NULL, AV_LOG_ERROR, "audio_codec done \n");
	}

	/* Now that all the parameters are set, we can open the audio and
	 * video codecs and allocate the necessary encode buffers. */
	if (have_video)
		open_video(oc, video_codec, &video_st, opt);

	if (have_audio)
		open_audio(oc, audio_codec, &audio_st, opt);

	av_dump_format(oc, 0, filename, 1);

	ff_segment_muxer->init(pFormatCtx);
	av_log(NULL, AV_LOG_ERROR, "extractchunk_init nb_streams: %d \n", pFormatCtx->nb_streams);


	while (av_read_frame(pFormatCtx, pPacket) >= 0) {
		if (decode(&got_frame, pFrame, pCodecCtx, pPacket) < 0) {
			av_log(NULL, AV_LOG_ERROR, "Decoding error\n");
			break;
		}

		if (got_frame) {

			av_log(NULL, AV_LOG_DEBUG, "====================================\n");
			av_log(NULL, AV_LOG_DEBUG, "Packet:\n");
			av_log(NULL, AV_LOG_DEBUG, "size:               %u\n", pPacket->size);
			av_log(NULL, AV_LOG_DEBUG, "dts:                %lu\n", pPacket->dts);
			av_log(NULL, AV_LOG_DEBUG, "pts:                %lu\n", pPacket->pts);
			av_log(NULL, AV_LOG_DEBUG, "stream_index:       %u\n", pPacket->stream_index);
			av_log(NULL, AV_LOG_DEBUG, "duration:           %lu\n", pPacket->duration);
			av_log(NULL, AV_LOG_DEBUG, "pos:                %lu\n", pPacket->pos);
			av_log(NULL, AV_LOG_DEBUG, "====================================\n");

			double ts = 0;
			if (rtsp == true)
				ts = ntp_timestamp(pFormatCtx, &last_rtcp_ts, &base_time);
			else {
				//av_log(NULL, AV_LOG_INFO, "DTS: %lu %lu %lu Duration: %lu FPS %lu/%lu fps: %f \n",pPacket->dts,pFrame->pts,pFrame->pkt_dts,pPacket->duration,pFrame->time_base.num,pFrame->time_base.den,fps);
				//ts = pPacket->dts/12800.0;
				if ((pPacket->duration > 0) && (pPacket->dts > 0) && (fps > 0))
					ts = pPacket->dts / (pPacket->duration * fps);
				else
					ts = pPacket->dts;
				//av_log(NULL, AV_LOG_INFO, "DTS: %lu\n",pPacket->dts);
			}
			av_log(NULL, AV_LOG_INFO, "Timestamp: %f %d %d\n",ts,pFrame->key_frame,i);
			//av_log(NULL, AV_LOG_DEBUG, "Timestamp: %018.6f\n", ts);
			if (get_time_string(ts, time_buffer, sizeof(time_buffer)))
			{
				av_log(NULL, AV_LOG_DEBUG, "Time:      %s\n", time_buffer);
			}
			// if wait_key_frame and key_frame detected capture can start
			if (pFrame->key_frame && (papp_context->wait_key_frame == true) && (ts > 1667466981))
				i = 0;


			// if wait_start_frame and key_frame detected and ts valid, capture can start if start frame detected
			if (pFrame->key_frame && (papp_context->wait_start_frame == true) && (((rtsp == true) && (ts > 1667466981)) || ((rtsp == false) && (ts >= 0))))
			{
				if (isStartFrame(pFrameRGB->data)) {
					av_log(NULL, AV_LOG_WARNING, "Timestamp: %018.6f - Start Frame detected\n", ts);
					start_frame_detected = true;
				}
				else
				{
					// is first video frame detected after start frame 
					if (start_frame_detected == true) {
						start_frame_detected = false;
						start_frame_ts = ts;
						i = 0;
					}
					av_log(NULL, AV_LOG_INFO, "Timestamp: %018.6f - Video Frame detected after start frame\n", ts);
				}

			}
			if (i >= (papp_context->extract_index-1)) {
				i = 0;
			}
			// Save the frame to disk
			if ((i >= 0) && (start_frame_detected == false))
			{
				// Start recording
				av_log(NULL, AV_LOG_INFO, "Timestamp: %018.6f - Start recording\n", ts);
				ff_segment_muxer->write_packet(pFormatCtx, pPacket);
			}
			if (i >= 0) {
				i++;
			}
			av_packet_unref(pPacket);
		}
	}
	ff_segment_muxer->deinit(oFormatCtx);

	return 0;
}


int extractframe(AppContext* papp_context) {
	AVCodecParameters* origin_par = NULL;
	const AVCodec* pCodec = NULL;
	AVFormatContext* pFormatCtx = NULL;
	AVCodecContext* pCodecCtx = NULL;
	AVFrame* pFrame = NULL;
	AVPacket* pPacket;
	int               videoStream = -1;
	//uint32_t		  frame_size = 1920*1080*4;
	uint8_t			  network_mode = 1;
	char              time_buffer[256];
	int result;

	const char* rtsp_source = papp_context->input;
	avformat_network_init();
	pPacket = av_packet_alloc();
	AVDictionary* opts = NULL;
	av_dict_set(&opts, "stimeout", "5000000", 0);

	if (network_mode == 0) {
		av_log(NULL, AV_LOG_INFO, "Opening UDP stream\n");
		result = avformat_open_input(&pFormatCtx, rtsp_source, NULL, &opts);
	}
	else {
		av_dict_set(&opts, "rtsp_transport", "tcp", 0);
		av_log(NULL, AV_LOG_INFO, "Opening TCP stream\n");
		result = avformat_open_input(&pFormatCtx, rtsp_source, NULL, &opts);
	}

	if (result < 0) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't open stream\n");
		return 0;
	}

	result = avformat_find_stream_info(pFormatCtx, NULL);
	if (result < 0) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't find stream information\n");
		return 0;
	}

	videoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (videoStream == -1) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't find video stream\n");
		return 0;
	}

	origin_par = pFormatCtx->streams[videoStream]->codecpar;
	pCodec = avcodec_find_decoder(origin_par->codec_id);

	if (pCodec == NULL) {
		av_log(NULL, AV_LOG_ERROR, "Unsupported codec\n");
		return 0;
	}

	pCodecCtx = avcodec_alloc_context3(pCodec);

	if (pCodecCtx == NULL) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't allocate codec context\n");
		return 0;
	}

	result = avcodec_parameters_to_context(pCodecCtx, origin_par);
	if (result) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't copy decoder context\n");
		return 0;
	}

	result = avcodec_open2(pCodecCtx, pCodec, NULL);
	if (result < 0) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't open decoder\n");
		return 0;
	}

	pFrame = av_frame_alloc();
	if (pFrame == NULL) {
		av_log(NULL, AV_LOG_ERROR, "Couldn't allocate frame\n");
		return 0;
	}

	int byte_buffer_size = av_image_get_buffer_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, 16);
	//byte_buffer_size = byte_buffer_size < frame_size ? byte_buffer_size : frame_size;

	int number_of_written_bytes;
	int got_frame;
	uint32_t last_rtcp_ts = 0;
	double base_time = 0;

	uint8_t* frame_data = NULL;

	FILE* output_file = NULL;
	AVFrame* pFrameRGB = NULL;
	int               numBytes;
	uint8_t* buffer = NULL;
	struct SwsContext* sws_ctx = NULL;
	int i = 0;
	bool start_frame_detected = false;
	double start_frame_ts = 0;
	// if wait_key_frame or wait_start_frame are true the capture will not start immmediatly
	if ((papp_context->wait_key_frame == true) || (papp_context->wait_start_frame == true))
		i = -1;

	// Allocate an AVFrame structure
	pFrameRGB = av_frame_alloc();
	if (pFrameRGB == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "Couldn't allocate RGB frame\n");
		return -1;
	}

	// Determine required buffer size and allocate buffer
	numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);
	buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

	// Assign appropriate parts of buffer to image planes in pFrameRGB
	// Note that pFrameRGB is an AVFrame, but AVFrame is a superset
	// of AVPicture
	av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer,
		AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);

	// initialize SWS context for software scaling
	sws_ctx = sws_getContext(pCodecCtx->width,
		pCodecCtx->height,
		pCodecCtx->pix_fmt,
		pCodecCtx->width,
		pCodecCtx->height,
		AV_PIX_FMT_RGB24,
		SWS_BILINEAR,
		NULL,
		NULL,
		NULL);

	bool rtsp = false;

	if (is_rtsp_source(rtsp_source)) {
		rtsp = true;
		av_log(NULL, AV_LOG_INFO, "RTSP source\n");
	}
	else {
		av_log(NULL, AV_LOG_INFO, "Not RTSP source\n");
	}
	double fps = get_frame_rate(pFormatCtx);

	start_frame_detected = false;

	while (av_read_frame(pFormatCtx, pPacket) >= 0) {
		if (decode(&got_frame, pFrame, pCodecCtx, pPacket) < 0) {
			av_log(NULL, AV_LOG_ERROR, "Decoding error\n");
			break;
		}

		if (got_frame) {

			av_log(NULL, AV_LOG_DEBUG, "====================================\n");
			av_log(NULL, AV_LOG_DEBUG, "Packet:\n");
			av_log(NULL, AV_LOG_DEBUG, "size:               %u\n", pPacket->size);
			av_log(NULL, AV_LOG_DEBUG, "dts:                %lu\n", pPacket->dts);
			av_log(NULL, AV_LOG_DEBUG, "pts:                %lu\n", pPacket->pts);
			av_log(NULL, AV_LOG_DEBUG, "stream_index:       %u\n", pPacket->stream_index);
			av_log(NULL, AV_LOG_DEBUG, "duration:           %lu\n", pPacket->duration);
			av_log(NULL, AV_LOG_DEBUG, "pos:                %lu\n", pPacket->pos);
			av_log(NULL, AV_LOG_DEBUG, "====================================\n");

			double ts = 0;
			if (rtsp == true)
				ts = ntp_timestamp(pFormatCtx, &last_rtcp_ts, &base_time);
			else {
				//av_log(NULL, AV_LOG_INFO, "DTS: %lu %lu %lu Duration: %lu FPS %lu/%lu fps: %f \n",pPacket->dts,pFrame->pts,pFrame->pkt_dts,pPacket->duration,pFrame->time_base.num,pFrame->time_base.den,fps);
				//ts = pPacket->dts/12800.0;
				if ((pPacket->duration > 0) && (pPacket->dts > 0) && (fps > 0))
					ts = pPacket->dts / (pPacket->duration * fps);
				else
					ts = pPacket->dts;
				//av_log(NULL, AV_LOG_INFO, "DTS: %lu\n",pPacket->dts);
			}
			av_log(NULL, AV_LOG_INFO, "Timestamp: %f %d %d\n",ts,pFrame->key_frame,i);
			// Comment this line to get a lighter logs
			//av_log(NULL, AV_LOG_INFO, "Extract Timestamp: %018.6f\n", ts);
			if (get_time_string(ts, time_buffer, sizeof(time_buffer)))
			{
				av_log(NULL, AV_LOG_DEBUG, "Time:      %s\n", time_buffer);
			}
			// if wait_key_frame and key_frame detected capture can start
			if (pFrame->key_frame && (papp_context->wait_key_frame == true) && (ts > 0))
				i = 0;


			// if wait_start_frame and key_frame detected and ts valid, capture can start if start frame detected
			if (pFrame->key_frame && (papp_context->wait_start_frame == true) && (((rtsp == true) && (ts > 1667466981)) || ((rtsp == false) && (ts >= 0))))
			{
				// Convert the image from its native format to RGB
				sws_scale(sws_ctx, (uint8_t const* const*)pFrame->data,
					pFrame->linesize, 0, pCodecCtx->height,
					pFrameRGB->data, pFrameRGB->linesize);

				if (isStartFrame(pFrameRGB->data)) {
					av_log(NULL, AV_LOG_WARNING, "Timestamp: %018.6f - Start Frame detected\n", ts);
					start_frame_detected = true;
				}
				else
				{
					// is first video frame detected after start frame 
					if (start_frame_detected == true) {
						start_frame_detected = false;
						start_frame_ts = ts;
						i = 0;
					}
					av_log(NULL, AV_LOG_DEBUG, "Timestamp: %018.6f - Video Frame detected\n", ts);
				}

			}
			if (i >= papp_context->extract_index) {
				i = 0;
			}
			// Save the frame to disk
			if ((i == 0) && (start_frame_detected == false))
			{
				char ts_string[256];
				char jpeg_filename[256];
				double relative_ts = ts - start_frame_ts;
				uint64_t last_time = 0;
				uint64_t new_time = 0;
				uint64_t diff_time = 0;
				last_time = get_timestamp();

				if (papp_context->wait_start_frame == true)
					// snprintf(ts_string, sizeof(ts_string), "%018.6f-%018.6f", ts, relative_ts);
					snprintf(ts_string, sizeof(ts_string), "%018.6f", relative_ts);
				else
					snprintf(ts_string, sizeof(ts_string), "%018.6f", ts);
				snprintf(jpeg_filename, sizeof(jpeg_filename), papp_context->output, ts_string);

				// Don't record frame before receiving the first start frame
				if ((papp_context->wait_start_frame == false) || (start_frame_ts != 0))
				{
					av_log(NULL, AV_LOG_WARNING, "Timestamp: %018.6f - Creating file: %s\n", ts, jpeg_filename);
					// Convert the image from its native format to RGB
					sws_scale(sws_ctx, (uint8_t const* const*)pFrame->data,
						pFrame->linesize, 0, pCodecCtx->height,
						pFrameRGB->data, pFrameRGB->linesize);
					if (write_jpeg(pCodecCtx, pFrame, jpeg_filename) <= 0) {
						av_log(NULL, AV_LOG_ERROR, "Error while storing the frame\n");
						break;
					}
				}

				new_time = get_timestamp();
				diff_time = new_time - last_time;
				av_log(NULL, AV_LOG_DEBUG, "Time: %"PRIu64"  File %s created in %"PRIu64" ns \n", get_timestamp(), jpeg_filename, diff_time);
			}
			if (i >= 0) {
				i++;
			}
			av_packet_unref(pPacket);
		}
	}
	return 0;
}

int main(int argc, char* argv[]) {
	AppContext* papp_context = NULL;
	const char* pVersion = "1.0.0.1";

	papp_context = av_context_alloc();
	if (parse_command_line(argc, argv, papp_context) <= 0) {
		show_command_line(papp_context, pVersion);
		exit_program(1);
	}



	if (papp_context->frame == true) {
		av_log(NULL, AV_LOG_INFO, "Launching application to extract frames from rtsp stream with: \n  Input: %s \n  rtsp source: %s\n  Output: %s \n  Rate: %d \n  wait for key frame: %s \n  wait for start frame: %s \n  Protocol: %s\n  Loglevel: %s\n",
			papp_context->input,
			is_rtsp_source(papp_context->input) ? "true" : "false",
			papp_context->output,
			papp_context->extract_index,
			papp_context->wait_key_frame ? "true" : "false",
			papp_context->wait_start_frame ? "true" : "false",
			papp_context->tcp == true ? "tcp" : "udp",
			get_log_level(papp_context->loglevel));
		extractframe(papp_context);
	}
	else if (papp_context->video == true) {
		av_log(NULL, AV_LOG_INFO, "Launching application to extract video chunks from rtsp stream with: \n  Input: %s \n  rtsp source: %s\n  Output: %s \n  Duration: %d \n  wait for key frame: %s \n  wait for start frame: %s \n  Protocol: %s\n  Loglevel: %s\n",
			papp_context->input,
			is_rtsp_source(papp_context->input) ? "true" : "false",
			papp_context->output,
			papp_context->duration,
			papp_context->wait_key_frame ? "true" : "false",
			papp_context->wait_start_frame ? "true" : "false",
			papp_context->tcp == true ? "tcp" : "udp",
			get_log_level(papp_context->loglevel));
		extractchunk(papp_context);
	}
}

