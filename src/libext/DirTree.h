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

/** @file  */

#ifndef SRC_LIBEXT_DIRTREE_H_
#define SRC_LIBEXT_DIRTREE_H_

#include <config.h>

#include <vector>
#include <string>

/*
 *
 */
class DirTree
{
public:
	DirTree();
	~DirTree();

	void Read(std::vector<std::string> start_paths);
};

#endif /* SRC_LIBEXT_DIRTREE_H_ */
