// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2005 - Stefan Kueng
// Modified by Oliver Pajonk

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
//#pragma once
#include <vector>

#include <apr_pools.h>
#include "svn_error.h"
#include "svn_client.h"
#include "svn_path.h"


#define URL_BUF	2048
#define OWNER_BUF	2048
#define COMMENT_BUF	4096
#define MAX_PATH 512 // is this enough?

/**
 * \ingroup SubWCRev
 * This structure is used as a part of the status baton for WC crawling
 * and contains all the information we are collecting regarding the lock status.
 */
typedef struct SubWcLockData_t
{
	bool NeedsLocks;			// TRUE if a lock can be applied in generally; if FALSE, the values of the other parms in this struct are invalid
	bool IsLocked;				// TRUE if the file or folder is locked
	char Owner[OWNER_BUF];		// the username which owns the lock
	char Comment[COMMENT_BUF];	// lock comment
	apr_time_t CreationDate;    // when lock was made
	
} SubWcLockData_t;

// This structure is used as the status baton for WC crawling
// and contains all the information we are collecting.
typedef struct SubWCRev_t
{
	svn_revnum_t MinRev;	// Lowest update revision found
	svn_revnum_t MaxRev;	// Highest update revision found
	svn_revnum_t CmtRev;	// Highest commit revision found
	apr_time_t CmtDate;		// Date of highest commit revision
	bool HasMods;			// True if local modifications found
	bool bFolders;			// If TRUE, status of folders is included
	bool bExternals;		// If TRUE, status of externals is included
	bool bHexPlain;			// If TRUE, revision numbers are output in HEX
	bool bHexX;				// If TRUE, revision numbers are output in HEX with '0x'
	char Url[URL_BUF];		// URL of working copy
	char UUID[1024];	// The repository UUID of the working copy
	char Author[URL_BUF];	// The author of the wcPath
	bool  bIsSvnItem;			// True if the item is under SVN
	SubWcLockData_t LockData;	// Data regarding the lock of the file
} SubWCRev_t;

typedef struct SubWCRev_StatusBaton_t
{
	SubWCRev_t * SubStat;
	std::vector<const char *> * extarray;
	apr_pool_t *pool;
} SubWCRev_StatusBaton_t;

svn_error_t *
svn_status (       const char *path,
                   void *status_baton,
                   svn_boolean_t no_ignore,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

