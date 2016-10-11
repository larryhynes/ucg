/*
 * Copyright 2016 Gary R. Van Sickle (grvs@users.sourceforge.net).
 *
 * This file is part of UniversalCodeGrep.
 *
 * UniversalCodeGrep is free software: you can redistribute it and/or modify it under the
 * terms of version 3 of the GNU General Public License as published by the Free
 * Software Foundation.
 *
 * UniversalCodeGrep is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * UniversalCodeGrep.  If not, see <http://www.gnu.org/licenses/>.
 */

/** @file */

#include <config.h>

#include "FileID.h"

#include <fcntl.h> // For AT_FDCWD, AT_NO_AUTOMOUNT
#include <unistd.h> // For close().
#include <sys/stat.h>
#include <fts.h>

std::mutex FileID::sm_mutables_mutex;

FileID::FileID(path_known_cwd_tag) : m_basename("."), m_path("."), m_file_descriptor(make_shared_fd(AT_FDCWD)), m_file_type(FT_DIR)
{
}

FileID::FileID(path_known_relative_tag, std::shared_ptr<FileID> at_dir_fileid, std::string basename, FileType type)
	: m_basename(basename), m_at_dir(at_dir_fileid), m_file_type(type)
{
	/// @note Taking basename by value since we are always storing it.
	/// Full openat() semantics:
	/// - If pathname is absolute, at_dir_fd is ignored.
	/// - If pathname is relative, it's relative to at_dir_fd.
}

FileID::FileID(path_known_absolute_tag, std::shared_ptr<FileID> at_dir_fileid, std::string pathname, FileType type)
	: m_basename(pathname), m_at_dir(at_dir_fileid), m_path(pathname), m_file_type(type)
{
	/// @note Taking pathname by value since we are always storing it.
	/// Full openat() semantics:
	/// - If pathname is absolute, at_dir_fd is ignored.
	/// - If pathname is relative, it's relative to at_dir_fd.
}

FileID::FileID(std::shared_ptr<FileID> at_dir_fileid, std::string pathname)
	: m_basename(pathname), m_at_dir(at_dir_fileid)
{
	/// @note Taking pathname by value since we are always storing it.
	/// Full openat() semantics:
	/// - If pathname is absolute, at_dir_fd is ignored.
	/// - If pathname is relative, it's relative to at_dir_fd.

	if(is_pathname_absolute(pathname))
	{
		m_path = pathname;
	}
}

FileID::FileID(const dirent *de)
{

}

FileID::FileID(const FTSENT *ftsent): m_path(ftsent->fts_path, ftsent->fts_pathlen)
{
	// Initialize the stat fields if possible.
	if(ftsent->fts_statp != nullptr)
	{
		m_stat_info_valid = true;
		m_unique_file_identifier = dev_ino_pair(ftsent->fts_statp->st_dev, ftsent->fts_statp->st_ino);
		m_size = ftsent->fts_statp->st_size;
		m_block_size = ftsent->fts_statp->st_blksize;
		m_blocks = ftsent->fts_statp->st_blocks;
	}
}

FileID::~FileID()
{

}

const std::string& FileID::GetPath() const
{
	std::lock_guard<std::mutex> lg(sm_mutables_mutex);
	return UnsyncedGetPath();
}

const std::string& FileID::UnsyncedGetPath() const
{
	if(m_path.empty())
	{
		// Build the full path.
		if(!m_at_dir->IsAtFDCWD())
		{
			auto at_path = m_at_dir->UnsyncedGetPath();
			m_path.reserve(at_path.size() + m_basename.size() + 2);
			m_path = at_path + '/' + m_basename;
		}
		else
		{
			m_path = m_basename;
		}
	}

	return m_path;
}

const std::shared_ptr<FileID>& FileID::GetAtDirCRef() const noexcept
{
	if(!m_at_dir)
	{
		throw std::runtime_error("no atdir");
	}
	return m_at_dir;
};

const std::string& FileID::GetAtDirRelativeBasename() const noexcept
{
	if(m_basename.empty())
	{
		throw std::runtime_error("basename was empty");
	}
	return m_basename;
}

FileID::FileType FileID::GetFileType() const noexcept
{
	if(m_file_type == FT_UNINITIALIZED)
	{
		// We don't know the file type yet.  We'll have to get it from a stat() call.
		LazyLoadStatInfo();
	}

	return m_file_type;
}

void FileID::SetDevIno(dev_t d, ino_t i) noexcept
{
	m_dev = d;
	m_unique_file_identifier = dev_ino_pair(d, i);
}

void FileID::LazyLoadStatInfo() const
{
	if(IsStatInfoValid())
	{
		// Already set.
		return;
	}

	// We don't have stat info and now we need it.
	// Get it from the filename.
	struct stat stat_buf;
	if(stat(GetPath().c_str(), &stat_buf) != 0)
	{
		// Error.
		m_file_type = FT_STAT_FAILED;
	}
	else
	{
		m_stat_info_valid = true;

		// Determine file type.
		if(S_ISREG(stat_buf.st_mode))
		{
			m_file_type = FT_REG;
		}
		else if(S_ISDIR(stat_buf.st_mode))
		{
			m_file_type = FT_DIR;
		}
		else if(S_ISLNK(stat_buf.st_mode))
		{
			m_file_type = FT_SYMLINK;
		}
		else
		{
			// Those are the only types we know or care about.
			m_file_type = FT_UNKNOWN;
		}

		m_dev = stat_buf.st_dev;
		m_unique_file_identifier = dev_ino_pair(stat_buf.st_dev, stat_buf.st_ino);
		m_size = stat_buf.st_size;
		m_block_size = stat_buf.st_blksize;
		m_blocks = stat_buf.st_blocks;
	}
}

FileDescriptor FileID::GetFileDescriptor()
{
	/// @todo This needs rethinking.  The FD would be opened differently depending on the file type etc.


	if(*m_file_descriptor == cm_invalid_file_descriptor)
	{
		// File hasn't been opened.

		if(m_basename.empty() && !m_path.empty())
		{
			m_file_descriptor = make_shared_fd(openat(AT_FDCWD, m_path.c_str(), (O_SEARCH ? O_SEARCH : O_RDONLY) | O_DIRECTORY | O_NOCTTY));
		}
		else
		{
			m_file_descriptor = make_shared_fd(openat(*(m_at_dir->GetFileDescriptor()), GetBasename().c_str(), (O_SEARCH ? O_SEARCH : O_RDONLY) | O_DIRECTORY | O_NOCTTY));
		}

		if(*m_file_descriptor < 0)
		{
			throw std::runtime_error("Bad fd");
		}
	}

	return m_file_descriptor;
}
