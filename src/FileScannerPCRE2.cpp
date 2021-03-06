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

#include "FileScannerPCRE2.h"

#include <iostream>
#include <future/string.hpp>
#include <cstring>

#include "libext/hints.hpp"

#include "Logger.h"

#ifdef HAVE_LIBPCRE2
/**
 * This callout handler is invoked by PCRE2 at the end of a potentially successful match.  It's purpose
 * is to prevent a regex like 'abc\s+def' from matching across an eol boundary, since '\s' matches both
 * 'normal' spaces and also newlines.
 *
 * It works in conjunction with a wrapper the constructor puts around the incoming regex, "(?:" + regex + ")(?=.*?$)(?C1)".
 * What happens is that when PCRE2 finds a potential match of the given regex, the (?C1) causes this function to be called.
 * This function then scans the potential match for a '\n' character.  If it finds one, the potential match is rejected by returning
 * a positive integer (+1), and if it's able to, PCRE2 backtracks and looks for a different match solution.  If no
 * '\n' is found, 0 is returned, and the match is accepted.
 *
 * @param cob
 * @param ctx
 * @return
 */
static int callout_handler(pcre2_callout_block *cob, void *ctx)
{
	(void)ctx;
	const char * p = (const char *)std::memchr(cob->subject+cob->start_match, '\n', cob->current_position - cob->start_match);

	std::string cur_match_str((const char *)cob->subject+cob->start_match, (const char *)cob->subject + cob->current_position);

	if(p == nullptr)
	{
		//std::cerr << "CALLOUT: No eols yet, string is: \"" << cur_match_str << "\"\n";
		return 0;
	}
	else
	{
		//std::cerr << "CALLOUT: Found eol, string is: \"" << cur_match_str << "\"\n";
		return 1;
	}
}

static int count_callouts_callback([[maybe_unused]] pcre2_callout_enumerate_block *ceb, void *ctx)
{
	size_t * ctr { reinterpret_cast<size_t*>(ctx) };

	*ctr = *ctr + 1;

	return 0;
}

/**
 * Returns the number of callouts in the given compiled regex.
 * @param code  Pointer to the compiled regex.
 * @return Number of callouts in the regex.
 */
static size_t pattern_num_callouts(const pcre2_code *code)
{
	size_t num_callouts {0};

	pcre2_callout_enumerate(code, count_callouts_callback, &num_callouts);

	return num_callouts;
}

#endif

FileScannerPCRE2::FileScannerPCRE2(sync_queue<FileID> &in_queue,
		sync_queue<MatchList> &output_queue,
		std::string regex,
		bool ignore_case,
		bool word_regexp,
		bool pattern_is_literal) : FileScanner(in_queue, output_queue, regex, ignore_case, word_regexp, pattern_is_literal)
{
#ifdef HAVE_LIBPCRE2
	// Compile the regex.
	int error_code;
	PCRE2_SIZE error_offset;
	uint32_t regex_compile_options = 0;

	// For now, we won't support capturing.  () will be treated as (?:).
	regex_compile_options = PCRE2_NO_AUTO_CAPTURE | PCRE2_MULTILINE | PCRE2_NEVER_BACKSLASH_C | PCRE2_NEVER_UTF | PCRE2_NEVER_UCP
			| PCRE2_JIT_COMPLETE;

	if(ignore_case)
	{
		// Ignore case while matching.
		regex_compile_options |= PCRE2_CASELESS;
	}

	if(m_pattern_is_literal)
	{
		// Surround the pattern with \Q...\E so it's treated as a literal string.
		regex = "\\Q" + regex + "\\E";
	}

	if(m_word_regexp)
	{
		// Surround the regex with \b (word boundary) assertions.
		regex = "\\b(?:" + regex + ")\\b";
	}

	// Put in our callout, which essentially exists to make '\s' not match a newline.
	regex = "(?:" + regex + ")(?=.*?$)(?C1)";

	// Compile the regex.
	m_pcre2_regex = pcre2_compile(reinterpret_cast<PCRE2_SPTR8>(regex.c_str()), regex.length(), regex_compile_options, &error_code, &error_offset, NULL);

	if (m_pcre2_regex == NULL)
	{
		// Regex compile failed, we can't continue.
		throw FileScannerException(std::string("Compilation of regex \"") + regex + "\" failed at offset " + std::to_string(error_offset) + ": "
				+ PCRE2ErrorCodeToErrorString(error_code));
	}

	int jit_retval = pcre2_jit_compile(m_pcre2_regex, PCRE2_JIT_COMPLETE);

	if(jit_retval != 0)
	{
		// Was it a real error, or does the PCRE2 lib not have JIT compiled in?
		if(jit_retval == PCRE2_ERROR_JIT_BADOPTION)
		{
			// No JIT support.
			LOG(INFO) << "No PCRE2 JIT support: " << PCRE2ErrorCodeToErrorString(jit_retval);
		}
		else
		{
			// JIT compilation error.
			pcre2_code_free(m_pcre2_regex);
			throw FileScannerException(std::string("PCRE2 JIT compilation error: ") + PCRE2ErrorCodeToErrorString(jit_retval));
		}
	}

	// Only allow the one callout we use internally, no user callouts.
	if(pattern_num_callouts(m_pcre2_regex) > 1)
	{
		pcre2_code_free(m_pcre2_regex);
		throw FileScannerException(std::string("Callouts not supported."));
	}
#endif
}

FileScannerPCRE2::~FileScannerPCRE2()
{
#ifdef HAVE_LIBPCRE2
	pcre2_code_free(m_pcre2_regex);
#endif
}

#ifdef HAVE_LIBPCRE2
/// @name Custom deleters for the PCRE2 objects we'll be using.
/// These are implemented as specializations of the std::default_delete<> template.
/// @{
namespace std
{

template<>
struct default_delete<pcre2_match_data>
{
	void operator()(pcre2_match_data *ptr) { pcre2_match_data_free(ptr); };
};

template<>
struct default_delete<pcre2_match_context>
{
	void operator()(pcre2_match_context *mctx) { pcre2_match_context_free(mctx); };
};

}
/// @}
#endif

void FileScannerPCRE2::ScanFile(const char* __restrict__ file_data, size_t file_size, MatchList& ml)
{
#ifdef HAVE_LIBPCRE2
	// Pointer to the offset vector returned by pcre2_match().
	PCRE2_SIZE *ovector;

	// Create a std::unique_ptr<> with a custom deleter (see above) to manage the lifetime of the match data.
	std::unique_ptr<pcre2_match_data> match_data;

	size_t line_no {1};
	size_t prev_lineno {0};
	const char *prev_lineno_search_end {file_data};
	size_t start_offset { 0 };

	match_data.reset(pcre2_match_data_create_from_pattern(m_pcre2_regex, NULL));
	ovector = pcre2_get_ovector_pointer(match_data.get());
	// Fool the "previous match was zero-length" logic for the first iteration.
	ovector[0] = -1;
	ovector[1] = 0;

	std::unique_ptr<pcre2_match_context> mctx(pcre2_match_context_create(NULL));
	// Hook in our callout function.
	pcre2_set_callout(mctx.get(), callout_handler, this);

	// Loop while the start_offset is less than the file_size.
	while(start_offset < file_size)
	{
		int options = 0;
		start_offset = ovector[1];

		// Was the previous match zero-length?
		if (ovector[0] == ovector[1])
		{
			// Yes, are we at the end of the file?
			if (ovector[0] == file_size)
			{
				// Yes, we're done searching.
				break;
			}

			// Not done, set options for another try for a non-empty match at the same point.
			options = PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED;
		}

		// Try to match the regex to whatever's left of the file.
		int rc = pcre2_match(
				m_pcre2_regex,
				reinterpret_cast<PCRE2_SPTR>(file_data),
				file_size,
				start_offset,
				options,
				match_data.get(),
				mctx.get()
				);

		// Check for no match.
		if(rc == PCRE2_ERROR_NOMATCH)
		{
			if(options == 0 )
			{
				// We weren't trying to recover from a zero-length match, so there are no more matches.
				// Break out of the loop.
				break;
			}
			else
			{
				// We've failed to find a non-empty-string match at a point where
				// we previously found an empty-string match.
				// Advance one character and continue.
				ovector[1] = start_offset + 1;

				/**
				 * @todo If we're treating \r\n as a newline, we have to check here to see
				 *       if we are at the start of one, and if so, skip over the whole thing.
				 *       For now, we don't support this.
				 */
				if(/** @todo crlf_is_newline */ false &&
						start_offset < file_size-1 &&
						file_data[start_offset] == '\r' &&
						file_data[start_offset+1] == '\n')
				{
					// Increment the new start position by one more byte, we're at a \r\n line ending.
					ovector[1]++;
				}
				/**
				 * @todo Similarly, if we support UTF-8, we have to skip all bytes in the
				 *       possibly multi-byte character.
				 *       Again, UTF-8 is not something we support at the moment.
				 */
				else if(false /** @todo utf8 */)
				{
					// Increment a whole UTF8 character.
					while(ovector[1] < file_size)
					{
						if((file_data[ovector[1]] & 0xC0) != 0x80)
						{
							// Found a non-start-byte.
							break;
						}
						else
						{
							// Go to the next byte in the character.
							ovector[1]++;
						}
					}
				}
			}

			// Try to match again.
			continue;
		}

		// Check for non-PCRE2_ERROR_NOMATCH error codes.
		if(rc < 0)
		{
			// Match error.  Convert to string, throw exception.
			throw FileScannerException(std::string("PCRE2 match error: ") + PCRE2ErrorCodeToErrorString(rc));
			return;
		}
		if (rc == 0)
		{
			ERROR() << "ovector only has room for 1 captured substring" << std::endl;
			return;
		}

		// There was a match.  Package it up in the MatchList which was passed in.
		line_no += CountLinesSinceLastMatch(prev_lineno_search_end, file_data+ovector[0]);
		prev_lineno_search_end = file_data+ovector[0];
		if(line_no == prev_lineno)
		{
			// Skip multiple matches on one line.
			continue;
		}
		prev_lineno = line_no;
		Match m(file_data, file_size, ovector[0], ovector[1], line_no);

		ml.AddMatch(std::move(m));
	}
#endif // HAVE_LIBPCRE2
}

std::string FileScannerPCRE2::PCRE2ErrorCodeToErrorString(int errorcode)
{
	std::string retstr = "";
#ifdef HAVE_LIBPCRE2
	PCRE2_SIZE error_msg_buf_size = 512;
	PCRE2_UCHAR *error_msg_buf = new PCRE2_UCHAR[error_msg_buf_size];
	int retval = pcre2_get_error_message(errorcode, error_msg_buf, error_msg_buf_size);
	if(retval>=0)
	{
		retstr.assign(reinterpret_cast<const char *>(error_msg_buf));
	}
#endif
	return retstr;
}
