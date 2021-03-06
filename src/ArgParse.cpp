/*
 * Copyright 2015-2016 Gary R. Van Sickle (grvs@users.sourceforge.net).
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

/** @file ArgParse.cpp
 * This is the implementation of the ArgParse class.  Because of the use of GNU argp (a C library) for arg parsing,
 * there's a healthy mix of C in here as well as C++; a tribute to the interoperability of the two languages.
 */

#include <config.h>

#include "ArgParse.h"

#include "../build_info.h"

#include <libext/cpuidex.hpp>

#include <locale>
#include <algorithm>
#include <vector>
#include <string>
#include <locale>
#include <thread>
#include <iostream>
#include <sstream>
#include <system_error>

#include <argp.h>
#ifdef HAVE_LIBPCRE
#include <pcre.h>
#endif
#ifdef HAVE_LIBPCRE2
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#endif
#include <cstdlib>
#include <cstring>
#include <cstdio>

#ifdef HAVE_PWD_H
#include <pwd.h> // for GetUserHomeDir()-->getpwuid().
#endif

#include <fcntl.h>
#include <unistd.h> // for GetUserHomeDir()-->getuid().
#include <sys/stat.h>

#include <libext/string.hpp>
#include <libext/filesystem.hpp>

#include "TypeManager.h"
#include "File.h"
#include "Logger.h"

// The sweet spot for the number of directory tree traversal threads seems to be 2 on Linux, independent of the
// number of scanner threads.  Cygwin does better with 3 or 4 here (and more dirjobs with more scanner threads) since it
// spends so much more time in the Windows<->POSIX path resolution logic.
static constexpr size_t f_default_dirjobs = 2;

// Our --version output isn't just a static string, so we'll register with argp for a version callback.
static void PrintVersionTextRedirector(FILE *stream, struct argp_state *state)
{
	static_cast<ArgParse*>(state->input)->PrintVersionText(stream);
}
void (*argp_program_version_hook)(FILE *stream, struct argp_state *state) = PrintVersionTextRedirector;


// Not static, argp.h externs this.
const char *argp_program_version = PACKAGE_STRING "\n"
	"Copyright (C) 2015-2016 Gary R. Van Sickle.\n"
	"\n"
	"This program is free software; you can redistribute it and/or modify\n"
	"it under the terms of version 3 of the GNU General Public License as\n"
	"published by the Free Software Foundation.\n"
	"\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU General Public License for more details.\n"
	"\n"
	"You should have received a copy of the GNU General Public License\n"
	"along with this program. If not, see http://www.gnu.org/licenses/."
	;

// Not static, argp.h externs this.
const char *argp_program_bug_address = PACKAGE_BUGREPORT;

/**
 * The pre- and post-opion help text.
 */
static const char doc[] = "\nucg: the UniversalCodeGrep code search tool."
		"\vExit status is 0 if any matches were found, 1 if no matches, 2 or greater on error.";

/**
 * The "Usage:" text.
 */
static const char args_doc[] = "PATTERN [FILES OR DIRECTORIES]";

/// Keys for options without short-options.
enum OPT
{
	OPT_RESERVED = 0,
	OPT_SMART_CASE,
	OPT_NO_SMART_CASE,
	OPT_COLOR,
	OPT_NOCOLOR,
	OPT_IGNORE_DIR,
	OPT_NOIGNORE_DIR,
	OPT_IGNORE_FILE,
	OPT_INCLUDE,
	OPT_EXCLUDE,
	OPT_TYPE,
	OPT_NOENV,
	OPT_TYPE_SET,
	OPT_TYPE_ADD,
	OPT_TYPE_DEL,
	OPT_PERF_DIRJOBS,
	OPT_HELP_TYPES,
	OPT_COLUMN,
	OPT_NOCOLUMN,
	OPT_TEST_LOG_ALL,
	OPT_TEST_NOENV_USER,
	OPT_TEST_USE_MMAP,
	OPT_BRACKET_NO_STANDIN
};

/// Status code to use for a bad parameter which terminates the program via argp_failure().
/// Ack returns 255 in this case, so we'll use that instead of BSD's EX_USAGE, which is 64.
#define STATUS_EX_USAGE 255

// Not static, argp.h externs this.
error_t argp_err_exit_status = STATUS_EX_USAGE;

/// Argp Option Definitions
// Disable (at least on gcc) the large number of spurious warnings about missing initializers
// the declaration of options[] and ArgParse::argp normally cause.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static struct argp_option options[] = {
		{0,0,0,0, "Searching:" },
		{"ignore-case", 'i', 0,	0,	"Ignore case distinctions in PATTERN."},
		{"[no]smart-case", OPT_BRACKET_NO_STANDIN, 0, 0, "Ignore case if PATTERN is all lowercase (default: enabled)."},
		{"smart-case", OPT_SMART_CASE, 0, OPTION_HIDDEN, ""},
		{"nosmart-case", OPT_NO_SMART_CASE, 0, OPTION_HIDDEN, ""},
		{"no-smart-case", OPT_NO_SMART_CASE, 0, OPTION_HIDDEN | OPTION_ALIAS },
		{"word-regexp", 'w', 0, 0, "PATTERN must match a complete word."},
		{"literal", 'Q', 0, 0, "Treat all characters in PATTERN as literal."},
		{0,0,0,0, "Search Output:"},
		{"column", OPT_COLUMN, 0, 0, "Print column of first match after line number."},
		{"nocolumn", OPT_NOCOLUMN, 0, 0, "Don't print column of first match (default)."},
		{0,0,0,0, "File presentation:" },
		{"color", OPT_COLOR, 0, 0, "Render the output with ANSI color codes."},
		{"colour", OPT_COLOR, 0, OPTION_ALIAS },
		{"nocolor", OPT_NOCOLOR, 0, 0, "Render the output without ANSI color codes."},
		{"nocolour", OPT_NOCOLOR, 0, OPTION_ALIAS },
		{0,0,0,0, "File/directory inclusion/exclusion:"},
		{"[no]ignore-dir", OPT_BRACKET_NO_STANDIN, "name", 0, "[Do not] exclude directories with this name."},
		{"[no]ignore-directory", OPT_BRACKET_NO_STANDIN, "name", OPTION_ALIAS },
		{"ignore-dir",  OPT_IGNORE_DIR, "name", OPTION_HIDDEN,  ""},
		{"ignore-directory", OPT_IGNORE_DIR, "name", OPTION_HIDDEN | OPTION_ALIAS },
		{"noignore-dir",  OPT_NOIGNORE_DIR, "name", OPTION_HIDDEN,  ""},
		{"noignore-directory", OPT_NOIGNORE_DIR, "name", OPTION_HIDDEN | OPTION_ALIAS },
		// ack-style --ignore-file=FILTER:FILTERARGS
		{"ignore-file", OPT_IGNORE_FILE, "FILTER:FILTERARGS", 0, "Files matching FILTER:FILTERARGS (e.g. ext:txt,cpp) will be ignored."},
		// grep-style --include=glob and --exclude=glob
		{"include", OPT_INCLUDE, "GLOB", 0, "Only files matching GLOB will be searched."},
		{"exclude", OPT_EXCLUDE, "GLOB", 0, "Files matching GLOB will be ignored."},
		// ag-style --ignore=GLOB
		// In ag, this option applies to both files and directories.  For the present, ucg will only apply this to files.
		{"ignore", OPT_EXCLUDE, "GLOB", OPTION_ALIAS },
		{"recurse", 'r', 0, 0, "Recurse into subdirectories (default: on)." },
		{0, 'R', 0, OPTION_ALIAS },
		{"no-recurse", 'n', 0, 0, "Do not recurse into subdirectories."},
		{"known-types", 'k', 0, 0, "Only search in files of recognized types (default: on)."},
		{"type", OPT_TYPE, "[no]TYPE", 0, "Include only [exclude all] TYPE files.  Types may also be specified as --[no]TYPE."},
		{0,0,0,0, "File type specification:"},
		{"type-set", OPT_TYPE_SET, "TYPE:FILTER:FILTERARGS", 0, "Files FILTERed with the given FILTERARGS are treated as belonging to type TYPE.  Any existing definition of type TYPE is replaced."},
		{"type-add", OPT_TYPE_ADD, "TYPE:FILTER:FILTERARGS", 0, "Files FILTERed with the given FILTERARGS are treated as belonging to type TYPE.  Any existing definition of type TYPE is appended to."},
		{"type-del", OPT_TYPE_DEL, "TYPE", 0, "Remove any existing definition of type TYPE."},
		{0,0,0,0, "Performance tuning:"},
		{"jobs",  'j', "NUM_JOBS",      0,  "Number of scanner jobs (std::thread<>s) to use." },
		{"dirjobs",  OPT_PERF_DIRJOBS, "NUM_JOBS",      0,  "Number of directory traversal jobs (std::thread<>s) to use." },
		{0,0,0,0, "Miscellaneous:" },
		{"noenv", OPT_NOENV, 0, 0, "Ignore .ucgrc files."},
		{0,0,0,0, "Informational options:", -1}, // -1 is the same group the default --help and --version are in.
		{"help-types", OPT_HELP_TYPES, 0, 0, "Print list of supported file types."},
		{"list-file-types", 0, 0, OPTION_ALIAS }, // For ag compatibility.
		// Hidden options for debug, test, etc.
		// DO NOT USE THESE.  They're going to change and go away without notice.
		{"test-log-all", OPT_TEST_LOG_ALL, 0, OPTION_HIDDEN, "Enable all logging output."},
		{"test-noenv-user", OPT_TEST_NOENV_USER, 0, OPTION_HIDDEN, "Don't search for or use $HOME/.ucgrc."},
		{"test-use-mmap", OPT_TEST_USE_MMAP, 0, OPTION_HIDDEN, "Use mmap() to access files being searched."},
		{ 0 }
	};

/// The argp struct for argp.
struct argp ArgParse::argp = { options, ArgParse::parse_opt, args_doc, doc };

#pragma GCC diagnostic pop // Re-enable -Wmissing-field-initializers


error_t ArgParse::parse_opt (int key, char *arg, struct argp_state *state)
{
	class ArgParse *arguments = static_cast<ArgParse*>(state->input);

	switch (key)
	{
	case 'i':
		arguments->m_ignore_case = true;
		// Shut off smart-case.
		arguments->m_smart_case = false;
		break;
	case OPT_SMART_CASE:
		arguments->m_smart_case = true;
		// Shut off ignore-case.
		arguments->m_ignore_case = false;
		break;
	case OPT_NO_SMART_CASE:
		arguments->m_smart_case = false;
		// Don't change ignore-case, regardless of whether it's on or off.
		break;
	case 'w':
		arguments->m_word_regexp = true;
		break;
	case 'Q':
		arguments->m_pattern_is_literal = true;
		break;
	case OPT_COLUMN:
		arguments->m_column = true;
		break;
	case OPT_NOCOLUMN:
		arguments->m_column = false;
		break;
	case OPT_IGNORE_DIR:
		arguments->m_excludes.insert(arg);
		break;
	case OPT_NOIGNORE_DIR:
		/**
		 * @todo Ack is fancier in its noignore handling.  If you noignore a directory under an ignored
		 * directory, it gets put back into the set of paths that will be searched.  Feature for another day.
		 */
		arguments->m_excludes.erase(arg);
		break;
	case OPT_IGNORE_FILE:
		// ack-style --ignore-file=FILTER:FILTERARGS option.
		// This is handled specially outside of the argp parser, since it interacts with the OPT_TYPE_SET/ADD/DEL mechanism.
		break;
	case OPT_INCLUDE:
	case OPT_EXCLUDE:
		// grep-style --include/exclude=GLOB.
		// This is handled specially outside of the argp parser, since it interacts with the OPT_TYPE_SET/ADD/DEL mechanism.
		break;
	case 'r':
	case 'R':
		arguments->m_recurse = true;
		break;
	case 'n':
		arguments->m_recurse = false;
		break;
	case 'k':
		// No argument variable because currently we only support searching known types.
		break;
	case OPT_TYPE:
		if(std::strncmp("no", arg, 2) == 0)
		{
			// The first two chars are "no", this is a "--type=noTYPE" option.
			if(arguments->m_type_manager.notype(arg+2) == false)
			{
				argp_failure(state, STATUS_EX_USAGE, 0, "Unknown type \'%s\'.", arg+2);
			}
		}
		else
		{
			// This is a "--type=TYPE" option.
			if(arguments->m_type_manager.type(arg) == false)
			{
				argp_failure(state, STATUS_EX_USAGE, 0, "Unknown type \'%s\'.", arg);
			}
		}
		break;
	case OPT_TYPE_SET:
	case OPT_TYPE_ADD:
	case OPT_TYPE_DEL:
		// These options are all handled specially outside of the argp parser.
		break;
	case OPT_NOENV:
		// The --noenv option is handled specially outside of the argp parser.
		break;
	case OPT_HELP_TYPES:
		// Consume the rest of the options/args.
		state->next = state->argc;
		arguments->PrintHelpTypes();
		break;
	case 'j':
		if(atoi(arg) < 1)
		{
			// Specified 0 or negative jobs.
			argp_failure(state, STATUS_EX_USAGE, 0, "jobs must be >= 1");
		}
		else
		{
			arguments->m_jobs = atoi(arg);
		}
		break;
	case OPT_PERF_DIRJOBS:
		if(atoi(arg) < 1)
		{
			// Specified 0 or negative jobs.
			argp_failure(state, STATUS_EX_USAGE, 0, "jobs must be >= 1");
		}
		else
		{
			arguments->m_dirjobs = atoi(arg);
		}
		break;
	case OPT_COLOR:
		arguments->m_color = true;
		arguments->m_nocolor = false;
		break;
	case OPT_NOCOLOR:
		arguments->m_color = false;
		arguments->m_nocolor = true;
		break;
	case OPT_TEST_LOG_ALL:
		INFO::Enable(true);
		break;
	case OPT_TEST_NOENV_USER:
		// The --test-noenv-user option is handled specially outside of the argp parser.
		break;
	case OPT_TEST_USE_MMAP:
		arguments->m_use_mmap = true;
		break;
	case ARGP_KEY_ARG:
		if(state->arg_num == 0)
		{
			// First arg is the pattern.
			arguments->m_pattern = arg;
		}
		else
		{
			// Remainder are optional file paths.
			arguments->m_paths.push_back(arg);
		}
		break;
	case ARGP_KEY_END:
		if(state->arg_num < 1)
		{
			// Not enough args.
			argp_usage(state);
		}
		break;
	case OPT_BRACKET_NO_STANDIN:
		// "Bracketed-No" long option stand-ins (i.e. "--[no]whatever") should never actually
		// be given on the command line.
		argp_error(state, "unrecognized option \'%s\'", state->argv[state->next-1]);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}


ArgParse::ArgParse(TypeManager &type_manager)
	: m_type_manager(type_manager)
{
}

ArgParse::~ArgParse()
{
}

/**
 * Why?  There's a legitimate reason, honestly:
 * 1. argp's argp_parse() is expecting an argv array of char*'s.
 * 2. For handling the rc files, we're creating vectors of new[]'ed char*'s.
 * 3. main's argv of course is an array of char*'s of unknown allocation.
 * 4. We combine #2 and #3 into one big vector<char*>, pass it to argp_parse().
 * 5. Then when we're all done, we have to delete[] all these strings, and we don't want to have to separately track and free() the ones from
 *    main()'s argv that we strdup()'ed.
 * @param orig  The string to be duplicated.
 * @return  A new[]-allocated copy of #orig.
 */
static char * cpp_strdup(const char *orig)
{
	char *retval = new char[strlen(orig)+1];
	std::strcpy(retval, orig);
	return retval;
}

void ArgParse::Parse(int argc, char **argv)
{
	std::vector<char*> user_argv, project_argv, combined_argv;

	// Check the command line for the --noenv option.
	// Note that we have to handle 'ucg -- --noenv' properly, hence the one_past_end_or_double_dash search first.
	auto one_past_end_or_double_dash = std::find_if(argv, argv+argc, [](char *s){ return std::strcmp(s, "--") == 0; });
	auto noenv = std::count_if(argv, one_past_end_or_double_dash, [](char *s){ return std::strcmp(s, "--noenv") == 0; });

	// Check for some test options which only make sense on the command line.
	auto noenv_user = std::count_if(argv, one_past_end_or_double_dash, [](char *s){ return std::strcmp(s, "--test-noenv-user") == 0; });
	if(noenv_user != 0)
	{
		m_test_noenv_user = true;
	}

	if(noenv == 0)
	{
		// Read all the config files.
		FindAndParseConfigFiles(nullptr, &user_argv, &project_argv);
	}

	// Combine all the argvs into one.
	combined_argv.push_back(cpp_strdup(argv[0]));
	if(noenv == 0)
	{
		combined_argv.insert(combined_argv.end(), user_argv.begin(), user_argv.end());
		combined_argv.insert(combined_argv.end(), project_argv.begin(), project_argv.end());
	}
	for(int i=1; i<argc; ++i)
	{
		combined_argv.push_back(cpp_strdup(argv[i]));
	}

	// We have to handle User Defined Types and --TYPEs ourselves, before finally calling argp_parse().  argp doesn't
	// support dynamically added options, which we need for the --TYPEs that will be created by the User Defined Type
	// mechanism.
	HandleTYPELogic(&combined_argv);

	// Parse the combined list of arguments.
	argp_parse(&argp, combined_argv.size(), combined_argv.data(), 0, 0, this);

	//// Now set up some defaults which we can only determine after all arg parsing is complete.

	// Number of jobs.
	if(m_jobs == 0)
	{
		// Number of jobs wasn't specified on command line.  Default to the number of cores the std lib says we have.
		m_jobs = std::thread::hardware_concurrency();

		if(m_jobs == 0)
		{
			// std::thread::hardware_concurrency() is broken.  Default to one thread.
			m_jobs = 1;
		}
	}

	// Number of directory scanning jobs.
	if(m_dirjobs == 0)
	{
		// Wasn't specified on command line.  Use the default.
		m_dirjobs = f_default_dirjobs;
	}

	// Search files/directories.
	if(m_paths.empty())
	{
		// Default to current directory.
		m_paths.push_back(".");
	}

	// Is smart-case enabled, and will we otherwise not be ignoring case?
	if(m_smart_case && !m_ignore_case)
	{
		// Is PATTERN all lower-case?

		// Use a copy of the current global locale.  Since we never call std::locale::global() to set it,
		// this should end up defaulting to the "classic" (i.e. "C") locale.
		/// @todo This really should be the environment's default locale (loc("")).  Cygwin doesn't support this
		/// at the moment (loc("") throws), and the rest of ucg isn't localized anyway, so this should work for now.
		std::locale loc;
		// Look for the first uppercase char in PATTERN.
		if(std::find_if(m_pattern.cbegin(), m_pattern.cend(), [&loc](decltype(m_pattern)::value_type c){ return std::isupper(c, loc); }) == m_pattern.cend())
		{
			// Didn't find one, so match without regard to case.
			m_ignore_case = true;
		}
	}

	// Free the strings we cpp_strdup()'ed
	for(char *c : combined_argv)
	{
		delete [] c;
	}
}

void ArgParse::PrintVersionText(FILE* stream)
{
	// Print the version string and copyright notice.
	std::fputs(argp_program_version, stream);

	// In addition, we want to print the compiler/version we were built with, the libpcre version and some other info on it,
	// and any source control version info we can get.

	std::fprintf(stream, "\n\nBuild info\n");

	//
	// Provenance info.
	//
	std::fprintf(stream, "\nRepo version: %s\n", g_git_describe);

	//
	// Compiler info
	//
	std::fprintf(stream, "\nCompiler info:\n");
	std::fprintf(stream, " Name ($(CXX)): %s\n", g_cxx);
	std::fprintf(stream, " Version string: \"%s\"\n", g_cxx_version_str);

	//
	// Runtime info
	//
	std::fprintf(stream, "\nISA extensions in use:\n");
	std::fprintf(stream, " sse4.2: %s\n", sys_has_sse4_2() ? "yes" : "no");
	std::fprintf(stream, " popcnt: %s\n", sys_has_popcnt() ? "yes" : "no");

	//
	// libpcre info
	//
	{
		std::fprintf(stream, "\nlibpcre info:\n");
#ifndef HAVE_LIBPCRE
		std::fprintf(stream, " Not linked against libpcre.\n");
#else
		std::fprintf(stream, " Version: %s\n", pcre_version());
		std::string s;
		int is_jit;
		s = "no";
		if(pcre_config(PCRE_CONFIG_JIT, &is_jit) == 0)
		{
			s = is_jit ? "yes" : "no";
		}
		std::fprintf(stream, " JIT support built in?: %s\n", s.c_str());
		const char *jittarget = "none";
		if(pcre_config(PCRE_CONFIG_JITTARGET, &jittarget) == 0)
		{
			if(jittarget == NULL)
			{
				jittarget = "none";
			}
		}
		std::fprintf(stream, " JIT target architecture: %s\n", jittarget);
		int nl;
		s = "unknown";
		std::map<int, std::string> newline_desc { {10, "LF"}, {13, "CR"}, {3338, "CRLF"}, {-2, "ANYCRLF"}, {-1, "ANY"},
												{21, "LF(EBCDIC)"}, {37, "LF(37)(EBCDIC)"}, {3349, "CRLF(EBCDIC)"}, {3365, "CRLF(37)(EBCDIC)"}};
		if(pcre_config(PCRE_CONFIG_NEWLINE, &nl) == 0)
		{
			auto nl_type_name = newline_desc.find(nl);
			if(nl_type_name != newline_desc.end())
			{
				s = nl_type_name->second;
			}
		}
		std::fprintf(stream, " Newline style: %s\n", s.c_str());
#endif
	}

	//
	// libpcre2 info
	//
	{
		std::fprintf(stream, "\nlibpcre2-8 info:\n");
#ifndef HAVE_LIBPCRE2
		std::fprintf(stream, " Not linked against libpcre2-8.\n");
#else
		char buffer[13];
		pcre2_config(PCRE2_CONFIG_VERSION, buffer);
		std::fprintf(stream, " Version: %s\n", buffer);
		std::string s;
		uint32_t is_jit;
		s = "no";
		if(pcre2_config(PCRE2_CONFIG_JIT, &is_jit) == 0)
		{
			s = is_jit ? "yes" : "no";
		}
		std::fprintf(stream, " JIT support built in?: %s\n", s.c_str());
		char jittarget[48];
		if(pcre2_config(PCRE2_CONFIG_JITTARGET, jittarget) == PCRE2_ERROR_BADOPTION)
		{
			std::strcpy(jittarget, "none");
		}
		std::fprintf(stream, " JIT target architecture: %s\n", jittarget);
		uint32_t nl;
		s = "unknown";
		std::map<uint32_t, std::string> newline_desc {
			{PCRE2_NEWLINE_LF, "LF"},
			{PCRE2_NEWLINE_CR, "CR"},
			{PCRE2_NEWLINE_CRLF, "CRLF"},
			{PCRE2_NEWLINE_ANYCRLF, "ANYCRLF"},
			{PCRE2_NEWLINE_ANY, "ANY"}
		};
		if(pcre2_config(PCRE2_CONFIG_NEWLINE, &nl) == 0)
		{
			auto nl_type_name = newline_desc.find(nl);
			if(nl_type_name != newline_desc.end())
			{
				s = nl_type_name->second;
			}
		}
		std::fprintf(stream, " Newline style: %s\n", s.c_str());
#endif
	}
}

void ArgParse::PrintHelpTypes() const
{
	std::cout << "ucg recognizes the following file types:" << std::endl;
	std::cout << std::endl;
	m_type_manager.PrintTypesForHelp(std::cout);
	std::cout << std::endl;
}

void ArgParse::FindAndParseConfigFiles(std::vector<char*> */*global_argv*/, std::vector<char*> *user_argv, std::vector<char*> *project_argv)
{
	// Find and parse the global config file.
	/// @todo
	/// @note global_argv commented out above to avoid unused parameter warning.

	// Check if we're ignoring $HOME/.ucgrc for test purposes.
	if(!m_test_noenv_user)
	{
		// Parse the user's config file.
		std::string homedir = GetUserHomeDir();
		if(!homedir.empty())
		{
			// See if we can open the user's .ucgrc file.
			homedir += "/.ucgrc";
			try
			{
				File home_file(homedir);

				if(home_file.size() == 0)
				{
					LOG(INFO) << "Config file \"" << homedir << "\" is zero-length.";
				}
				else
				{
					auto vec_argv = ConvertRCFileToArgv(home_file);

					user_argv->insert(user_argv->end(), vec_argv.cbegin(), vec_argv.cend());
				}
			}
			catch(const FileException &e)
			{
				WARN() << "During search for ~/.ucgrc: " << e.what();
			}
			catch(const std::system_error &e)
			{
				if(e.code() != std::errc::no_such_file_or_directory)
				{
					WARN() << "Couldn't open config file \"" << homedir << "\", error " << e.code() << " - " << e.code().message();
				}
				// Otherwise, the file just doesn't exist.
			}
		}
	}

	// Find and parse the project config file.
	auto proj_rc_filename = GetProjectRCFilename();
	if(!proj_rc_filename.empty())
	{
		// We found it, see if we can open it.
		try
		{
			File proj_rc_file(proj_rc_filename);

			if(proj_rc_file.size() == 0)
			{
				LOG(INFO) << "Config file \"" << proj_rc_filename << "\" is zero-length.";
			}
			else
			{
				LOG(INFO) << "Parsing config file \"" << proj_rc_filename << "\".";

				auto vec_argv = ConvertRCFileToArgv(proj_rc_file);

				project_argv->insert(project_argv->end(), vec_argv.cbegin(), vec_argv.cend());
			}
		}
		catch(const FileException &e)
		{
			WARN() << "During search for project .ucgrc file: " << e.what();
		}
		catch(const std::system_error &e)
		{
			if(e.code() != std::errc::no_such_file_or_directory)
			{
				WARN() << "Couldn't open config file \"" << proj_rc_filename << "\", error " << e.code() << " - " << e.code().message();
			}
			// Otherwise, the file just doesn't exist.
		}
	}
}

std::string ArgParse::GetUserHomeDir() const
{
	std::string retval;

	// First try the $HOME environment variable.
	const char * home_path = getenv("HOME");

	if(home_path == nullptr)
	{
		// No HOME variable, check the user database.
		home_path = getpwuid(getuid())->pw_dir;
	}

	if(home_path != nullptr)
	{
		// Found user's HOME dir.
		retval = home_path;
	}

	return retval;
}

std::string ArgParse::GetProjectRCFilename() const
{
	// Walk up the directory hierarchy from the cwd until we:
	// 1. Get to the user's $HOME dir, in which case we don't return an rc filename even if it exists.
	// 2. Find an rc file, which we'll then return the name of.
	// 3. Can't go up the hierarchy any more (i.e. we hit root).
	/// @todo We might want to reconsider if we want to start at cwd or rather at whatever
	///       paths may have been specified on the command line.  cwd is what Ack is documented
	///       to do, and is easier.

	std::string retval;

	// Get a file descriptor to the user's home dir, if there is one.
	std::string homedirname = GetUserHomeDir();
	int home_fd = -1;
	if(!homedirname.empty())
	{
		home_fd = open(homedirname.c_str(), O_RDONLY);
		/// @todo Should probably check for is-a-dir here.
	}

	// Get the current working directory's absolute pathname.
	/// @note GRVS - get_current_dir_name() under Cygwin will currently return a DOS path if this is started
	///              under the Eclipse gdb.  This mostly doesn't cause problems, except for terminating the loop
	///              (see below).
#ifdef HAVE_GET_CURRENT_DIR_NAME
	char *original_cwd = get_current_dir_name();
#else
	char *original_cwd = getcwd(NULL, 0);
#endif


	LOG(INFO) << "cwd = \'" << original_cwd << "\'";

	std::string current_cwd(original_cwd == nullptr ? "" : original_cwd);
	while(!current_cwd.empty() && current_cwd[0] != '.')
	{
		// If we were able to get a file descriptor to $HOME above...
		if(home_fd != -1)
		{
			// ...check if this dir is the user's $HOME dir.
			int cwd_fd = open(current_cwd.c_str(), O_RDONLY);
			if(cwd_fd != -1)
			{
				/// @todo Should probably check for is-a-dir here.
				if(is_same_file(cwd_fd, home_fd))
				{
					// We've hit the user's home directory without finding a config file.
					close(cwd_fd);
					break;
				}
				close(cwd_fd);
			}
			// else couldn't open the current cwd, so we can't check if it's the same directory as home_fd.
		}

		// Try to open the config file.
		auto test_rc_filename = current_cwd;
		if(*test_rc_filename.rbegin() != '/')
		{
			test_rc_filename += "/";
		}
		test_rc_filename += ".ucgrc";
		LOG(INFO) << "Checking for rc file \'" << test_rc_filename << "\'";
		int rc_file = open(test_rc_filename.c_str(), O_RDONLY);
		if(rc_file != -1)
		{
			// Found it.  Return its name.
			LOG(INFO) << "Found rc file \'" << test_rc_filename << "\'";
			retval = test_rc_filename;
			close(rc_file);
			break;
		}

		/// @note GRVS - get_current_dir_name() under Cygwin will currently return a DOS path if this is started
		///              under the Eclipse gdb.  This mostly doesn't cause problems, except for terminating the loop.
		///              The clause below after the || handles this.
		if((current_cwd.length() == 1) || (current_cwd.length() <= 4 && current_cwd[1] == ':'))
		{
			// We've hit the root and didn't find a config file.
			break;
		}

		// Go up one directory.
		current_cwd = portable::dirname(current_cwd);
	}

	// Free the cwd string.
	free(original_cwd);

	if(home_fd != -1)
	{
		// Close the homedir we opened above.
		close(home_fd);
	}

	return retval;
}

std::vector<char*> ArgParse::ConvertRCFileToArgv(const File& f)
{
	// The RC files we support are text files with one command-line parameter per line.
	// Comments are supported, as lines beginning with '#'.
	const char *pos = f.data();
	const char *end = f.data() + f.size();
	std::vector<char*> retval;

	while(pos != end)
	{
		// Eat all leading whitespace, including newlines.
		pos = std::find_if_not(pos, end, [](char c){ return std::isspace(c); });

		if(pos != end)
		{
			// We found some non-whitespace text.
			if(*pos == '#')
			{
				// It's a comment, skip it.
				pos = std::find_if(pos, end, [](char c){ return c == '\n'; });
				// pos now points at the '\n', but the eat-ws line above will consume it.
			}
			else
			{
				// It's something that is intended to be a command-line param.
				// Find the end of the line.  Everything between [pos, param_end) is the parameter.
				const char *param_end = std::find_if(pos, end, [](char c){ return (c=='\r') || (c=='\n'); });

				char *param = new char[(param_end - pos) + 1];
				std::copy(pos, param_end, param);
				param[param_end - pos] = '\0';

				// Check if it's not an option.
				if(std::strcmp(param, "--") == 0)
				{
					// Double-dash is not allowed in an rc file.
					throw ArgParseException("Double-dash \"" + std::string(param) + "\" is not allowed in rc file \"" + f.name() + "\".");
				}
				else if(param[0] != '-')
				{
					throw ArgParseException("Non-option argument \"" + std::string(param) + "\" is not allowed in rc file \"" + f.name() + "\".");
				}

				retval.push_back(param);

				pos = param_end;
			}
		}
	}

	return retval;
}

void ArgParse::HandleTYPELogic(std::vector<char*> *v)
{
	for(auto arg = v->begin(); arg != v->end(); ++arg)
	{
		try // TypeManager might throw on malformed file type filter strings.
		{
			auto arglen = std::strlen(*arg);
			if((arglen < 3) || (std::strncmp("--", *arg, 2) != 0))
			{
				// We only care about double-dash options here.
				continue;
			}

			if(std::strcmp("--", *arg) == 0)
			{
				// This is a "--", ignore all further command-line params.
				break;
			}

			std::string argtxt(*arg+2);

			// Is this a type specification of the form "--TYPE"?
			auto type_name_list = m_type_manager.GetMatchingTypenameList(argtxt);
			if(type_name_list.size() == 1)
			{
				// Yes, replace it with something digestible by argp: --type=TYPE.
				std::string new_param("--type=" + type_name_list[0]);
				delete [] *arg;
				*arg = cpp_strdup(new_param.c_str());
			}
			else if(type_name_list.size() > 1)
			{
				// Ambiguous parameter.
				// Try to match argp's output in this case as closely as we can.
				// argp's output in such a case looks like this:
				//   $ ./ucg --i 'endif' ../
				//   ./ucg: option '--i' is ambiguous; possibilities: '--ignore-case' '--ignore' '--include' '--ignore-file' '--ignore-directory' '--ignore-dir'
				//   Try `ucg --help' or `ucg --usage' for more information.
				std::string possibilities = "'--" + join(type_name_list, "' '--") + "'";
				throw ArgParseException("option '--" + argtxt + "' is ambiguous; possibilities: " + possibilities);
			}

			// Is this a type specification of the form '--noTYPE'?
			else if(argtxt.compare(0, 2, "no") == 0)
			{
				auto possible_type_name = argtxt.substr(2);
				auto type_name_list = m_type_manager.GetMatchingTypenameList(possible_type_name);
				if(type_name_list.size() == 1)
				{
					// Yes, replace it with something digestible by argp: --type=noTYPE.
					std::string new_param("--type=no" + type_name_list[0]);
					delete [] *arg;
					*arg = cpp_strdup(new_param.c_str());
				}
				else if(type_name_list.size() > 1)
				{
					// Ambiguous parameter.
					std::string possibilities = "'--no" + join(type_name_list, "' '--no") + "'";
					throw ArgParseException("option '--" + argtxt + "' is ambiguous; possibilities: " + possibilities);
				}
			}

			// Otherwise, check if it's one of the file type definition parameters.
			else
			{
				// All file type params take the form "--CMD=CMDPARAMS", so split on the '='.
				auto on_equals_split = split(argtxt, '=');

				if(on_equals_split.size() != 2)
				{
					// No '=', not something we care about here.
					continue;
				}

				// Is this a type-add?
				if(on_equals_split[0] == "type-add")
				{
					// Yes, have type manager add the params.
					m_type_manager.TypeAddFromFilterSpecString(false, on_equals_split[1]);
				}

				// Is this a type-set?
				else if(on_equals_split[0] == "type-set")
				{
					// Yes, delete any existing file type by the given name and do a type-add.
					m_type_manager.TypeAddFromFilterSpecString(true, on_equals_split[1]);
				}

				// Is this a type-del?
				else if(on_equals_split[0] == "type-del")
				{
					// Tell the TypeManager to delete the type.
					/// @note ack reports no error if the file type to be deleted doesn't exist.
					/// We'll match that behavior here.
					m_type_manager.TypeDel(on_equals_split[1]);
				}

				// Is this an ignore-file?
				else if(on_equals_split[0] == "ignore-file")
				{
					// It's an ack-style "--ignore-file=FILTER:FILTERARGS".
					// Behaviorally, this is as if an unnamed type was set on the command line, and then
					// immediately --notype='ed.  So that's how we'll handle it.
					m_type_manager.TypeAddIgnoreFileFromFilterSpecString(on_equals_split[1]);
				}

				// Is this an include or exclude?
				else if(on_equals_split[0] == "exclude" || on_equals_split[0] == "ignore")
				{
					// This is a grep-style "--exclude=GLOB" or an ag-style "--ignore=GLOB".
					m_type_manager.TypeAddIgnoreFileFromFilterSpecString("globx:" + on_equals_split[1]);
				}
				else if(on_equals_split[0] == "include")
				{
					// This is a grep-style "--include=GLOB".
					m_type_manager.TypeAddIncludeGlobFromFilterSpecString("glob:" + on_equals_split[1]);
				}
			}
		}
		catch(const TypeManagerException &e)
		{
			throw ArgParseException(std::string(e.what()) + " while parsing option \'" + *arg + "\'");
		}
	}
}
