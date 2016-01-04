/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2015 Live Networks, Inc.  All rights reserved.
// A file source that is a plain byte stream (rather than frames)
// Implementation

#include "ByteStreamFileSource.hh"
#include "InputFile.hh"
#include "GroupsockHelper.hh"

////////// ByteStreamFileSource //////////

ByteStreamFileSource*
ByteStreamFileSource::createNew(UsageEnvironment& env, char const* fileName,
				unsigned preferredFrameSize,
				unsigned playTimePerFrame) {
  FILE* fid = OpenInputFile(env, fileName);
  if (fid == NULL) return NULL;

  ByteStreamFileSource* newSource
    = new ByteStreamFileSource(env, fid, preferredFrameSize, playTimePerFrame);
  newSource->fFileSize = GetFileSize(fileName, fid);

  return newSource;
}

ByteStreamFileSource*
ByteStreamFileSource::createNew(UsageEnvironment& env, FILE* fid,
				unsigned preferredFrameSize,
				unsigned playTimePerFrame) {
  if (fid == NULL) return NULL;

  ByteStreamFileSource* newSource = new ByteStreamFileSource(env, fid, preferredFrameSize, playTimePerFrame);
  newSource->fFileSize = GetFileSize(NULL, fid);

  return newSource;
}

void ByteStreamFileSource::seekToByteAbsolute(u_int64_t byteNumber, u_int64_t numBytesToStream) {
  SeekFile64(fFid, (int64_t)byteNumber, SEEK_SET);

  fNumBytesToStream = numBytesToStream;
  fLimitNumBytesToStream = fNumBytesToStream > 0;
}

void ByteStreamFileSource::seekToByteRelative(int64_t offset, u_int64_t numBytesToStream) {
  SeekFile64(fFid, offset, SEEK_CUR);

  fNumBytesToStream = numBytesToStream;
  fLimitNumBytesToStream = fNumBytesToStream > 0;
}

void ByteStreamFileSource::seekToEnd() {
  SeekFile64(fFid, 0, SEEK_END);
}

ByteStreamFileSource::ByteStreamFileSource(UsageEnvironment& env, FILE* fid,
					   unsigned preferredFrameSize,
					   unsigned playTimePerFrame)
  : FramedFileSource(env, fid), fFileSize(0), fPreferredFrameSize(preferredFrameSize),
    fPlayTimePerFrame(playTimePerFrame), fLastPlayTime(0),
    fHaveStartedReading(False), fLimitNumBytesToStream(False), fNumBytesToStream(0) {
#ifndef READ_FROM_FILES_SYNCHRONOUSLY
  makeSocketNonBlocking(fileno(fFid));
#endif

  // Test whether the file is seekable
  fFidIsSeekable = FileIsSeekable(fFid);
}

ByteStreamFileSource::~ByteStreamFileSource() {
  if (fFid == NULL) return;

#ifndef READ_FROM_FILES_SYNCHRONOUSLY
  envir().taskScheduler().turnOffBackgroundReadHandling(fileno(fFid));
#endif

  CloseInputFile(fFid);
}

void ByteStreamFileSource::doGetNextFrame() {
  if (feof(fFid) || ferror(fFid) || (fLimitNumBytesToStream && fNumBytesToStream == 0)) {
    handleClosure();
    return;
  }

DEBUG_MARK
  // 读取文件内容
#ifdef READ_FROM_FILES_SYNCHRONOUSLY
  doReadFromFile(); // 这里是windows环境的
#else
  if (!fHaveStartedReading) {
    // Await readable data from the file:
    envir().taskScheduler().turnOnBackgroundReadHandling(fileno(fFid),
	       (TaskScheduler::BackgroundHandlerProc*)&fileReadableHandler, this);
    fHaveStartedReading = True;
  }
#endif
}

void ByteStreamFileSource::doStopGettingFrames() {
  envir().taskScheduler().unscheduleDelayedTask(nextTask());
#ifndef READ_FROM_FILES_SYNCHRONOUSLY
  envir().taskScheduler().turnOffBackgroundReadHandling(fileno(fFid));
  fHaveStartedReading = False;
#endif
}

void ByteStreamFileSource::fileReadableHandler(ByteStreamFileSource* source, int /*mask*/) {
  if (!source->isCurrentlyAwaitingData()) {
    source->doStopGettingFrames(); // we're not ready for the data yet
    return;
  }
  source->doReadFromFile();
}

///////////////////////////////////////////////////////////////////
typedef struct _TIME_STRUCT {
    uint16_t ts_year;     /* year */
    uint16_t ts_mon;      /* month */
    uint16_t ts_wday;     /* day of week, since Sunday, range from 0 to 6 */
    uint16_t ts_day;      /* day of month, range from 1 to 31 */
    uint16_t ts_hour;     /* hour */
    uint16_t ts_min;      /* minute */
    uint16_t ts_sec;      /* second */
    uint16_t ts_millsec;  /* million second */
	int16_t  ts_isdst;   /* aylight saving time */
} TIME_STRUCT;

#include <time.h>

static void convert_ms2time(struct timeval tv, TIME_STRUCT* ts)
{
    time_t second = 0;
    struct tm* timeinfo = NULL;

    if (ts == NULL)
        return;

    ts->ts_millsec = tv.tv_usec / 1000;
    second = tv.tv_sec;

    timeinfo = localtime(&second);
    ts->ts_year = timeinfo->tm_year + 1900;
    ts->ts_mon  = timeinfo->tm_mon + 1;
    ts->ts_wday = timeinfo->tm_wday;
    ts->ts_day  = timeinfo->tm_mday;
    ts->ts_hour = timeinfo->tm_hour;
    ts->ts_min  = timeinfo->tm_min;
    ts->ts_sec  = timeinfo->tm_sec;
}
///////////////////////////////////////////////////////////////////

// file source的，在此函数读取文件
// 一次读fMaxSize，不一定一次只读一帧
void ByteStreamFileSource::doReadFromFile() {
  // Try to read as many bytes as will fit in the buffer provided (or "fPreferredFrameSize" if less)
  if (fLimitNumBytesToStream && fNumBytesToStream < (u_int64_t)fMaxSize) {
    fMaxSize = (unsigned)fNumBytesToStream;
  }
  if (fPreferredFrameSize > 0 && fPreferredFrameSize < fMaxSize) {
    fMaxSize = fPreferredFrameSize;
  }

  LL_DEBUG(7, "read maxsize: %d (total: %d)\n", fMaxSize, fFileSize);

#ifdef READ_FROM_FILES_SYNCHRONOUSLY
  fFrameSize = fread(fTo, 1, fMaxSize, fFid);
#else
  if (fFidIsSeekable) {
    fFrameSize = fread(fTo, 1, fMaxSize, fFid);
  } else {
    // For non-seekable files (e.g., pipes), call "read()" rather than "fread()", to ensure that the read doesn't block:
    fFrameSize = read(fileno(fFid), fTo, fMaxSize);
  }
#endif
  if (fFrameSize == 0) {
    handleClosure();
    return;
  }
  fNumBytesToStream -= fFrameSize;

  // Set the 'presentation time':
  if (fPlayTimePerFrame > 0 && fPreferredFrameSize > 0) {
    if (fPresentationTime.tv_sec == 0 && fPresentationTime.tv_usec == 0) {
      // This is the first frame, so use the current time:
      gettimeofday(&fPresentationTime, NULL);
    } else {
      // Increment by the play time of the previous data:
      unsigned uSeconds	= fPresentationTime.tv_usec + fLastPlayTime;
      fPresentationTime.tv_sec += uSeconds/1000000;
      fPresentationTime.tv_usec = uSeconds%1000000;
    }

    // Remember the play time of this data:
    fLastPlayTime = (fPlayTimePerFrame*fFrameSize)/fPreferredFrameSize;
    fDurationInMicroseconds = fLastPlayTime;
  } else {
    // We don't know a specific play time duration for this data,
    // so just record the current time as being the 'presentation time':
    gettimeofday(&fPresentationTime, NULL);
  }
  TIME_STRUCT tm_now, tm_next;
  convert_ms2time(fPresentationTime, &tm_next);
  printf("+++source: %04d-%02d-%02d %02d:%02d:%02d:%03d\n", tm_next.ts_year, tm_next.ts_mon, tm_next.ts_day,
          tm_next.ts_hour, tm_next.ts_min, tm_next.ts_sec, tm_next.ts_millsec);
  // Inform the reader that he has data:
#ifdef READ_FROM_FILES_SYNCHRONOUSLY
  // To avoid possible infinite recursion, we need to return to the event loop to do this:
  nextTask() = envir().taskScheduler().scheduleDelayedTask(0,
				(TaskFunc*)FramedSource::afterGetting, this);
#else
  // Because the file read was done from the event loop, we can call the
  // 'after getting' function directly, without risk of infinite recursion:
  FramedSource::afterGetting(this);
#endif
}



