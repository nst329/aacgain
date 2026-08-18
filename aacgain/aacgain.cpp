/*
** aacgain - modifications to mp3gain to support mp4/m4a files
** Copyright (C) David Lasker, 2004-2010 Altos Design, Inc.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**/

//Portions of this file are derived from faad2 file frontend/main.c

//Thanks to Prakash Punoor for help making it portable

#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <vector>
#include <sys/stat.h>

#ifdef WIN32
#include <sys/utime.h>
    #include <process.h>
#else
    #include <utime.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

#include "neaacdec.h"
#include "aacgain.h"
#include "aacgaini.h"
#include "mp4v2/mp4v2.h"
#include "MP4MetaFile.h"

#ifndef max
#define max(X,Y) ((X)>(Y)?(X):(Y))
#endif

#ifndef min
#define min(X,Y) ((X)<(Y)?(X):(Y))
#endif

#define SQRTHALF            0.70710678118654752440084436210485


#define NEXT_SAMPLE(dest)\
{\
    decode_t s;\
    s = *sample++;\
    dest = s * 32768.0;\
    if (s < 0) s = -s;\
    theGainData->peak = max(s, theGainData->peak);\
}

GainDataPtr theGainData;

//replay_gain tags
static char *RGTags[num_rg_tags] =
{
    "replaygain_track_gain",
    "replaygain_album_gain",
    "replaygain_track_peak",
    "replaygain_album_peak",
    "replaygain_track_minmax",
    "replaygain_album_minmax",
    "replaygain_undo"
};

typedef struct
{
    struct stat savedAttributes;
} PreserveTimestamp, *PreserveTimestampPtr;

//used to suppress bogus error messages from mp4lib
static void mp4v2_lib_message_func(MP4LogLevel loglevel, const char* fmt, va_list ap)
{
	char buf[512];
	vsnprintf(buf, sizeof(buf), fmt, ap);
	//ignore this mp4v2 error message:
	if (!strstr(buf, "no such property - moov.iods.audioProfileLevelId"))
	{
		fprintf(stderr, "mp4v2 error: %s\n", buf);
	}
}

static char* temp_file_name(const char* input_file_name)
{
#ifdef WIN32
    char* temp_file_buf = (char*)malloc(strlen(input_file_name) + 64);
    const char* lastDelim = strrchr(input_file_name, '\\');
    int dirLen = lastDelim ? (int)(lastDelim - input_file_name + 1) : 0;

    if (lastDelim)
        memcpy(temp_file_buf, input_file_name, dirLen);

    for (uint32_t i = getpid(); i < 0xFFFFFFFF; i++)
    {
        sprintf(temp_file_buf + dirLen, "aacgain-tmp-%u.mp4", i);
        struct stat status;
        if (stat(temp_file_buf, &status))
            return temp_file_buf;
    }

    fprintf(stderr, "Error: unable to create temporary file");
    free(temp_file_buf);
    exit(1);
#else
    const char* lastDelim = strrchr(input_file_name, '/');
    size_t dirLen = lastDelim ? (size_t)(lastDelim - input_file_name + 1) : 0;
    size_t capacity = strlen(input_file_name) + 64;
    char* tempFileName = (char*)malloc(capacity);

    if (lastDelim)
        snprintf(tempFileName, capacity, "%.*saacgain.XXXXXXXXXX.mp4",
                 (int)dirLen, input_file_name);
    else
        snprintf(tempFileName, capacity, "./aacgain.XXXXXXXXXX.mp4");

    if (mkstemps(tempFileName, 4) < 0)
    {
        fprintf(stderr, "Error: unable to create temporary file");
        free(tempFileName);
        exit(1);
    }
    return tempFileName;
#endif
}

static MP4FileHandle mp4_open_ro(GainDataPtr gd, char *mp4_file_name)
{
	MP4FileHandle fh = MP4Read(mp4_file_name);
	if (fh)
	{
		gd->mp4File = fh;
		if (!gd->mp4File)
		{
			return NULL;
		}
		gd->itmfList = MP4ItmfGetItems(gd->mp4File);
		if (!gd->itmfList)
		{
			MP4Close(fh, MP4_CLOSE_DO_NOT_COMPUTE_BITRATE);
			return NULL;
		}
	}
	return fh;
}

static void mp4_close(GainDataPtr gd)
{
	if (gd->mp4File)
	{
		MP4Close(gd->mp4File, MP4_CLOSE_DO_NOT_COMPUTE_BITRATE);
		MP4ItmfItemListFree((MP4ItmfItemList *)gd->itmfList);
		gd->mp4File = NULL;
		gd->itmfList = NULL;
	}
}

static int aac_analyze(void *sample_buffer, long num_samples, unsigned char channels,
                      int compute_gain)
{
    decode_t *samples = (decode_t *)sample_buffer;
    decode_t *sample = samples;
    rg_t *left_samples;
    rg_t *right_samples;
    long i;

    left_samples = new rg_t[num_samples];
    if (!left_samples)
        return 1;
    if ((channels == 2) || (channels == 6))
    {
        right_samples = new rg_t[num_samples];
    } else {
        right_samples = NULL;
    }

    switch (channels)
    {
    case 1:
        for (i=0; i<num_samples; i++)
        {
            NEXT_SAMPLE(left_samples[i])
        }
        break;
    case 2:
        for (i=0; i<num_samples; i++)
        {
            NEXT_SAMPLE(left_samples[i])
            NEXT_SAMPLE(right_samples[i])
        }
       break;
    case 6:
        for (i=0; i<num_samples; i++)
        {
            //faad2 gives samples in following order: c,l,r,bl,br,lfe
            decode_t c, l, r, bl, br, lfe;
            NEXT_SAMPLE(c);
            NEXT_SAMPLE(l);
            NEXT_SAMPLE(r);
            NEXT_SAMPLE(bl);
            NEXT_SAMPLE(br);
            NEXT_SAMPLE(lfe);
            left_samples[i] = l + c*SQRTHALF + bl*SQRTHALF + lfe;
            right_samples[i] = r + c*SQRTHALF + br*SQRTHALF + lfe;
        }
        break;
    default:
        for (i=0; i<num_samples; i++)
        {
            int j;
            decode_t sum = 0;
            for (j=0; j<channels; j++)
            {
                decode_t samp;
                NEXT_SAMPLE(samp)
                sum += samp;
            }
            left_samples[i] = (rg_t)sum / (rg_t)channels;
        }
    }

    if (compute_gain)
        AnalyzeSamples(left_samples, right_samples, num_samples, right_samples ? 2 : 1);

    delete [] left_samples;
    if (right_samples)
        delete [] right_samples;

    return 0;
}

static int parse_mp4_file(GainDataPtr gd, ProgressCallback report_progress, int compute_gain)
{
    NeAACDecHandle hDecoder = gd->hDecoder;
    unsigned char *buffer;
    unsigned int buffer_size;
    void *sample_buffer;
    NeAACDecFrameInfo frameInfo;
    MP4FileHandle mp4File = gd->mp4File;
	struct stat fileStats;
	stat(gd->mp4file_name, &fileStats);

    unsigned long sampleId, numSamples;
    int percent, old_percent = -1;

    theGainData = gd;
    gd->GainHead = gd->GainTail = NULL;
    frameInfo.error = 0;
    numSamples = MP4GetTrackNumberOfSamples(mp4File, gd->track);
	if (!numSamples)
	{
        fprintf(stderr, "Error: No samples found. Reading from MP4 file failed. \n");
        NeAACDecClose(hDecoder);
        return 1;
    }

    for (sampleId = 1; sampleId <= numSamples; sampleId++)
    {
        /* get acces unit from MP4 file */
        buffer = NULL;
        buffer_size = 0;

        //set sampleId for use by syntax.c
        gd->sampleId = sampleId;

        if (!MP4ReadSample(mp4File, gd->track, sampleId, (uint8_t**)(&buffer), (uint32_t*)(&buffer_size)))
		{
            fprintf(stderr, "Error: Reading sample from MP4 file failed. \n");
            NeAACDecClose(hDecoder);
            return 1;
        }

        sample_buffer = aacgainDecode(hDecoder, &frameInfo, buffer, buffer_size);
        if (gd->analyze && (frameInfo.error == 0) && (frameInfo.samples > 0))

        {
            aac_analyze(sample_buffer, frameInfo.samples/gd->channels, gd->channels, compute_gain);
        }

        if (buffer) free(buffer);

        percent = min((int)(sampleId*100)/numSamples, 100);
        if (report_progress && (percent > old_percent))
        {
            old_percent = percent;
            report_progress(percent, fileStats.st_size);
        }

		//ignore error 4 (scalefactor out of range) which seems to happen on some tracks
		//other errors are fatal
        if ((frameInfo.error > 0) && (frameInfo.error!=4))
        {
            fprintf(stderr, "Error: invalid file format %s, code=%d\n",
                gd->mp4file_name, frameInfo.error);
            gd->abort = 1;
            break;
        }
    }

    return frameInfo.error;
}

static MP4ItmfItem *find_metadata(GainDataPtr gd, rg_tag_e tag) {
	char* name = RGTags[tag];
	MP4ItmfItemList *list = (MP4ItmfItemList *)gd->itmfList;
	for (uint32_t i=0; i<list->size; i++)
	{
		MP4ItmfItem *item = &list->elements[i];
		if (item->name && (strcmp(item->name, name) == 0))
		{
			return item;
		}
	}
	return NULL;
}

static void set_tag(GainDataPtr gd, rg_tag_e tag, char *value)
{
	int len = strlen(value);

	MP4ItmfItem *item = find_metadata(gd, tag);
	MP4ItmfData *data = NULL;
	bool found;
	if (item)
	{
		found = TRUE;
		data = &item->dataList.elements[0];
		free(data->value);
	}
	else
	{
		found = FALSE;
		item = MP4ItmfItemAlloc("----", 1);
		item->name = strdup(RGTags[tag]);
		item->mean = strdup("com.apple.iTunes");
		data = &item->dataList.elements[0];
		data->typeCode = MP4_ITMF_BT_UTF8;
	}

	data->value = (uint8_t *)strdup(value);
	data->valueSize = len;

	if (found)
	{
		MP4ItmfSetItem(gd->mp4File, item);
	}
	else
	{
		MP4ItmfAddItem(gd->mp4File, item);
        MP4ItmfItemFree(item);
	}
}

static void set_pending_tag(GainDataPtr gd, rg_tag_e tag, const char *value)
{
    free(gd->pending_tag_values[tag]);
    gd->pending_tag_values[tag] = strdup(value);
    gd->write_requested = 1;
}

static void refresh_metadata(GainDataPtr gd)
{
    if (gd->itmfList)
        MP4ItmfItemListFree((MP4ItmfItemList *)gd->itmfList);
    gd->itmfList = MP4ItmfGetItems((MP4FileHandle)gd->mp4File);
}

// Applies deferred ReplayGain metadata to the output MP4 before it is written.
static void apply_pending_metadata(MP4MetaFile& file, void *context)
{
    GainDataPtr gd = (GainDataPtr)context;
    gd->mp4File = &file;
    gd->itmfList = NULL;
    refresh_metadata(gd);
    if (!gd->itmfList)
        return;

    if (gd->clear_rg_tags)
    {
        for (uint32_t i = 0; i < num_rg_tags; i++)
        {
            MP4ItmfItem *item = find_metadata(gd, (rg_tag_e)i);
            if (item)
                MP4ItmfRemoveItem(gd->mp4File, item);
        }
        MP4ItmfItemListFree((MP4ItmfItemList *)gd->itmfList);
        gd->itmfList = NULL;
    }

    for (uint32_t i = 0; i < num_rg_tags; i++)
    {
        if (gd->pending_tag_values[i])
        {
            refresh_metadata(gd);
            set_tag(gd, (rg_tag_e)i, gd->pending_tag_values[i]);
        }
    }

    if (gd->itmfList)
    {
        MP4ItmfItemListFree((MP4ItmfItemList *)gd->itmfList);
        gd->itmfList = NULL;
    }
}

int aac_open(char *mp4_file_name, int use_temp, int preserve_timestamp, int track_index, AACGainHandle *gh)
{
    FILE* mp4_file;
    GainDataPtr gd;
    unsigned char header[8];
    size_t file_name_len;
    PreserveTimestampPtr pt = NULL;

    *gh = NULL;

    // AAC output is always built in a sibling temporary file and replaced atomically.
    (void)use_temp;

    file_name_len = strlen(mp4_file_name);
    if ((file_name_len >= 5) && (strcmp(mp4_file_name + file_name_len - 4, ".m4p") == 0))
    {
        fprintf(stderr, "Error: DRM protected file %s is not supported.\n", mp4_file_name);
        return 1;
    }

    mp4_file = fopen(mp4_file_name, "rb");
    if (!mp4_file)
    {
        //caller's responsibility to give error message so we can use aac_open to test for aac file
        return 0;
    }

    fread(header, 1, 8, mp4_file);
    fclose(mp4_file);
    if (header[4] != 'f' || header[5] != 't' || header[6] != 'y' || header[7] != 'p')
    {
        //no error - use this to tell if a file is mp3 or mp4
        return 0;
    }

    if (preserve_timestamp)
    {
        pt = new PreserveTimestamp;
        stat(mp4_file_name, &pt->savedAttributes);
    }

    gd = new GainData;
	gd->track = MP4_INVALID_TRACK_ID;
    gd->analyze = 0;
	gd->write_requested = 0;
	gd->clear_rg_tags = 0;
	gd->gain_modified = 0;
	gd->gain_left = 0;
	gd->gain_right = 0;
	gd->gain_read = 0;
    gd->peak = 0;
    gd->hDecoder = NULL;
    gd->abort = 0;
    gd->preserve_timestamp = pt;
	gd->GainHead = NULL;
	for (uint32_t i = 0; i < num_rg_tags; i++)
		gd->pending_tag_values[i] = NULL;

	gd->mp4file_name = strdup(mp4_file_name);

    unsigned char *buffer = NULL;
    unsigned int buffer_size = 0;

	MP4SetLogCallback(&mp4v2_lib_message_func);

	MP4FileHandle fh = mp4_open_ro(gd, mp4_file_name);
	if (fh)
	{
        int num_audio_tracks = MP4GetNumberOfTracks(fh, MP4_AUDIO_TRACK_TYPE);
        if (num_audio_tracks == 0)
        {
            fprintf(stderr, "Error: File must contain at least one audio track.\n");
			gd->abort = 1;
            return 1;
        }

        /* Print a warning if there are multiple audio tracks */
        if (num_audio_tracks > 1)
        {
            fprintf(stderr, "File contains %d audio tracks, using track index %d. (Use -i option to override)\n", num_audio_tracks, track_index);
        }

		/* Find the audio track given by track_index.  Store in GainData struct. */
		gd->track = MP4FindTrackId(fh, track_index, MP4_AUDIO_TRACK_TYPE);
        if (gd->track == MP4_INVALID_TRACK_ID)
        {
            fprintf(stderr, "Error: Audio track index %d is not found in file.\n", track_index);
            gd->abort = 1;
            return 1;
        }

		//check for Apple Lossless
		if (MP4HaveTrackAtom(fh, gd->track, "mdia.minf.stbl.stsd.alac"))
		{
			fprintf(stderr, "Error: Apple Lossless files are not supported.\n");
			gd->abort = 1;
            return 1;
		}

        MP4GetTrackESConfiguration(fh, gd->track, (uint8_t**)(&buffer), (uint32_t*)(&buffer_size));
	} else {
        fprintf(stderr, "Error opening file: %s\n", gd->mp4file_name);
        gd->abort = 1;
        aac_close(gd);
        return 1;
	}

    NeAACDecHandle hDecoder;
    NeAACDecConfigurationPtr config;
    mp4AudioSpecificConfig mp4ASC;

    hDecoder = gd->hDecoder = NeAACDecOpen();

    /* Set configuration */
    config = NeAACDecGetCurrentConfiguration(hDecoder);
    config->outputFormat = FAAD_FMT_DOUBLE;
    config->downMatrix = 0;
    NeAACDecSetConfiguration(hDecoder, config);

    if (NeAACDecInit2(hDecoder, buffer, buffer_size,
                    &gd->samplerate, &gd->channels) < 0)
    {
        /* If some error initializing occurred, skip the file */
        if (buffer)
            free(buffer);
        fprintf(stderr, "Error: file format not recognized.\n");
        gd->abort = 1;
        aac_close(gd);
        return 1;
    }

    if (NeAACDecAudioSpecificConfig(buffer, buffer_size, &mp4ASC) >= 0)
    {
        if (mp4ASC.sbr_present_flag == 1)
        {
            free(buffer);
            fprintf(stderr, "Error: HE_AAC/SBR files are not supported.\n");
            gd->abort = 1;
            aac_close(gd);
            return 1;
        }
    }
    free(buffer);

    *gh = gd;
    return 0;
}

unsigned int aac_get_sample_rate(AACGainHandle gh)
{
    GainDataPtr gd = (GainDataPtr)gh;

    return gd->samplerate;
}

int aac_compute_gain(AACGainHandle gh, rg_t *peak, unsigned char *min_gain,
                     unsigned char *max_gain, ProgressCallback reportProgress)
{
    int rc = 0;
    GainDataPtr gd = (GainDataPtr)gh;

    if (!gd->gain_read || !gd->analyze)
    {
	    gd->analyze = 1;
        gd->peak = 0;
        gd->min_gain = 255;
        gd->max_gain = 0;
        rc = parse_mp4_file(gd, reportProgress, 1);
        gd->gain_read = 1;
    }
    if (peak)
    {
        *peak = gd->peak * 32768.0;
    }
    if (min_gain)
        *min_gain = gd->min_gain;
    if (max_gain)
        *max_gain = gd->max_gain;

    return rc;
}

int aac_compute_peak(AACGainHandle gh, rg_t *peak, unsigned char *min_gain,
                     unsigned char *max_gain, ProgressCallback reportProgress)
{
    int rc = 0;
    GainDataPtr gd = (GainDataPtr)gh;

    if (!gd->gain_read || !gd->analyze)
    {
		gd->analyze = 1;
        gd->peak = 0;
        gd->min_gain = 255;
        gd->max_gain = 0;
        rc = parse_mp4_file(gd, reportProgress, 0);
        gd->gain_read = 1;
    }
    if (peak)
    {
        *peak = gd->peak * 32768.0;
    }
    if (min_gain)
        *min_gain = gd->min_gain;
    if (max_gain)
        *max_gain = gd->max_gain;

    return rc;
}

int aac_modify_gain(AACGainHandle gh, int left, int right,
                    ProgressCallback reportProgress)
{
	GainDataPtr gd = (GainDataPtr)gh;
    GainFixupPtr gf;
    int rc = 0;

	if ((gd->channels != 2) && (left != right))
    {
        fprintf(stderr, "Error: individual channel adjustments are only supported on\n"
            "2-channel (stereo) files.\n");
        gd->abort = 1;
        return -1;
    }

    if (!gd->gain_read)
    {
        gd->analyze = 0;
        rc = parse_mp4_file(gd, reportProgress, 0);
        gd->gain_read = 1;
        if (rc)
        {
            gd->abort = 1;
            return rc;
        }
    }

    //test for wrap before modifying file
    gf = gd->GainHead;
    while (gf)
    {
        if (((gf->channel == 0) &&
            (((gf->orig_gain + left) < 0) || ((gf->orig_gain + left) > 255))) ||
            ((gf->channel == 1) &&
            (((gf->orig_gain + right) < 0) || ((gf->orig_gain + right) > 255))))
        {
            fprintf(stderr, "Error: Wrap while modifying gain.\n");
            gd->abort = 1;
            return -1;
        }
        gf = gf->next;
    }

	gd->gain_left = left;
	gd->gain_right = right;
	gd->gain_modified = 1;
	gd->write_requested = 1;

    return rc;
}

int aac_set_tag_float(AACGainHandle gh, rg_tag_e tag, rg_t value)
{
    GainDataPtr gd = (GainDataPtr)gh;

	//format the value
    char vstr[32];
    sprintf(vstr, "%-.2f", value);

	set_pending_tag(gd, tag, vstr);
    return 0;
}

int aac_get_tag_float(AACGainHandle gh, rg_tag_e tag, rg_t *value)
{
    GainDataPtr gd = (GainDataPtr)gh;
	if (gd->pending_tag_values[tag])
	{
		sscanf(gd->pending_tag_values[tag], "%lf", value);
		return 0;
	}
	if (gd->clear_rg_tags)
		return 1;

    MP4ItmfItem *item = find_metadata(gd, tag);
    if (item)
    {
		MP4ItmfData *data = &item->dataList.elements[0];

		//null terminate the value
		char buf[128];
		memset(buf, 0, sizeof(buf));
		memcpy(buf, data->value, min(data->valueSize, sizeof(buf)-1));

		sscanf(buf, "%lf", value);
        return 0;
    }

    return 1;
}

int aac_set_tag_int_2(AACGainHandle gh, rg_tag_e tag, int p1, int p2)
{
    GainDataPtr gd = (GainDataPtr)gh;

    char vstr[128];
    sprintf(vstr, "%d,%d", p1, p2);

	set_pending_tag(gd, tag, vstr);

    return 0;
}

int aac_get_tag_int_2(AACGainHandle gh, rg_tag_e tag, int *p1, int *p2)
{
    GainDataPtr gd = (GainDataPtr)gh;
	if (gd->pending_tag_values[tag])
	{
		sscanf(gd->pending_tag_values[tag], "%d,%d", p1, p2);
		return 0;
	}
	if (gd->clear_rg_tags)
		return 1;

    MP4ItmfItem *item = find_metadata(gd, tag);
	if (item)
    {
		MP4ItmfData *data = &item->dataList.elements[0];

		//null terminate the value
		char buf[128];
		memset(buf, 0, sizeof(buf));
		memcpy(buf, data->value, min(data->valueSize, sizeof(buf)-1));

		sscanf(buf, "%d,%d", p1, p2);
        return 0;
    }

    return 1;
}

int aac_clear_rg_tags(AACGainHandle gh)
{
    GainDataPtr gd = (GainDataPtr)gh;
	gd->clear_rg_tags = 1;
	gd->write_requested = 1;

    return 0;
}

static int fastcopy(const char *from, const char *to);

// Finalizes deferred gain and tag changes, then atomically replaces the input.
int aac_close(AACGainHandle gh)
{
    GainDataPtr gd = (GainDataPtr)gh;
    int rc = 0;
    PreserveTimestampPtr pt = (PreserveTimestampPtr)gd->preserve_timestamp;
    char *temp_name = NULL;

    //close the faad decoder if open
    if (gd->hDecoder)
    {
        NeAACDecClose(gd->hDecoder);
    }

    if (gd->mp4File)
        mp4_close(gd);

    if (gd->write_requested && !gd->abort)
    {
        temp_name = temp_file_name(gd->mp4file_name);

        std::vector<MP4MetaFilePatch> patches;
        if (gd->gain_modified)
        {
            for (GainFixupPtr gf = gd->GainHead; gf; gf = gf->next)
            {
                MP4MetaFilePatch patch;
                patch.sampleId = gf->sampleId;
                patch.byteOffset = gf->sample_offset;
                patch.bitOffset = gf->bit_offset;
                patch.value = (uint8_t)(gf->orig_gain +
                    ((gf->channel == 0) ? gd->gain_left : gd->gain_right));
                patches.push_back(patch);
            }
        }

        MP4MetaFile optimizedFile;
        rc = optimizedFile.OptimizeWithPatches(
            gd->mp4file_name,
            temp_name,
            gd->track,
            patches.empty() ? NULL : &patches[0],
            (uint32_t)patches.size(),
            apply_pending_metadata,
            gd) ? 0 : 1;

        if (gd->itmfList)
        {
            MP4ItmfItemListFree((MP4ItmfItemList *)gd->itmfList);
            gd->itmfList = NULL;
        }
        gd->mp4File = NULL;

        if (rc == 0)
        {
            rc = rename(temp_name, gd->mp4file_name);
            if (rc)
                rc = fastcopy(temp_name, gd->mp4file_name);
        }

        if (rc)
        {
            fprintf(stderr, "Error: attempt to create file %s failed. The original file was kept.\n",
                    gd->mp4file_name);
            remove(temp_name);
        }
    }

    if (pt)
    {
        if (!gd->abort && rc == 0)
        {
			struct utimbuf setTime;

			setTime.actime = pt->savedAttributes.st_atime;
			setTime.modtime = pt->savedAttributes.st_mtime;
			utime(gd->mp4file_name, &setTime);
        }
        delete pt;
    }

    while (gd->GainHead)
    {
        GainFixupPtr next = gd->GainHead->next;
        free(gd->GainHead);
        gd->GainHead = next;
    }

    for (uint32_t i = 0; i < num_rg_tags; i++)
        free(gd->pending_tag_values[i]);

    free(temp_name);

    free(gd->mp4file_name);
    delete gd;

    return rc;
}



static int fastcopy(const char *from, const char *to)
{
        struct timespec ts[2];
	static u_int blen = (1024 * 1024);
        static char *bp = NULL;
        mode_t oldmode;
        int nread, from_fd, to_fd;
        struct stat tsb;
        struct stat sb;
	struct stat *sbp ;

	printf( "%s->%s\n",from,to);

        if (lstat(from, &sb)) {
//                warn("%s", from);
                return (1);
        }
	sbp = &sb;

        if ((from_fd = open(from, O_RDONLY, 0)) < 0) {
//                warn("fastcopy: open() failed (from): %s", from);
                return (1);
        }
        if (bp == NULL && (bp = (char *)malloc((size_t)blen)) == NULL) {
//                warnx("malloc(%u) failed", blen);
                (void)close(from_fd);
                return (1);
        }
        while ((to_fd =
            open(to, O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, 0)) < 0) {
                if (errno == EEXIST && unlink(to) == 0)
                        continue;
//                warn("fastcopy: open() failed (to): %s", to);
                (void)close(from_fd);
                return (1);
        }
        while ((nread = read(from_fd, bp, (size_t)blen)) > 0)
                if (write(to_fd, bp, (size_t)nread) != nread) {
//                        warn("fastcopy: write() failed: %s", to);
                        goto err;
                }
        if (nread < 0) {
//                warn("fastcopy: read() failed: %s", from);
	  err:
	    if (unlink(to))
	    {
//                        warn("%s: remove", to);
	    }

                (void)close(from_fd);
                (void)close(to_fd);
                return (1);
        }

        oldmode = sbp->st_mode & ALLPERMS;
        if (fchown(to_fd, sbp->st_uid, sbp->st_gid)) {
//                warn("%s: set owner/group (was: %lu/%lu)", to,
//                    (u_long)sbp->st_uid, (u_long)sbp->st_gid);
                if (oldmode & (S_ISUID | S_ISGID)) {
//                        warnx(
//"%s: owner/group changed; clearing suid/sgid (mode was 0%03o)",
//                            to, oldmode);
                        sbp->st_mode &= ~(S_ISUID | S_ISGID);
                }
        }
        if (fchmod(to_fd, sbp->st_mode))
	{
//	    warn("%s: set mode (was: 0%03o)", to, oldmode);
	}

        /*
         * POSIX 1003.2c states that if _POSIX_ACL_EXTENDED is in effect
         * for dest_file, then its ACLs shall reflect the ACLs of the
         * source_file.
         */
//        preserve_fd_acls(from_fd, to_fd, from, to);
        (void)close(from_fd);

        if (close(to_fd)) {
//                warn("%s", to);
                return (1);
        }

        if (unlink(from)) {
//                warn("%s: remove", from);
                return (1);
        }
//        if (vflg)
//                printf("%s -> %s\n", from, to);
        return (0);
}
