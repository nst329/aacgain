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

#include "MP4MetaFile.h"

#include <algorithm>
#include <vector>

using namespace mp4v2;
using namespace mp4v2::impl;

namespace
{
struct AbsolutePatch
{
    uint64_t fileOffset;
    uint8_t bitOffset;
    uint8_t value;
};

struct AbsolutePatchLess
{
    bool operator()(const AbsolutePatch& left, const AbsolutePatch& right) const
    {
        return left.fileOffset < right.fileOffset;
    }
};
}

bool MP4MetaFile::OptimizeWithPatches(const char* srcFileName, const char* dstFileName,
                                      MP4TrackId trackId,
                                      const MP4MetaFilePatch* patches,
                                      uint32_t patchCount,
                                      MP4MetaFileMetadataCallback metadataCallback,
                                      void* metadataContext)
{
    File* src = NULL;
    File* dst = NULL;

    try
    {
        Open(srcFileName, File::MODE_READ, NULL);
        ReadFromFile();
        CacheProperties();

        MP4Track* track = GetTrack(trackId);
        std::vector<AbsolutePatch> absolutePatches;
        absolutePatches.reserve(patchCount);

        for (uint32_t i = 0; i < patchCount; i++)
        {
            uint32_t sampleSize = track->GetSampleSize(patches[i].sampleId);
            if (patches[i].byteOffset >= sampleSize ||
                (patches[i].bitOffset && patches[i].byteOffset + 1 >= sampleSize))
            {
                throw new Exception("sample patch is outside sample bounds",
                                    __FILE__, __LINE__, __FUNCTION__);
            }

            AbsolutePatch patch;
            patch.fileOffset = track->GetSampleFileOffset(patches[i].sampleId) +
                               patches[i].byteOffset;
            patch.bitOffset = patches[i].bitOffset;
            patch.value = patches[i].value;
            absolutePatches.push_back(patch);
        }

        std::sort(absolutePatches.begin(), absolutePatches.end(), AbsolutePatchLess());

        src = m_file;
        m_file = NULL;
        Open(dstFileName, File::MODE_CREATE, NULL);
        dst = m_file;

        SetIntegerProperty("moov.mvhd.modificationTime", MP4GetAbsTimestamp());
        if (metadataCallback)
            metadataCallback(*this, metadataContext);

        ((MP4RootAtom*)m_pRootAtom)->BeginOptimalWrite();

        uint32_t numTracks = m_pTracks.Size();
        std::vector<MP4ChunkId> chunkIds(numTracks, 1);
        std::vector<MP4ChunkId> maxChunkIds(numTracks);
        std::vector<MP4Timestamp> nextChunkTimes(numTracks, MP4_INVALID_TIMESTAMP);

        for (uint32_t i = 0; i < numTracks; i++)
            maxChunkIds[i] = m_pTracks[i]->GetNumberOfChunks();

        for (;;)
        {
            uint32_t nextTrackIndex = (uint32_t)-1;
            MP4Timestamp nextTime = MP4_INVALID_TIMESTAMP;

            for (uint32_t i = 0; i < numTracks; i++)
            {
                if (chunkIds[i] > maxChunkIds[i])
                    continue;

                if (nextChunkTimes[i] == MP4_INVALID_TIMESTAMP)
                {
                    MP4Timestamp chunkTime = m_pTracks[i]->GetChunkTime(chunkIds[i]);
                    nextChunkTimes[i] = MP4ConvertTime(chunkTime,
                                                       m_pTracks[i]->GetTimeScale(),
                                                       GetTimeScale());
                }

                if (nextChunkTimes[i] > nextTime)
                    continue;
                if (nextChunkTimes[i] == nextTime &&
                    strcmp(m_pTracks[i]->GetType(), MP4_HINT_TRACK_TYPE))
                    continue;

                nextTime = nextChunkTimes[i];
                nextTrackIndex = i;
            }

            if (nextTrackIndex == (uint32_t)-1)
                break;

            MP4Track* currentTrack = m_pTracks[nextTrackIndex];
            MP4ChunkId chunkId = chunkIds[nextTrackIndex];
            uint8_t* chunk = NULL;
            uint32_t chunkSize = 0;
            uint64_t chunkOffset = currentTrack->GetChunkOffset(chunkId);

            m_file = src;
            currentTrack->ReadChunk(chunkId, &chunk, &chunkSize);

            if (currentTrack->GetId() == trackId && !absolutePatches.empty())
            {
                AbsolutePatch key = {chunkOffset, 0, 0};
                std::vector<AbsolutePatch>::const_iterator it =
                    std::lower_bound(absolutePatches.begin(), absolutePatches.end(), key,
                                     AbsolutePatchLess());
                uint64_t chunkEnd = chunkOffset + chunkSize;
                for (; it != absolutePatches.end() && it->fileOffset < chunkEnd; ++it)
                {
                    uint64_t offset = it->fileOffset - chunkOffset;
                    if (offset >= chunkSize ||
                        (it->bitOffset && offset + 1 >= chunkSize))
                    {
                        MP4Free(chunk);
                        throw new Exception("sample patch is outside chunk bounds",
                                            __FILE__, __LINE__, __FUNCTION__);
                    }

                    if (it->bitOffset)
                    {
                        uint8_t* bytes = &chunk[offset];
                        bytes[0] &= (uint8_t)(0xff << it->bitOffset);
                        bytes[0] |= (uint8_t)(it->value >> (8 - it->bitOffset));
                        bytes[1] &= (uint8_t)(0xff >> (8 - it->bitOffset));
                        bytes[1] |= (uint8_t)(it->value << it->bitOffset);
                    }
                    else
                    {
                        chunk[offset] = it->value;
                    }
                }
            }

            m_file = dst;
            currentTrack->RewriteChunk(chunkId, chunk, chunkSize);
            MP4Free(chunk);

            chunkIds[nextTrackIndex]++;
            nextChunkTimes[nextTrackIndex] = MP4_INVALID_TIMESTAMP;
        }

        ((MP4RootAtom*)m_pRootAtom)->FinishOptimalWrite();
        delete dst;
        delete src;
        m_file = NULL;
        return true;
    }
    catch (Exception* x)
    {
        mp4v2::impl::log.errorf(*x);
        delete x;
    }
    catch (...)
    {
        mp4v2::impl::log.errorf("%s: failed", __FUNCTION__);
    }

    if (m_file != src && m_file != dst)
        delete m_file;
    m_file = NULL;
    delete dst;
    delete src;
    return false;
}
