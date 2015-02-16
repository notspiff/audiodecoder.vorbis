/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "kodi/libXBMC_addon.h"

extern "C" {
#include <vorbis/vorbisfile.h>
#include "kodi/kodi_audiodec_dll.h"
#include "kodi/AEChannelData.h"
#include <inttypes.h>

ADDON::CHelper_libXBMC_addon *XBMC           = NULL;

//-- Create -------------------------------------------------------------------
// Called on load. Addon should fully initalize or return error status
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!XBMC)
    XBMC = new ADDON::CHelper_libXBMC_addon;

  if (!XBMC->RegisterMe(hdl))
  {
    delete XBMC, XBMC=NULL;
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  return ADDON_STATUS_OK;
}

//-- Stop ---------------------------------------------------------------------
// This dll must cease all runtime activities
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Stop()
{
}

//-- Destroy ------------------------------------------------------------------
// Do everything before unload of this add-on
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Destroy()
{
  XBMC=NULL;
}

//-- HasSettings --------------------------------------------------------------
// Returns true if this add-on use settings
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
bool ADDON_HasSettings()
{
  return false;
}

//-- GetStatus ---------------------------------------------------------------
// Returns the current Status of this visualisation
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_GetStatus()
{
  return ADDON_STATUS_OK;
}

//-- GetSettings --------------------------------------------------------------
// Return the settings for XBMC to display
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
  return 0;
}

//-- FreeSettings --------------------------------------------------------------
// Free the settings struct passed from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------

void ADDON_FreeSettings()
{
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from XBMC)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_SetSetting(const char *strSetting, const void* value)
{
  return ADDON_STATUS_OK;
}

//-- Announce -----------------------------------------------------------------
// Receive announcements from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Announce(const char *flag, const char *sender, const char *message, const void *data)
{
}

size_t ReadCallback(void *ptr, size_t size, size_t nmemb, void *datasource)
{
  return XBMC->ReadFile(datasource, ptr, size*nmemb);
}

int SeekCallback(void *datasource, ogg_int64_t offset, int whence)
{
  return XBMC->SeekFile(datasource, offset, whence);
}

int CloseCallback(void *datasource)
{
  XBMC->CloseFile(datasource);
  return 1;
}

long TellCallback(void *datasource)
{
  return XBMC->GetFilePosition(datasource);
}

ov_callbacks GetCallbacks(const char* strFile)
{
  ov_callbacks oggIOCallbacks;
  oggIOCallbacks.read_func=ReadCallback;
  oggIOCallbacks.seek_func=SeekCallback;
  oggIOCallbacks.tell_func=TellCallback;
  oggIOCallbacks.close_func=CloseCallback;

  return oggIOCallbacks;
}

struct VorbisContext
{
  void* file;
  ov_callbacks callbacks;
  OggVorbis_File vorbisfile;
  int track;
  double timeoffset;
};

void* Init(const char* strFile, unsigned int filecache, int* channels,
           int* samplerate, int* bitspersample, int64_t* totaltime,
           int* bitrate, AEDataFormat* format, const AEChannel** channelinfo)
{
  VorbisContext* result = new VorbisContext;
  result->track=0;
  std::string toLoad(strFile);
  if (toLoad.find(".oggstream") != std::string::npos)
  {
    size_t iStart=toLoad.rfind('-') + 1;
    result->track = atoi(toLoad.substr(iStart, toLoad.size()-iStart-10).c_str());
    //  The directory we are in, is the file
    //  that contains the bitstream to play,
    //  so extract it
    size_t slash = toLoad.rfind('\\');
    if (slash == std::string::npos)
      slash = toLoad.rfind('/');
    toLoad = toLoad.substr(0, slash);
  }

  result->file = XBMC->OpenFile(toLoad.c_str(), 0);
  if (!result->file)
  {
    delete result;
    return NULL;
  }
  result->callbacks = GetCallbacks(strFile);

  if (ov_open_callbacks(result->file, &result->vorbisfile,
                         NULL, 0, result->callbacks) != 0)
  {
    delete result;
    return NULL;
  }

  long iStreams=ov_streams(&result->vorbisfile);
  if (iStreams>1)
  {
    if (result->track > iStreams)
    {
      DeInit(result);
      return NULL;
    }
  }

  //  Calculate the offset in secs where the bitstream starts
  result->timeoffset = 0;
  for (int i=0; i<result->track; ++i)
    result->timeoffset += ov_time_total(&result->vorbisfile, i);

  vorbis_info* pInfo=ov_info(&result->vorbisfile, result->track);
  if (!pInfo)
  {
    XBMC->Log(ADDON::LOG_ERROR, "OGGCodec: Can't get stream info from %s", toLoad.c_str());
    DeInit(result);
    return NULL;
  }

  *channels      = pInfo->channels;
  *samplerate    = pInfo->rate;
  *bitspersample = 16;
  *totaltime     = (int64_t)ov_time_total(&result->vorbisfile, result->track)*1000;
  *format        = AE_FMT_S16NE;
  static enum AEChannel map[8][9] = {
    {AE_CH_FC, AE_CH_NULL},
    {AE_CH_FL, AE_CH_FR, AE_CH_NULL},
    {AE_CH_FL, AE_CH_FC, AE_CH_FR, AE_CH_NULL},
    {AE_CH_FL, AE_CH_FR, AE_CH_BL, AE_CH_BR, AE_CH_NULL},
    {AE_CH_FL, AE_CH_FC, AE_CH_FR, AE_CH_BL, AE_CH_BR, AE_CH_NULL},
    {AE_CH_FL, AE_CH_FC, AE_CH_FR, AE_CH_BL, AE_CH_BR, AE_CH_LFE, AE_CH_NULL},
    {AE_CH_FL, AE_CH_FC, AE_CH_FR, AE_CH_SL, AE_CH_SR, AE_CH_BL, AE_CH_BR, AE_CH_NULL},
    {AE_CH_FL, AE_CH_FC, AE_CH_FR, AE_CH_SL, AE_CH_SR, AE_CH_BL, AE_CH_BR, AE_CH_LFE, AE_CH_NULL}
  };

  *channelinfo = NULL;
  if (*channels <= 8)
    *channelinfo = map[*channels - 1];
  *bitrate = pInfo->bitrate_nominal;

  if (*samplerate == 0 || *channels == 0 || *bitspersample == 0 || *totaltime == 0)
  {
    XBMC->Log(ADDON::LOG_ERROR, "OGGCodec: Can't get stream info, SampleRate=%i, Channels=%i, BitsPerSample=%i, TotalTime=%" PRIu64, *samplerate, *channels, *bitspersample, *totaltime);
    delete result;
    return NULL;
  }

  if (result->timeoffset > 0.0)
  {
    if (ov_time_seek(&result->vorbisfile, result->timeoffset)!=0)
    {
      XBMC->Log(ADDON::LOG_ERROR, "OGGCodec: Can't seek to the bitstream start time (%s)", toLoad.c_str());
      DeInit(result);
      return NULL;
    }
  }

  return result;
}

int ReadPCM(void* context, uint8_t* pBuffer, int size, int *actualsize)
{
  VorbisContext* ctx = (VorbisContext*)context;

  *actualsize=0;
  int iBitStream=-1;

  //  the maximum chunk size the vorbis decoder seem to return with one call is 4096
  long lRead=ov_read(&ctx->vorbisfile, (char*)pBuffer, 
                     size, 0, 2, 1, &iBitStream);

  if (lRead == OV_HOLE)
    return 0;

  //  Our logical bitstream changed, we reached the eof
  if (lRead > 0 && ctx->track!=iBitStream)
    lRead=0;

  if (lRead<0)
  {
    XBMC->Log(ADDON::LOG_ERROR, "OGGCodec: Read error %lu", lRead);
    return 1;
  }
  else if (lRead==0)
    return -1;
  else
    *actualsize=lRead;

  return 0;
}

int64_t Seek(void* context, int64_t time)
{
  VorbisContext* ctx = (VorbisContext*)context;

  if (ov_time_seek(&ctx->vorbisfile, ctx->timeoffset+(double)(time/1000.0f))!=0)
    return 0;

  return time;
}

bool DeInit(void* context)
{
  VorbisContext* ctx = (VorbisContext*)context;
  if (ctx->vorbisfile.datasource)
    ov_clear(&ctx->vorbisfile);
  delete ctx;
  return true;
}

bool ReadTag(const char* strFile, char* title, char* artist,
             int* length)
{
  return true;
}

int TrackCount(const char* strFile)
{
  VorbisContext* ctx = new VorbisContext;
  ctx->file = XBMC->OpenFile(strFile, 4);
  if (!ctx->file)
  {
    delete ctx;
    return 1;
  }
  ctx->callbacks = GetCallbacks(strFile);

  if (ov_open_callbacks(ctx->file, &ctx->vorbisfile,
                        NULL, 0, ctx->callbacks) != 0)
  {
    delete ctx;
    return 1;
  }

  long iStreams=ov_streams(&ctx->vorbisfile);

  ov_clear(&ctx->vorbisfile);
  delete ctx;

  return iStreams;
}
}
