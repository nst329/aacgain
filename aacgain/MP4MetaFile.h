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

//MP4MetaFile extends mp4v2 class MP4File to allow
//modification of any 8 bits of a sample

#ifndef __MP4_META_FILE_H__
#define __MP4_META_FILE_H__

#include "src/impl.h" //in mp4v2

class MP4MetaFile;

// The callback updates metadata after the source has been parsed and before
// the optimized output is written. The context is owned by the caller.
typedef void (*MP4MetaFileMetadataCallback)(MP4MetaFile& file, void* context);

// A patch replaces the AAC global_gain bits in one MP4 sample.
// Example: {sampleId, 3, 0, newGain} changes one byte at sample offset 3.
struct MP4MetaFilePatch
{
    MP4SampleId sampleId;
    uint32_t byteOffset;
    uint8_t bitOffset;
    uint8_t value;
};

class MP4MetaFile : public mp4v2::impl::MP4File
{
public:
    // Rewrites the source once, applying sample patches while each chunk is
    // in memory. The source remains read-only until the caller replaces it.
    bool OptimizeWithPatches(const char* srcFileName, const char* dstFileName,
                             MP4TrackId trackId,
                             const MP4MetaFilePatch* patches,
                             uint32_t patchCount,
                             MP4MetaFileMetadataCallback metadataCallback,
                             void* metadataContext);
};

#endif //__MP4_META_FILE_H__
