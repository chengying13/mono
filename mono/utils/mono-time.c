/**
 * \file
 * Time utility functions.
 * Author: Paolo Molaro (<lupus@ximian.com>)
 * Copyright (C) 2008 Novell, Inc.
 * Licensed under the MIT license. See LICENSE file in the project root for full license information.
 */

#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include <mono/utils/mono-time.h>
#include <mono/utils/atomic.h>

#if HAVE_MACH_ABSOLUTE_TIME
#include <mach/mach_time.h>
static mach_timebase_info_data_t s_TimebaseInfo;
#endif

#define MTICKS_PER_SEC (10 * 1000 * 1000)

typedef enum _TimeConversionConstants
{
	tccSecondsToMillieSeconds       = 1000,         // 10^3
	tccSecondsToMicroSeconds        = 1000000,      // 10^6
	tccSecondsToNanoSeconds         = 1000000000,   // 10^9
	tccMillieSecondsToMicroSeconds  = 1000,         // 10^3
	tccMillieSecondsToNanoSeconds   = 1000000,      // 10^6
	tccMicroSecondsToNanoSeconds    = 1000,         // 10^3
	tccSecondsTo100NanoSeconds      = 10000000,     // 10^7
	tccMicroSecondsTo100NanoSeconds = 10            // 10^1
} TimeConversionConstants;

gint64
mono_msec_ticks (void)
{
	return mono_100ns_ticks () / 10 / 1000;
}

#ifdef HOST_WIN32
#include <windows.h>

#ifndef _MSC_VER
/* we get "error: implicit declaration of function 'GetTickCount64'" */
WINBASEAPI ULONGLONG WINAPI GetTickCount64(void);
#endif

gint64
mono_msec_boottime (void)
{
	/* GetTickCount () is reportedly monotonic */
	return GetTickCount64 ();
}

/* Returns the number of 100ns ticks from unspecified time: this should be monotonic */
gint64
mono_100ns_ticks (void)
{
	static LARGE_INTEGER freq;
	static UINT64 start_time;
	UINT64 cur_time;
	LARGE_INTEGER value;

	if (!freq.QuadPart) {
		if (!QueryPerformanceFrequency (&freq))
			return mono_100ns_datetime ();
		QueryPerformanceCounter (&value);
		start_time = value.QuadPart;
	}
	QueryPerformanceCounter (&value);
	cur_time = value.QuadPart;
	/* we use unsigned numbers and return the difference to avoid overflows */
	return (cur_time - start_time) * (double)MTICKS_PER_SEC / freq.QuadPart;
}

/* Returns the number of 100ns ticks since Jan 1, 1601, UTC timezone */
gint64
mono_100ns_datetime (void)
{
	ULARGE_INTEGER ft;

	if (sizeof(ft) != sizeof(FILETIME))
		g_assert_not_reached ();

	GetSystemTimeAsFileTime ((FILETIME*) &ft);
	return ft.QuadPart;
}

#else


#if defined (HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif
#if defined(HAVE_SYS_SYSCTL_H)
#include <sys/sysctl.h>
#endif

#if defined(HOST_DARWIN)
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

#include <time.h>

static gint64
get_boot_time (void)
{
#if defined (HAVE_SYS_PARAM_H) && defined (KERN_BOOTTIME)
	int mib [2];
	size_t size;
	time_t now;
	struct timeval boottime;

	(void)time(&now);

	mib [0] = CTL_KERN;
	mib [1] = KERN_BOOTTIME;

	size = sizeof(boottime);

	if (sysctl(mib, 2, &boottime, &size, NULL, 0) != -1)
		return (gint64)((now - boottime.tv_sec) * MTICKS_PER_SEC);
#else
	FILE *uptime = fopen ("/proc/uptime", "r");
	if (uptime) {
		double upt;
		if (fscanf (uptime, "%lf", &upt) == 1) {
			gint64 now = mono_100ns_datetime ();
			fclose (uptime);
			return now - (gint64)(upt * MTICKS_PER_SEC);
		}
		fclose (uptime);
	}
#endif
	/* a made up uptime of 300 seconds */
	return (gint64)300 * MTICKS_PER_SEC;
}

/* Returns the number of milliseconds from boot time: this should be monotonic */
/* Adapted from CoreCLR: https://github.com/dotnet/coreclr/blob/66d2738ea96fcce753dec1370e79a0c78f7b6adb/src/pal/src/misc/time.cpp */
gint64
mono_msec_boottime (void)
{
	gint64 retval = 0;

	/* clock_gettime () is found by configure but its only present from ios 10 */
#if (defined(HAVE_CLOCK_MONOTONIC_COARSE) || defined(HAVE_CLOCK_MONOTONIC)) && !defined(TARGET_IOS)
	clockid_t clockType =
#if HAVE_CLOCK_MONOTONIC_COARSE
	CLOCK_MONOTONIC_COARSE; /* good enough resolution, fastest speed */
#else
	CLOCK_MONOTONIC;
#endif
	struct timespec ts;
	if (clock_gettime (clockType, &ts) != 0) {
		g_error ("clock_gettime(CLOCK_MONOTONIC*) failed; errno is %d", errno, strerror (errno));
		goto exit;
	}
	retval = (ts.tv_sec * tccSecondsToMillieSeconds) + (ts.tv_nsec / tccMillieSecondsToNanoSeconds);

#elif HAVE_MACH_ABSOLUTE_TIME
	static gboolean timebase_inited;

	if (!timebase_inited) {
		kern_return_t machRet;
		mach_timebase_info_data_t tmp;
		machRet = mach_timebase_info (&tmp);
		g_assert (machRet == KERN_SUCCESS);
		/* Assume memcpy works correctly if ran concurrently */
		memcpy (&s_TimebaseInfo, &tmp, sizeof (mach_timebase_info_data_t));
		mono_memory_barrier ();
		timebase_inited = TRUE;
	}
	retval = (mach_absolute_time () * s_TimebaseInfo.numer / s_TimebaseInfo.denom) / tccMillieSecondsToNanoSeconds;

#elif HAVE_GETHRTIME
	retval = (gint64)(gethrtime () / tccMillieSecondsToNanoSeconds);

#elif HAVE_READ_REAL_TIME
	timebasestruct_t tb;
	read_real_time (&tb, TIMEBASE_SZ);
	if (time_base_to_time (&tb, TIMEBASE_SZ) != 0) {
		g_error ("time_base_to_time() failed; errno is %d (%s)", errno, strerror (errno));
		goto exit;
	}
	retval = (tb.tb_high * tccSecondsToMillieSeconds) + (tb.tb_low / tccMillieSecondsToNanoSeconds);

#else
	struct timeval tv;
	if (gettimeofday (&tv, NULL) == -1) {
		g_error ("gettimeofday() failed; errno is %d (%s)", errno, strerror (errno));
		goto exit;
	}
    retval = (tv.tv_sec * tccSecondsToMillieSeconds) + (tv.tv_usec / tccMillieSecondsToMicroSeconds);

#endif /* HAVE_CLOCK_MONOTONIC */
exit:
	return retval;
}

/* Returns the number of 100ns ticks from unspecified time: this should be monotonic */
gint64
mono_100ns_ticks (void)
{
	struct timeval tv;
#if defined(HOST_DARWIN)
	/* http://developer.apple.com/library/mac/#qa/qa1398/_index.html */
	static mach_timebase_info_data_t timebase;
	guint64 now = mach_absolute_time ();
	if (timebase.denom == 0) {
		mach_timebase_info (&timebase);
		timebase.denom *= 100; /* we return 100ns ticks */
	}
	return now * timebase.numer / timebase.denom;
#elif defined(CLOCK_MONOTONIC) && !defined(_AIX)
	/* !_AIX is defined because i 7.1 doesn't have clock_getres */
	struct timespec tspec;
	static struct timespec tspec_freq = {0};
	static int can_use_clock = 0;
	if (!tspec_freq.tv_nsec) {
		can_use_clock = clock_getres (CLOCK_MONOTONIC, &tspec_freq) == 0;
		/*printf ("resolution: %lu.%lu\n", tspec_freq.tv_sec, tspec_freq.tv_nsec);*/
	}
	if (can_use_clock) {
		if (clock_gettime (CLOCK_MONOTONIC, &tspec) == 0) {
			/*printf ("time: %lu.%lu\n", tspec.tv_sec, tspec.tv_nsec); */
			return ((gint64)tspec.tv_sec * MTICKS_PER_SEC + tspec.tv_nsec / 100);
		}
	}
#endif
	if (gettimeofday (&tv, NULL) == 0)
		return ((gint64)tv.tv_sec * 1000000 + tv.tv_usec) * 10;
	return 0;
}

/*
 * Magic number to convert unix epoch start to windows epoch start
 * Jan 1, 1970 into a value which is relative to Jan 1, 1601.
 */
#define EPOCH_ADJUST    ((guint64)11644473600LL)

/* Returns the number of 100ns ticks since 1/1/1601, UTC timezone */
gint64
mono_100ns_datetime (void)
{
	struct timeval tv;
	if (gettimeofday (&tv, NULL) == 0)
		return mono_100ns_datetime_from_timeval (tv);
	return 0;
}

gint64
mono_100ns_datetime_from_timeval (struct timeval tv)
{
	return (((gint64)tv.tv_sec + EPOCH_ADJUST) * 1000000 + tv.tv_usec) * 10;
}

#endif

