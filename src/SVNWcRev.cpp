// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2005 - Stefan Kueng
// Modified by Oliver Pajonk
// Copied functionality from SubWCRev (21338 rev.) by Igor Azarny (iazarny@yahoo.com) 2011-May-11

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#include "version.h"

#include <iostream>

#include <apr_pools.h>

#include <svn_error.h>
#include <svn_client.h>
#include <svn_path.h>
#include <svn_utf.h>
#include <svn_wc.h>

// MOD: Replaced Windows specific includes with some UNIX specific includes
// TODO: Try to use APR to achieve complete platform independence
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <svn_dso.h>
#include "SVNWcRev.h"






// Define the help text as a multi-line macro
// Every line except the last must be terminated with a backslash
#define HelpText1 "\
Usage: svnwcrev WorkingCopyPath [SrcVersionFile DstVersionFile] [-nmdf]\n\
\n\
Params:\n\
WorkingCopyPath    :   path to a Subversion working copy.\n\
SrcVersionFile     :   path to a template file containing keywords.\n\
DstVersionFile     :   path to save the resulting parsed file.\n\
-n                 :   if given, then svnwcrev will error if the working\n\
                       copy contains local modifications.\n\
-m                 :   if given, then svnwcrev will error if the working\n\
                       copy contains mixed revisions.\n\
-d                 :   if given, then svnwcrev will only do its job if\n\
                       DstVersionFile does not exist.\n"
#define HelpText2 "\
-f                 :   if given, then svnwcrev will include the\n\
                       last-committed revision of folders. Default is\n\
                       to use only files to get the revision numbers.\n\
                       This only affects $WCREV$ and $WCDATE$.\n\
-e                 :   if given, also include dirs which are included\n\
                       with svn:externals, but only if they're from the\n\
                       same repository.\n"
                       
#define HelpText3 "\
-x                 :   if given, then svnwcrev will write the revisions\n\
                       numbers in HEX instead of decimal\n\
-X                 :   if given, then svnwcrev will write the revisions\n\
                       numbers in HEX with '0x' before them\n"

#define HelpText4 "\
Switches must be given in a single argument, e.g. '-nm' not '-n -m'.\n\
\n\
svnwcrev reads the Subversion status of all files in a working copy\n\
excluding externals. If SrcVersionFile is specified, it is scanned\n\
for special placeholders of the form \"$WCxxx$\".\n\
SrcVersionFile is then copied to DstVersionFile but the placeholders\n\
are replaced with information about the working copy as follows:\n\
\n\
$WCREV$         Highest committed revision number\n\
$WCDATE$        Date of highest committed revision\n\
$WCDATE=$       Like $WCDATE$ with an added strftime format after the =\n\
$WCRANGE$       Update revision range\n\
$WCURL$         Repository URL of the working copy\n\
$WCNOW$         Current system date & time\n\
$WCNOW=$        Like $WCNOW$ with an added strftime format after the =\n\
$WCLOCKDATE$    Lock date for this item\n\
$WCLOCKDATE=$   Like $WCLOCKDATE$ with an added strftime format after the =\n\
$WCLOCKOWNER$   Lock owner for this item\n\
$WCLOCKCOMMENT$ Lock comment for this item\n\
\n"

#define HelpText5 "\
The strftime format strings for $WCxxx=$ must not be longer than 1024\n\
characters, and must not produce output greater than 1024 characters.\n\
\n\
Placeholders of the form \"$WCxxx?TrueText:FalseText$\" are replaced with\n\
TrueText if the tested condition is true, and FalseText if false.\n\
\n\
$WCMODS$        True if local modifications found\n\
$WCMIXED$       True if mixed update revisions found\n\
$WCINSVN$       True if the item is versioned\n\
$WCNEEDSLOCK$   True if the svn:needs-lock property is set\n\
$WCISLOCKED$    True if the item is locked\n"
// End of multi-line help text.


#define VERDEF           "$WCREV$"
#define DATEDEF          "$WCDATE$"
#define DATEDEFUTC       "$WCDATEUTC$"
#define DATEWFMTDEF      "$WCDATE="
#define DATEWFMTDEFUTC   "$WCDATEUTC="
#define MODDEF           "$WCMODS?"
#define RANGEDEF         "$WCRANGE$"
#define MIXEDDEF         "$WCMIXED?"
#define URLDEF           "$WCURL$"
#define NOWDEF           "$WCNOW$"
#define NOWDEFUTC        "$WCNOWUTC$"
#define NOWWFMTDEF       "$WCNOW="
#define NOWWFMTDEFUTC    "$WCNOWUTC="
#define ISINSVN          "$WCINSVN?"
#define NEEDSLOCK        "$WCNEEDSLOCK?"
#define ISLOCKED         "$WCISLOCKED?"
#define LOCKDATE         "$WCLOCKDATE$"
#define LOCKDATEUTC      "$WCLOCKDATEUTC$"
#define LOCKWFMTDEF      "$WCLOCKDATE="
#define LOCKWFMTDEFUTC   "$WCLOCKDATEUTC="
#define LOCKOWNER        "$WCLOCKOWNER$"
#define LOCKCOMMENT      "$WCLOCKCOMMENT$"

// Internal error codes
#define ERR_SYNTAX		1	// Syntax error
#define ERR_FNF			2	// File/folder not found
#define ERR_OPEN		3	// File open error
#define ERR_ALLOC		4	// Memory allocation error
#define ERR_READ		5	// File read/write/size error
#define ERR_SVN_ERR		6	// SVN error

// Documented error codes
#define ERR_SVN_MODS	7	// Local mods found (-n)
#define ERR_SVN_MIXED	8	// Mixed rev WC found (-m)
#define ERR_OUT_EXISTS	9	// Output file already exists (-d)
#define ERR_NOWC       10   // the path is not a working copy or part of one

// Value for apr_time_t to signify "now"
#define USE_TIME_NOW    -2  // 0 and -1 might already be significant.



int FindPlaceholder(char *def, char *pBuf, size_t & index, size_t filelength)
{
	size_t deflen = strlen(def);
	while (index + deflen <= filelength)
	{
		if (memcmp(pBuf + index, def, deflen) == 0)
			return TRUE;
		index++;
	}
	return FALSE;
}

int InsertRevision(char * def, char * pBuf, size_t & index,
					size_t & filelength, size_t maxlength,
					long MinRev, long MaxRev, SubWCRev_t * SubStat)
{
	// Search for first occurrence of def in the buffer, starting at index.
	if (!FindPlaceholder(def, pBuf, index, filelength))
	{
		// No more matches found.
		return FALSE;
	}
	// Format the text to insert at the placeholder
	char destbuf[40];
	if (MinRev == -1 || MinRev == MaxRev)
	{
	    if ((SubStat)&&(SubStat->bHexPlain))
	      sprintf(destbuf, "%LX", (apr_int64_t)MaxRev);
	    else if ((SubStat)&&(SubStat->bHexX))
	      sprintf(destbuf, "%#LX", (apr_int64_t)MaxRev);
	    else 
	      sprintf(destbuf, "%Ld", (apr_int64_t)MaxRev);
	}
	else
	{
	  if ((SubStat)&&(SubStat->bHexPlain))
	    sprintf(destbuf, "%LX:%LX", (apr_int64_t)MinRev, (apr_int64_t)MaxRev);
	  else if ((SubStat)&&(SubStat->bHexX))
            sprintf(destbuf, "%#LX:%#LX", (apr_int64_t)MinRev, (apr_int64_t)MaxRev);
	  else
	    sprintf(destbuf, "%Ld:%Ld", (apr_int64_t)MinRev, (apr_int64_t)MaxRev);
	}	
	// Replace the $WCxxx$ string with the actual revision number
	char * pBuild = pBuf + index;
    ptrdiff_t Expansion = strlen(destbuf) - strlen(def);
	if (Expansion < 0)
	{
		memmove(pBuild, pBuild - Expansion, filelength - ((pBuild - Expansion) - pBuf));
	}
	else if (Expansion > 0)
	{
		// Check for buffer overflow
		if (maxlength < Expansion + filelength) return FALSE;
		memmove(pBuild + Expansion, pBuild, filelength - (pBuild - pBuf));
	}
	memmove(pBuild, destbuf, strlen(destbuf));
	filelength += Expansion;
	return TRUE;
}

int InsertDate(char * def, char * pBuf, size_t & index,
				size_t & filelength, size_t maxlength,
				apr_time_t date_svn)
{ 
	// Search for first occurrence of def in the buffer, starting at index.
	if (!FindPlaceholder(def, pBuf, index, filelength))
	{
		// No more matches found.
		return FALSE;
	}

 	apr_time_exp_t newtime;
	apr_status_t status = apr_time_exp_lt(&newtime, date_svn);
	if(status)
		return false;
	
	// Format the date/time in international format as yyyy/mm/dd hh:mm:ss
	char destbuf[32];
	sprintf(destbuf, "%04d/%02d/%02d %02d:%02d:%02d",
			newtime.tm_year + 1900,
			newtime.tm_mon + 1,
			newtime.tm_mday,
			newtime.tm_hour,
			newtime.tm_min,
			newtime.tm_sec);
	// Replace the $WCDATE$ string with the actual commit date
	char * pBuild = pBuf + index;
	ptrdiff_t Expansion = strlen(destbuf) - strlen(def);
	if (Expansion < 0)
	{
		memmove(pBuild, pBuild - Expansion, filelength - ((pBuild - Expansion) - pBuf));
	}
	else if (Expansion > 0)
	{
		// Check for buffer overflow
		if (maxlength < Expansion + filelength) return FALSE;
		memmove(pBuild + Expansion, pBuild, filelength - (pBuild - pBuf));
	}
	memmove(pBuild, destbuf, strlen(destbuf));
	filelength += Expansion;
	return TRUE;
}

int InsertUrl(char * def, char * pBuf, size_t & index,
					size_t & filelength, size_t maxlength,
					char * pUrl)
{
	// Search for first occurrence of def in the buffer, starting at index.
	if (!FindPlaceholder(def, pBuf, index, filelength))
	{
		// No more matches found.
		return FALSE;
	}
	// Replace the $WCURL$ string with the actual URL
	char * pBuild = pBuf + index;
	ptrdiff_t Expansion = strlen(pUrl) - strlen(def);
	if (Expansion < 0)
	{
		memmove(pBuild, pBuild - Expansion, filelength - ((pBuild - Expansion) - pBuf));
	}
	else if (Expansion > 0)
	{
		// Check for buffer overflow
		if (maxlength < Expansion + filelength) return FALSE;
		memmove(pBuild + Expansion, pBuild, filelength - (pBuild - pBuf));
	}
	memmove(pBuild, pUrl, strlen(pUrl));
	filelength += Expansion;
	return TRUE;
}

int InsertBoolean(char * def, char * pBuf, size_t & index, size_t & filelength, bool isTrue)
{ 
	// Search for first occurrence of def in the buffer, starting at index.
	if (!FindPlaceholder(def, pBuf, index, filelength))
	{
		// No more matches found.
		return FALSE;
	}
	// Look for the terminating '$' character
	char * pBuild = pBuf + index;
	char * pEnd = pBuild + 1;
	while (*pEnd != '$')
	{
		pEnd++;
		if (pEnd - pBuf >= (apr_int64_t)filelength)
			return FALSE;	// No terminator - malformed so give up.
	}
	
	// Look for the ':' dividing TrueText from FalseText
	char *pSplit = pBuild + 1;
	// This loop is guaranteed to terminate due to test above.
	while (*pSplit != ':' && *pSplit != '$')
		pSplit++;

	if (*pSplit == '$')
		return FALSE;		// No split - malformed so give up.

	if (isTrue)
	{
		// Replace $WCxxx?TrueText:FalseText$ with TrueText
		// Remove :FalseText$
		memmove(pSplit, pEnd + 1, filelength - (pEnd + 1 - pBuf));
		filelength -= (pEnd + 1 - pSplit);
		// Remove $WCxxx?
		size_t deflen = strlen(def);
		memmove(pBuild, pBuild + deflen, filelength - (pBuild + deflen - pBuf));
		filelength -= deflen;
	}
	else
	{
		// Replace $WCxxx?TrueText:FalseText$ with FalseText
		// Remove terminating $
		memmove(pEnd, pEnd + 1, filelength - (pEnd + 1 - pBuf));
		filelength--;
		// Remove $WCxxx?TrueText:
		memmove(pBuild, pSplit + 1, filelength - (pSplit + 1 - pBuf));
		filelength -= (pSplit + 1 - pBuild);
	} // if (isTrue)
	return TRUE;
}

int abort_on_pool_failure (int /*retcode*/)
{
	abort ();
	return -1;
}



int main(int argc, char** argv){
	// we have three parameters
	const char* src = NULL;
	const char* dst = NULL;
	const char* wc = NULL;
	bool bErrOnMods = FALSE;
	bool bErrOnMixed = FALSE;
	
	SubWCRev_t SubStat;
	memset (&SubStat, 0, sizeof (SubStat));
	SubStat.bFolders = FALSE;
	

	if (argc >= 2 && argc <= 5)
	{
		// WC path is always first argument.
		wc = argv[1];
	}
	if (argc == 4 || argc == 5)
	{
		// SubWCRev Path Tmpl.in Tmpl.out [-params]
		src = argv[2];
		dst = argv[3];
		if (access(src, R_OK) != 0)
		{
			printf("File '%s' does not exist\n", src);
			return ERR_FNF;		// file does not exist
		}
	}
	if (argc == 3 || argc == 5)
	{
		// SubWCRev Path -params
		// SubWCRev Path Tmpl.in Tmpl.out -params
		const char* Params = argv[argc-1];
		if (Params[0] == '-')
		{
			if (strchr(Params, 'n') != 0)
				bErrOnMods = TRUE;
			if (strchr(Params, 'm') != 0)
				bErrOnMixed = TRUE;
			if (strchr(Params, 'd') != 0)
			{
				if ((dst != NULL) && access(dst, W_OK) != 0)
				{
					printf("File '%s' already exists\n", dst);
					return ERR_OUT_EXISTS;
				}
			}
			// the 'f' option is useful to keep the revision which is inserted in
			// the file constant, even if there are commits on other branches.
			// For example, if you tag your working copy, then half a year later
			// do a fresh checkout of that tag, the folder in your working copy of
			// that tag will get the HEAD revision of the time you check out (or
			// do an update). The files alone however won't have their last-committed
			// revision changed at all.
			if (strchr(Params, 'f') != 0)
				SubStat.bFolders = true;
			if (strchr(Params, 'e') != 0)
				SubStat.bExternals = true;
			if (strchr(Params, 'x') != 0)
				SubStat.bHexPlain = true;
			if (strchr(Params, 'X') != 0)
			        SubStat.bHexX = true;
			
		}
		else
		{
			// Bad params - abort and display help.
			wc = NULL;
		}
	}
	if (wc == NULL)
	{
		printf("SVNWCRev %s \n\n", SVNWCREV_VERSION);
		puts(HelpText1);
		puts(HelpText2);
 		puts(HelpText3);
 		puts(HelpText4);
 		puts(HelpText5);
		return ERR_SYNTAX;
	}

	if (access(wc, R_OK) != 0)
	{
		printf("Directory or file '%s' does not exist\n", wc);
		return ERR_FNF;			// dir does not exist
	}
	char * pBuf = NULL;
	size_t readlength = 0;
	size_t filelength = 0;
	size_t maxlength  = 0;
	int hFile = -1;
	struct stat inputStatus;
	if (dst != NULL)
	{
		// open the file and read the contents
		hFile = open(src, O_RDONLY);
		if (hFile == -1)
		{
			printf("Unable to open input file '%s'\n", src);
			return ERR_OPEN;		// error opening file
		}
		
		
		if(fstat(hFile, &inputStatus) != 0){
		}
		
		
		filelength = inputStatus.st_size;
		
		if (filelength == 0)
		{
			printf("Could not determine filesize of '%s'\n", src);
			return ERR_READ;
		}
		maxlength = filelength+4096;	// We might be increasing filesize.
		pBuf = new char[maxlength];
		if (pBuf == NULL)
		{
			printf("Could not allocate enough memory!\n");
			return ERR_ALLOC;
		}
		if ((readlength = read(hFile, pBuf, filelength)) <= 0)
		{
			printf("Could not read the file '%s'\n", src);
			return ERR_READ;
		}
		if (readlength != filelength)
		{
			printf("Could not read the file '%s' to the end!\n", src);
			return ERR_READ;
		}
		close(hFile);

	}


	// Now check the status of every file in the working copy
	// and gather revision status information in SubStat.

	apr_pool_t * pool;
	svn_error_t * svnerr = NULL;
	svn_client_ctx_t* ctx;
	const char * internalpath;	

	apr_initialize();
	apr_pool_create_ex(&pool, NULL, abort_on_pool_failure, NULL);

	svn_client_create_context(&ctx, pool);
	
// This only works with SVN headers of version 1.3.x or above
#if SVN_VER_MINOR > 2
	if (getenv ("SVN_ASP_DOT_NET_HACK"))
	{
		svn_wc_set_adm_dir ("_svn", pool);
	}
#endif

	const char* utf8Path = new char[MAX_PATH];
 	svn_utf_cstring_to_utf8(&utf8Path, wc, pool);
	internalpath = svn_path_internal_style (utf8Path, pool);

	svnerr = svn_status(	internalpath,	//path
							&SubStat,		//status_baton
							TRUE,			//noignore
							ctx,
							pool);

	if (svnerr){
		svn_handle_error2(svnerr, stderr, FALSE, "svnwcrev : ");
	}
	apr_pool_destroy(pool);
	apr_terminate2();
	
	if (bErrOnMods && SubStat.HasMods)
	{
		printf("Working copy has local modifications!\n");
		return ERR_SVN_MODS;
	}
	
	if (bErrOnMixed && (SubStat.MinRev != SubStat.MaxRev))
	{
	  if (SubStat.bHexPlain)
	    printf("Working copy contains mixed revisions %LX:%LX!\n", (long long int)SubStat.MinRev, (long long int)SubStat.MaxRev);
	  else if (SubStat.bHexX)
            printf("Working copy contains mixed revisions %#LX:%#LX!\n", (long long int)SubStat.MinRev, (long long int)SubStat.MaxRev);
	  else
	    printf("Working copy contains mixed revisions %Ld:%Ld!\n", (long long int)SubStat.MinRev, (long long int)SubStat.MaxRev);
	  return ERR_SVN_MIXED;
	}
	
	if (SubStat.bHexPlain)
	  printf("Last committed at revision %LX\n", (long long int)SubStat.CmtRev);
	else if (SubStat.bHexX)
	  printf ("Last committed at revision %#LX\n", (long long int)SubStat.CmtRev);
	else
	  printf("Last committed at revision %Ld\n", (long long int)SubStat.CmtRev);

	if (SubStat.MinRev != SubStat.MaxRev)
	{
	  if (SubStat.bHexPlain)
            printf("Mixed revision range %LX:%LX\n", (long long int)SubStat.MinRev, (long long int)SubStat.MaxRev);
	  else if (SubStat.bHexX)
            printf("Mixed revision range %#LX:%#LX\n", (long long int)SubStat.MinRev, (long long int)SubStat.MaxRev);
	  else
	    printf("Mixed revision range %Ld:%Ld\n", (long long int)SubStat.MinRev, (long long int)SubStat.MaxRev);
	}
	else
	{
	  if (SubStat.bHexPlain)
            printf("Updated to revision %LX\n", (long long int)SubStat.MaxRev);
	  else if (SubStat.bHexX)
            printf("Updated to revision %#LX\n", (long long int)SubStat.MaxRev);
	  else
	    printf("Updated to revision %Ld\n", (long long int)SubStat.MaxRev);
	}
	
	if (SubStat.HasMods)
	{
		printf("Local modifications found\n");
	}

	if (dst == NULL)
	{
		return 0;
	}

	// now parse the filecontents for version defines.
	
	size_t index = 0;
	
	while (InsertRevision((char *)VERDEF, pBuf, index, filelength, maxlength, -1, SubStat.CmtRev, &SubStat));
	
	index = 0;
	while (InsertRevision((char *)RANGEDEF, pBuf, index, filelength, maxlength, SubStat.MinRev, SubStat.MaxRev, &SubStat));
	
	index = 0;
	while (InsertDate((char *)DATEDEF, pBuf, index, filelength, maxlength, SubStat.CmtDate));
	
	index = 0;
	while (InsertDate((char *)DATEDEFUTC, pBuf, index, filelength, maxlength, SubStat.CmtDate));

	index = 0;
	while (InsertDate((char *)DATEWFMTDEF, pBuf, index, filelength, maxlength, SubStat.CmtDate));
	index = 0;
	while (InsertDate((char *)DATEWFMTDEFUTC, pBuf, index, filelength, maxlength, SubStat.CmtDate));
	
	index = 0;
	while (InsertDate((char *)NOWDEF, pBuf, index, filelength, maxlength, USE_TIME_NOW));

	index = 0;
	while (InsertDate((char *)NOWDEFUTC, pBuf, index, filelength, maxlength, USE_TIME_NOW));

	index = 0;
	while (InsertDate((char *)NOWWFMTDEF, pBuf, index, filelength, maxlength, USE_TIME_NOW));

	index = 0;
	while (InsertDate((char *)NOWWFMTDEFUTC, pBuf, index, filelength, maxlength, USE_TIME_NOW));
	
	index = 0;
	while (InsertBoolean((char *)MODDEF, pBuf, index, filelength, SubStat.HasMods));
	
	index = 0;
	while (InsertBoolean((char *)MIXEDDEF, pBuf, index, filelength, SubStat.MinRev != SubStat.MaxRev));
	
	
	index = 0;
	while (InsertUrl((char *)URLDEF, pBuf, index, filelength, maxlength, SubStat.Url));
	
	index = 0;
	while (InsertBoolean((char *)ISINSVN, pBuf, index, filelength, SubStat.bIsSvnItem));

	index = 0;
	while (InsertBoolean((char *)NEEDSLOCK, pBuf, index, filelength, SubStat.LockData.NeedsLocks));
	
	index = 0;
	while (InsertBoolean((char *)ISLOCKED, pBuf, index, filelength,  SubStat.LockData.IsLocked));

	index = 0;
	while (InsertDate((char *)LOCKDATE, pBuf, index, filelength, maxlength, SubStat.LockData.CreationDate));
	
	index = 0;
	while (InsertDate((char *)LOCKDATEUTC, pBuf, index, filelength, maxlength, SubStat.LockData.CreationDate));
	
	index = 0;
	while (InsertDate((char *)LOCKWFMTDEF, pBuf, index, filelength, maxlength, SubStat.LockData.CreationDate));
	
	index = 0;
	while (InsertDate((char *)LOCKWFMTDEFUTC, pBuf, index, filelength, maxlength, SubStat.LockData.CreationDate));
	
	index = 0;
	while (InsertUrl((char *)LOCKOWNER, pBuf, index, filelength, maxlength, SubStat.LockData.Owner));
	
	index = 0;
	while (InsertUrl((char *)LOCKCOMMENT, pBuf, index, filelength, maxlength, SubStat.LockData.Comment));

	


	
	hFile = open(dst, O_RDWR | O_CREAT);
	if (hFile == -1)
	{
		printf("Unable to open output file '%s' for writing\n", dst);
		return ERR_OPEN;
	}

	struct stat status;
	if(fstat(hFile, &status) != 0){
		printf("Unable retrieve satus of output file '%s'\n", dst);
		return ERR_OPEN;
	}
	
	size_t filelengthExisting = status.st_size;

	bool sameFileContent = false;
	if (filelength == filelengthExisting)
	{
		size_t readlengthExisting = 0;
		char * pBufExisting = new char[filelength];
		if ((readlengthExisting = read(hFile, pBufExisting, filelengthExisting)) <= 0)
		{
			printf("Could not read the file '%s'\n", dst);
			return ERR_READ;
		}
		if (readlengthExisting != filelengthExisting)
		{
			printf("Could not read the file '%s' to the end!\n", dst);
			return ERR_READ;
		}
		sameFileContent = (memcmp(pBuf, pBufExisting, filelength) == 0);
		delete [] pBufExisting;
	}

	// The file is only written if its contents would change.
	// This prevents the timestamp from changing.
	if (!sameFileContent)
	{
		lseek(hFile, 0, SEEK_SET);

		readlength = write(hFile, pBuf, filelength);
		if (readlength != filelength)
		{
			printf("Could not write the file '%s' to the end!\n", dst);
			return ERR_READ;
		}

		if (ftruncate(hFile, filelength) != 0)
		{
			printf("Could not truncate the file '%s' to the end!\n", dst);
			return ERR_READ;
		}
		
		// HACK: set file modes to the same as the input file
		// as open seems to ignore the umask setting (??)
		// TODO: Why is this? --> Find better solution
		fchmod(hFile, inputStatus.st_mode); 
	}
	close(hFile);
	delete [] pBuf;
	
	return 0;
}

