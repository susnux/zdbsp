/*
    The main glue for ZDBSP.
    Copyright (C) 2002,2003 Randy Heit

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

// HEADER FILES ------------------------------------------------------------

#ifdef _WIN32

// Need windows.h for QueryPerformanceCounter
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define HAVE_TIMING 1
#define START_COUNTER(s,e,f) \
	LARGE_INTEGER s, e, f; QueryPerformanceCounter (&s);
#define END_COUNTER(s,e,f,l) \
	QueryPerformanceCounter (&e); QueryPerformanceFrequency (&f); \
	if (!NoTiming) printf (l, double(e.QuadPart - s.QuadPart) / double(f.QuadPart));

#else

#define HAVE_TIMING 0
#define START_COUNTER(s,e,f)
#define END_COUNTER(s,e,f)

// Need these to check if input/output are the same file
#include <sys/types.h>
#include <sys/stat.h>

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#include "zdbsp.h"
#include "wad.h"
#include "processor.h"
#include "getopt.h"

// MACROS ------------------------------------------------------------------

#ifndef M_PI
#define M_PI            3.14159265358979323846
#endif

// TYPES -------------------------------------------------------------------

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

static void ParseArgs (int argc, char **argv);
static void ShowUsage ();
static void ShowVersion ();
static bool CheckInOutNames ();

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern "C" int optind;
extern "C" char *optarg;

// PUBLIC DATA DEFINITIONS -------------------------------------------------

const char		*Map = NULL;
const char		*InName;
const char		*OutName = "tmp.wad";
bool			 BuildNodes = true;
bool			 BuildGLNodes = false;
bool			 ConformNodes = false;
bool			 NoPrune = false;
EBlockmapMode	 BlockmapMode = EBM_Rebuild;
ERejectMode		 RejectMode = ERM_DontTouch;
int				 MaxSegs = 64;
int				 SplitCost = 8;
int				 AAPreference = 16;
bool			 CheckPolyobjs = true;
bool			 ShowMap = false;
bool			 ShowWarnings = false;
bool			 NoTiming = false;
bool			 CompressNodes = false;
bool			 CompressGLNodes = false;
bool			 GLOnly = false;

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static option long_opts[] =
{
	{"help",			no_argument,		0,	1000},
	{"version",			no_argument,		0,	'V'},
	{"view",			no_argument,		0,	'v'},
	{"warn",			no_argument,		0,	'w'},
	{"map",				required_argument,	0,	'm'},
	{"output",			required_argument,	0,	'o'},
	{"output-file",		required_argument,	0,	'o'},
	{"file",			required_argument,	0,	'f'},
	{"no-nodes",		no_argument,		0,	'N'},
	{"gl",				no_argument,		0,	'g'},
	{"gl-matching",		no_argument,		0,	'G'},
	{"empty-blockmap",	no_argument,		0,	'b'},
	{"empty-reject",	no_argument,		0,	'r'},
	{"zero-reject",		no_argument,		0,	'R'},
	{"full-reject",		no_argument,		0,	'e'},
	{"no-reject",		no_argument,		0,	'E'},
	{"partition",		required_argument,	0,	'p'},
	{"split-cost",		required_argument,	0,	's'},
	{"diagonal-cost",	required_argument,	0,	'd'},
	{"no-polyobjs",		no_argument,		0,	'P'},
	{"no-prune",		no_argument,		0,	'q'},
	{"no-timing",		no_argument,		0,	't'},
	{"compress",		no_argument,		0,	'z'},
	{"compress-normal",	no_argument,		0,	'Z'},
	{"gl-only",			no_argument,		0,	'x'},
	{0,0,0,0}
};

static const char short_opts[] = "wVgGvbNrReEm:o:f:p:s:d:PqtzZx";

// CODE --------------------------------------------------------------------

int main (int argc, char **argv)
{
	bool fixSame = false;

	ParseArgs (argc, argv);

	if (InName == NULL)
	{
		if (optind >= argc || optind < argc-1)
		{ // Source file is unspecified or followed by junk
			ShowUsage ();
			return 0;
		}

		InName = argv[optind];
	}

	try
	{
		START_COUNTER(t1a, t1b, t1c)

		if (CheckInOutNames ())
		{
			// When the input and output files are the same, output will go to
			// a temporary file. After everything is done, the input file is
			// deleted and the output file is renamed to match the input file.

			char *out = new char[strlen(OutName)+3], *dot;

			if (out == NULL)
			{
				throw exception("Could not create temporary file name.");
			}

			strcpy (out, OutName);
			dot = strrchr (out, '.');
			if (dot && (dot[1] == 'w' || dot[1] == 'W')
					&& (dot[2] == 'a' || dot[2] == 'A')
					&& (dot[3] == 'd' || dot[3] == 'D')
					&& dot[4] == 0)
			{
				// *.wad becomes *.daw
				dot[1] = 'd';
				dot[3] = 'w';
			}
			else
			{
				// * becomes *.x
				strcat (out, ".x");
			}
			OutName = out;
			fixSame = true;
		}

		{
			FWadReader inwad (InName);
			FWadWriter outwad (OutName, inwad.IsIWAD());

			int lump = 0;
			int max = inwad.NumLumps ();

			while (lump < max)
			{
				if (inwad.IsMap (lump) &&
					(!Map || stricmp (inwad.LumpName (lump), Map) == 0))
				{
					START_COUNTER(t2a, t2b, t2c)
					FProcessor builder (inwad, lump);
					builder.Write (outwad);
					END_COUNTER(t2a, t2b, t2c, "   %g seconds.\n")

					lump = inwad.LumpAfterMap (lump);
				}
				else if (inwad.IsGLNodes (lump))
				{
					// Ignore GL nodes from the input for any maps we process.
					if (BuildNodes && (Map == NULL || stricmp (inwad.LumpName (lump)+3, Map) == 0))
					{
						lump = inwad.SkipGLNodes (lump);
					}
					else
					{
						outwad.CopyLump (inwad, lump);
						++lump;
					}
				}
				else
				{
					//printf ("copy %s\n", inwad.LumpName (lump));
					outwad.CopyLump (inwad, lump);
					++lump;
				}
			}

			outwad.Close ();
		}

		if (fixSame)
		{
			remove (InName);
			if (0 != rename (OutName, InName))
			{
				printf ("The output file could not be renamed to %s.\nYou can find it as %s.\n",
					InName, OutName);
			}
		}

		END_COUNTER(t1a, t1b, t1c, "\nTotal time: %g seconds.\n")
	}
	catch (exception msg)
	{
		printf ("%s\n", msg.what());
		return 20;
	}

	return 0;
}

//==========================================================================
//
// ParseArgs
//
//==========================================================================

static void ParseArgs (int argc, char **argv)
{
	int ch;

	while ((ch = getopt_long (argc, argv, short_opts, long_opts, NULL)) != EOF)
	{
		switch (ch)
		{
		case 0:
			break;

		case 'v':
			ShowMap = true;
			break;
		case 'w':
			ShowWarnings = true;
			break;
		case 'm':
			Map = optarg;
			break;
		case 'o':
			OutName = optarg;
			break;
		case 'f':
			InName = optarg;
			break;
		case 'N':
			BuildNodes = false;
			break;
		case 'b':
			BlockmapMode = EBM_Create0;
			break;
		case 'r':
			RejectMode = ERM_Create0;
			break;
		case 'R':
			RejectMode = ERM_CreateZeroes;
			break;
		case 'e':
			RejectMode = ERM_Rebuild;
			break;
		case 'E':
			RejectMode = ERM_DontTouch;
			break;
		case 'p':
			MaxSegs = atoi (optarg);
			if (MaxSegs < 3)
			{ // Don't be too unreasonable
				MaxSegs = 3;
			}
			break;
		case 's':
			SplitCost = atoi (optarg);
			if (SplitCost < 1)
			{ // 1 means to add no extra weight at all
				SplitCost = 1;
			}
			break;
		case 'd':
			AAPreference = atoi (optarg);
			if (AAPreference < 1)
			{
				AAPreference = 1;
			}
			break;
		case 'P':
			CheckPolyobjs = false;
			break;
		case 'g':
			BuildGLNodes = true;
			ConformNodes = false;
			break;
		case 'G':
			BuildGLNodes = true;
			ConformNodes = true;
			break;
		case 'z':
			CompressNodes = true;
			CompressGLNodes = true;
			break;
		case 'Z':
			CompressNodes = true;
			CompressGLNodes = false;
			break;
		case 'x':
			GLOnly = true;
			BuildGLNodes = true;
			ConformNodes = false;
			break;
		case 'q':
			NoPrune = true;
			break;
		case 't':
			NoTiming = true;
			break;
		case 'V':
			ShowVersion ();
			exit (0);
			break;
		case 1000:
			ShowUsage ();
			exit (0);
		default:
			printf ("Try `zdbsp --help' for more information.\n");
			exit (0);
		}
	}
}

//==========================================================================
//
// ShowUsage
//
//==========================================================================

static void ShowUsage ()
{
	printf (
"Usage: zdbsp [options] sourcefile.wad\n"
"  -m, --map=MAP            Only affect the specified map\n"
"  -o, --output=FILE        Write output to FILE instead of tmp.wad\n"
"  -q, --no-prune           Keep unused sidedefs and sectors\n"
"  -N, --no-nodes           Do not rebuild nodes\n"
"  -g, --gl                 Build GL-friendly nodes\n"
"  -G, --gl-matching        Build GL-friendly nodes that match normal nodes\n"
"  -x, --gl-only            Only build GL-friendly nodes\n"
"  -b, --empty-blockmap     Create an empty blockmap\n"
"  -r, --empty-reject       Create an empty reject table\n"
"  -R, --zero-reject        Create a reject table of all zeroes\n"
//"  -e, --full-reject        Rebuild reject table (unsupported)\n"
"  -E, --no-reject          Leave reject table untouched\n"
"  -p, --partition=NNN      Maximum number of segs to consider at each node\n"// (default 64)\n"
"  -s, --split-cost=NNN     Adjusts the cost for splitting segs\n"// (default 8)\n"
"  -d, --diagonal-cost=NNN  Adjusts the cost for avoiding diagonal splitters\n"// (default 16)\n"
"  -P, --no-polyobjs        Do not check for polyobject subsector splits\n"
"  -z, --compress           Compress the nodes (including GL nodes, if created)\n"
"  -Z, --compress-normal    Compress normal nodes but not GL nodes\n"
#ifdef _WIN32
"  -v, --view               View the nodes\n"
#endif
"  -w, --warn               Show warning messages\n"
#if HAVE_TIMING
"  -t, --no-timing          Suppress timing information\n"
#endif
"  -V, --version            Display version information\n"
"      --help               Display this usage information\n"
	);
}

//==========================================================================
//
// ShowVersion
//
//==========================================================================

static void ShowVersion ()
{
	printf ("ZDBSP " ZDBSP_VERSION "\n");
}

//==========================================================================
//
// CheckInOutNames
//
// Returns true if InName and OutName refer to the same file. This needs
// to be implemented different under Windows than Unix because the inode
// information returned by stat is always 0, so it cannot be used to
// determine duplicate files.
//
//==========================================================================

static bool CheckInOutNames ()
{
#ifndef _WIN32
	struct stat info;
	dev_t outdev;
	ino_t outinode;

	if (0 != stat (OutName, &info))
	{ // If out doesn't exist, it can't be duplicated
		return false;
	}
	outdev = info.st_dev;
	outinode = info.st_ino;
	if (0 != stat (InName, &info))
	{
		return false;
	}
	return outinode == info.st_ino && outdev == info.st_dev;
#else
	HANDLE inFile, outFile;

	outFile = CreateFile (OutName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL);
	if (outFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	inFile = CreateFile (InName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL);
	if (inFile == INVALID_HANDLE_VALUE)
	{
		CloseHandle (outFile);
		return false;
	}

	BY_HANDLE_FILE_INFORMATION inInfo, outInfo;
	bool same = false;

	if (GetFileInformationByHandle (inFile, &inInfo) &&
		GetFileInformationByHandle (outFile, &outInfo))
	{
		same = inInfo.dwVolumeSerialNumber == outInfo.dwVolumeSerialNumber &&
			inInfo.nFileIndexLow == outInfo.nFileIndexLow &&
			inInfo.nFileIndexHigh == outInfo.nFileIndexHigh;
	}

	CloseHandle (inFile);
	CloseHandle (outFile);

	return same;
#endif
}

//==========================================================================
//
// PointToAngle
//
//==========================================================================

angle_t PointToAngle (fixed_t x, fixed_t y)
{
	const double rad2bam = double(1<<30) / M_PI;
	double ang = atan2 (double(y), double(x));
	if (ang < 0.0)
	{
		ang = 2*M_PI+ang;
	}
	return angle_t(ang * rad2bam) << 1;
}

//==========================================================================
//
// Warn
//
//==========================================================================

void Warn (const char *format, ...)
{
	va_list marker;

	if (!ShowWarnings)
	{
		return;
	}

	va_start (marker, format);
	vprintf (format, marker);
	va_end (marker);
}