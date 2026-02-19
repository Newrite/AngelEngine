#include "scripteastlstring.h"
#include <assert.h> // assert()
#include <cctype>   // std::isspace
#include <cstdio>	// std::snprintf()
#include <cstring>  // std::memcpy, std::strlen
#ifndef __psp2__
	#include <locale.h> // setlocale()
#endif
#include <regex>

using namespace eastl;

// This macro is used to avoid warnings about unused variables.
// Usually where the variables are only used in debug mode.
#define UNUSED_VAR(x) (void)(x)

#ifdef AS_CAN_USE_CPP11
// The string factory doesn't need to keep a specific order in the
// cache, so the unordered_map is faster than the ordinary map
#include <EASTL/unordered_map.h>  // std::unordered_map
BEGIN_AS_NAMESPACE
typedef eastl::unordered_map<eastl::string, int> map_t;
END_AS_NAMESPACE
#else
#include <EASTL/map.h>      // std::map
BEGIN_AS_NAMESPACE
typedef map<string, int> map_t;
END_AS_NAMESPACE
#endif

BEGIN_AS_NAMESPACE
class CStdStringFactory : public asIStringFactory
{
public:
	CStdStringFactory() {}
	~CStdStringFactory() 
	{
		// The script engine must release each string 
		// constant that it has requested
		assert(stringCache.size() == 0);
	}

	const void *GetStringConstant(const char *data, asUINT length)
	{
		// The string factory might be modified from multiple 
		// threads, so it is necessary to use a mutex.
		asAcquireExclusiveLock();
		
		string str(data, length);
		map_t::iterator it = stringCache.find(str);
		if (it != stringCache.end())
			it->second++;
		else
			it = stringCache.insert(map_t::value_type(str, 1)).first;

		asReleaseExclusiveLock();
		
		return reinterpret_cast<const void*>(&it->first);
	}

	int  ReleaseStringConstant(const void *str)
	{
		if (str == 0)
			return asERROR;

		int ret = asSUCCESS;
		
		// The string factory might be modified from multiple 
		// threads, so it is necessary to use a mutex.
		asAcquireExclusiveLock();
		
		map_t::iterator it = stringCache.find(*reinterpret_cast<const string*>(str));
		if (it == stringCache.end())
			ret = asERROR;
		else
		{
			it->second--;
			if (it->second == 0)
				stringCache.erase(it);
		}
		
		asReleaseExclusiveLock();
		
		return ret;
	}

	int  GetRawStringData(const void *str, char *data, asUINT *length) const
	{
		if (str == 0)
			return asERROR;

		if (length)
			*length = (asUINT)reinterpret_cast<const string*>(str)->length();

		if (data)
			std::memcpy(data, reinterpret_cast<const string*>(str)->c_str(), reinterpret_cast<const string*>(str)->length());

		return asSUCCESS;
	}

	// THe access to the string cache is protected with the common mutex provided by AngelScript
	map_t stringCache;
};

static CStdStringFactory *stringFactory = 0;

// TODO: Make this public so the application can also use the string 
//       factory and share the string constants if so desired, or to
//       monitor the size of the string factory cache.
CStdStringFactory *GetStdStringFactorySingleton()
{
	if( stringFactory == 0 )
	{
		// Make sure no other thread is creating the string factory at the same time
		asAcquireExclusiveLock();
		if (stringFactory == 0)
		{
			// The following instance will be destroyed by the global 
			// CStdStringFactoryCleaner instance upon application shutdown
			stringFactory = new CStdStringFactory();
		}
		asReleaseExclusiveLock();
	}
	return stringFactory;
}

class CStdStringFactoryCleaner
{
public:
	~CStdStringFactoryCleaner()
	{
		if (stringFactory)
		{
			// Only delete the string factory if the stringCache is empty
			// If it is not empty, it means that someone might still attempt
			// to release string constants, so if we delete the string factory
			// the application might crash. Not deleting the cache would
			// lead to a memory leak, but since this is only happens when the
			// application is shutting down anyway, it is not important.
			if (stringFactory->stringCache.empty())
			{
				delete stringFactory;
				stringFactory = 0;
			}
		}
	}
};

static CStdStringFactoryCleaner cleaner;


static void ConstructString(string *thisPointer)
{
	new(thisPointer) string();
}

static void CopyConstructString(const string &other, string *thisPointer)
{
	new(thisPointer) string(other);
}

static void DestructString(string *thisPointer)
{
	thisPointer->~string();
}

static string &AddAssignStringToString(const string &str, string &dest)
{
	// We don't register the method directly because some compilers
	// and standard libraries inline the definition, resulting in the
	// linker being unable to find the declaration.
	// Example: CLang/LLVM with XCode 4.3 on OSX 10.7
	dest += str;
	return dest;
}

// bool string::isEmpty()
// bool string::empty() // if AS_USE_STLNAMES == 1
static bool StringIsEmpty(const string &str)
{
	// We don't register the method directly because some compilers
	// and standard libraries inline the definition, resulting in the
	// linker being unable to find the declaration
	// Example: CLang/LLVM with XCode 4.3 on OSX 10.7
	return str.empty();
}

#if AS_NO_IMPL_OPS_WITH_STRING_AND_PRIMITIVE == 0
static string &AssignUInt64ToString(asQWORD i, string &dest)
{
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)i);
	dest = buf;
	return dest;
}

static string &AddAssignUInt64ToString(asQWORD i, string &dest)
{
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)i);
	dest += buf;
	return dest;
}

static string AddStringUInt64(const string &str, asQWORD i)
{
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)i);
	return str + buf;
}

static string AddInt64String(asINT64 i, const string &str)
{
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%lld", (long long)i);
	return string(buf) + str;
}

static string &AssignInt64ToString(asINT64 i, string &dest)
{
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%lld", (long long)i);
	dest = buf;
	return dest;
}

static string &AddAssignInt64ToString(asINT64 i, string &dest)
{
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%lld", (long long)i);
	dest += buf;
	return dest;
}

static string AddStringInt64(const string &str, asINT64 i)
{
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%lld", (long long)i);
	return str + buf;
}

static string AddUInt64String(asQWORD i, const string &str)
{
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)i);
	return string(buf) + str;
}

static string &AssignDoubleToString(double f, string &dest)
{
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", f);
	dest = buf;
	return dest;
}

static string &AddAssignDoubleToString(double f, string &dest)
{
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", f);
	dest += buf;
	return dest;
}

static string &AssignFloatToString(float f, string &dest)
{
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", (double)f);
	dest = buf;
	return dest;
}

static string &AddAssignFloatToString(float f, string &dest)
{
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", (double)f);
	dest += buf;
	return dest;
}

static string &AssignBoolToString(bool b, string &dest)
{
	dest = (b ? "true" : "false");
	return dest;
}

static string &AddAssignBoolToString(bool b, string &dest)
{
	dest += (b ? "true" : "false");
	return dest;
}

static string AddStringDouble(const string &str, double f)
{
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", f);
	return str + buf;
}

static string AddDoubleString(double f, const string &str)
{
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", f);
	return string(buf) + str;
}

static string AddStringFloat(const string &str, float f)
{
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", (double)f);
	return str + buf;
}

static string AddFloatString(float f, const string &str)
{
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", (double)f);
	return string(buf) + str;
}

static string AddStringBool(const string &str, bool b)
{
	return str + (b ? "true" : "false");
}

static string AddBoolString(bool b, const string &str)
{
	return string(b ? "true" : "false") + str;
}
#endif

static char *StringCharAt(unsigned int i, string &str)
{
	if( i >= str.size() )
	{
		// Set a script exception
		asIScriptContext *ctx = asGetActiveContext();
		ctx->SetException("Out of range");

		// Return a null pointer
		return 0;
	}

	return &str[i];
}

// AngelScript signature:
// int string::opCmp(const string &in) const
static int StringCmp(const string &a, const string &b)
{
	int cmp = 0;
	if( a < b ) cmp = -1;
	else if( a > b ) cmp = 1;
	return cmp;
}

// This function returns the index of the first position where the substring
// exists in the input string. If the substring doesn't exist in the input
// string -1 is returned.
//
// AngelScript signature:
// int string::findFirst(const string &in sub, uint start = 0) const
static int StringFindFirst(const string &sub, asUINT start, const string &str)
{
	// We don't register the method directly because the argument types change between 32bit and 64bit platforms
	return (int)str.find(sub, (size_t)(start < 0 ? string::npos : start));
}

// This function returns the index of the first position that matches the regular expression
//
// AngelScript signature:
// int string::regexFind(const string &in regex, uint start = 0, uint &out lengthOfMatch = void)
static int StringRegexFind(const string& rex, asUINT start, asUINT& outLengthOfMatch, const string& str)
{
	if (start >= str.length())
	{
		outLengthOfMatch = 0;
		return -1;
	}

	// TODO: If possible add support for matching utf8 characters
	// However on with MSVC it doesn't seem that std::regex works with utf8
	// This works with MSVC, but I don't want to have to convert the string to UTF-16 first because the position and length will not work
	// https://www.regular-expressions.info/stdregex.html
	// 
	//  std::wregex pattern(L"[[:alpha:]]+");
	//  bool result = std::regex_match(std::wstring(L"abcdfg"), pattern);
	//
	// The solution from stack overflow doesn't work with MSVC
	// https://stackoverflow.com/questions/11254232/do-c11-regular-expressions-work-with-utf-8-strings
	// 
	//  std::locale old;
	//  std::locale::global(std::locale("en_US.UTF-8"));
	//  std::regex pattern("[[:alpha:]]+", std::regex_constants::extended);
	//  bool result = std::regex_match(std::string(u8"abcdfg"), pattern);
	//
	// I've tried setting the manifest to use utf8 code page but it also doesn't work with MSVC
	// https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page

	std::regex pattern(rex.c_str(), std::regex_constants::ECMAScript | std::regex_constants::collate);
	std::cmatch match;
	bool result = std::regex_search(str.c_str() + start, str.c_str()+str.length(), match, pattern);

	if (!result)
	{
		outLengthOfMatch = 0;
		return -1;
	}

	outLengthOfMatch = (asUINT)match[0].length();
	return (int)match.prefix().length();
}

// This function returns the index of the first position where the one of the bytes in substring
// exists in the input string. If the characters in the substring doesn't exist in the input
// string -1 is returned.
//
// AngelScript signature:
// int string::findFirstOf(const string &in sub, uint start = 0) const
static int StringFindFirstOf(const string &sub, asUINT start, const string &str)
{
	// We don't register the method directly because the argument types change between 32bit and 64bit platforms
	return (int)str.find_first_of(sub, (size_t)(start < 0 ? string::npos : start));
}

// This function returns the index of the last position where the one of the bytes in substring
// exists in the input string. If the characters in the substring doesn't exist in the input
// string -1 is returned.
//
// AngelScript signature:
// int string::findLastOf(const string &in sub, uint start = -1) const
static int StringFindLastOf(const string &sub, asUINT start, const string &str)
{
	// We don't register the method directly because the argument types change between 32bit and 64bit platforms
	return (int)str.find_last_of(sub, (size_t)(start < 0 ? string::npos : start));
}

// This function returns the index of the first position where a byte other than those in substring
// exists in the input string. If none is found -1 is returned.
//
// AngelScript signature:
// int string::findFirstNotOf(const string &in sub, uint start = 0) const
static int StringFindFirstNotOf(const string &sub, asUINT start, const string &str)
{
	// We don't register the method directly because the argument types change between 32bit and 64bit platforms
	return (int)str.find_first_not_of(sub, (size_t)(start < 0 ? string::npos : start));
}

// This function returns the index of the last position where a byte other than those in substring
// exists in the input string. If none is found -1 is returned.
//
// AngelScript signature:
// int string::findLastNotOf(const string &in sub, uint start = -1) const
static int StringFindLastNotOf(const string &sub, asUINT start, const string &str)
{
	// We don't register the method directly because the argument types change between 32bit and 64bit platforms
	return (int)str.find_last_not_of(sub, (size_t)(start < 0 ? string::npos : start));
}

// This function returns the index of the last position where the substring
// exists in the input string. If the substring doesn't exist in the input
// string -1 is returned.
//
// AngelScript signature:
// int string::findLast(const string &in sub, int start = -1) const
static int StringFindLast(const string &sub, int start, const string &str)
{
	// We don't register the method directly because the argument types change between 32bit and 64bit platforms
	return (int)str.rfind(sub, (size_t)(start < 0 ? string::npos : start));
}

// AngelScript signature:
// void string::insert(uint pos, const string &in other)
static void StringInsert(unsigned int pos, const string &other, string &str)
{
	// We don't register the method directly because the argument types change between 32bit and 64bit platforms
	str.insert(pos, other);
}

// AngelScript signature:
// void string::erase(uint pos, int count = -1)
static void StringErase(unsigned int pos, int count, string &str)
{
	// We don't register the method directly because the argument types change between 32bit and 64bit platforms
	str.erase(pos, (size_t)(count < 0 ? string::npos : count));
}


// AngelScript signature:
// uint string::length() const
static asUINT StringLength(const string &str)
{
	// We don't register the method directly because the return type changes between 32bit and 64bit platforms
	return (asUINT)str.length();
}


// AngelScript signature:
// void string::resize(uint l)
static void StringResize(asUINT l, string &str)
{
	// We don't register the method directly because the argument types change between 32bit and 64bit platforms
	str.resize(l);
}

// AngelScript signature:
// string formatInt(int64 val, const string &in options, uint width)
static string formatInt(asINT64 value, const string &options, asUINT width)
{
	bool leftJustify = options.find("l") != string::npos;
	bool padWithZero = options.find("0") != string::npos;
	bool alwaysSign  = options.find("+") != string::npos;
	bool spaceOnSign = options.find(" ") != string::npos;
	bool hexSmall    = options.find("h") != string::npos;
	bool hexLarge    = options.find("H") != string::npos;

	string fmt = "%";
	if( leftJustify ) fmt += "-";
	if( alwaysSign ) fmt += "+";
	if( spaceOnSign ) fmt += " ";
	if( padWithZero ) fmt += "0";

#ifdef _WIN32
	fmt += "*I64";
#else
#ifdef _LP64
	fmt += "*l";
#else
	fmt += "*ll";
#endif
#endif

	if( hexSmall ) fmt += "x";
	else if( hexLarge ) fmt += "X";
	else fmt += "d";

	string buf;
	buf.resize(width+30);
#if _MSC_VER >= 1400 && !defined(__S3E__)
	// MSVC 8.0 / 2005 or newer
	(void)sprintf_s(&buf[0], buf.size(), fmt.c_str(), width, value);
#else
	(void)std::snprintf(&buf[0], buf.size(), fmt.c_str(), width, value);
#endif
	buf.resize(std::strlen(&buf[0]));

	return buf;
}

// AngelScript signature:
// string formatUInt(uint64 val, const string &in options, uint width)
static string formatUInt(asQWORD value, const string &options, asUINT width)
{
	bool leftJustify = options.find("l") != string::npos;
	bool padWithZero = options.find("0") != string::npos;
	bool alwaysSign  = options.find("+") != string::npos;
	bool spaceOnSign = options.find(" ") != string::npos;
	bool hexSmall    = options.find("h") != string::npos;
	bool hexLarge    = options.find("H") != string::npos;

	string fmt = "%";
	if( leftJustify ) fmt += "-";
	if( alwaysSign ) fmt += "+";
	if( spaceOnSign ) fmt += " ";
	if( padWithZero ) fmt += "0";

#ifdef _WIN32
	fmt += "*I64";
#else
#ifdef _LP64
	fmt += "*l";
#else
	fmt += "*ll";
#endif
#endif

	if( hexSmall ) fmt += "x";
	else if( hexLarge ) fmt += "X";
	else fmt += "u";

	string buf;
	buf.resize(width+30);
#if _MSC_VER >= 1400 && !defined(__S3E__)
	// MSVC 8.0 / 2005 or newer
	(void)sprintf_s(&buf[0], buf.size(), fmt.c_str(), width, value);
#else
	(void)std::snprintf(&buf[0], buf.size(), fmt.c_str(), width, value);
#endif
	buf.resize(std::strlen(&buf[0]));

	return buf;
}

// AngelScript signature:
// string formatFloat(double val, const string &in options, uint width, uint precision)
static string formatFloat(double value, const string &options, asUINT width, asUINT precision)
{
	bool leftJustify = options.find("l") != string::npos;
	bool padWithZero = options.find("0") != string::npos;
	bool alwaysSign  = options.find("+") != string::npos;
	bool spaceOnSign = options.find(" ") != string::npos;
	bool expSmall    = options.find("e") != string::npos;
	bool expLarge    = options.find("E") != string::npos;

	string fmt = "%";
	if( leftJustify ) fmt += "-";
	if( alwaysSign ) fmt += "+";
	if( spaceOnSign ) fmt += " ";
	if( padWithZero ) fmt += "0";

	fmt += "*.*";

	if( expSmall ) fmt += "e";
	else if( expLarge ) fmt += "E";
	else fmt += "f";

	string buf;
	buf.resize(width+precision+50);
#if _MSC_VER >= 1400 && !defined(__S3E__)
	// MSVC 8.0 / 2005 or newer
	(void)sprintf_s(&buf[0], buf.size(), fmt.c_str(), width, precision, value);
#else
	(void)std::snprintf(&buf[0], buf.size(), fmt.c_str(), width, precision, value);
#endif
	buf.resize(std::strlen(&buf[0]));

	return buf;
}

// TODO: variadic: review
static void StringFormat(asIScriptGeneric* gen)
{
	const string& fmt = *(string*)gen->GetArgAddress(0);
	string result;

	asUINT defaultArgIdx = 1; // Skip the first argument which is the fmt
	for (asUINT i = 0; i < fmt.size(); ++i)
	{
		char ch = fmt[i];
		if (ch == '{')
		{
			if (i + 1 >= (asUINT)fmt.size())
			{
				asGetActiveContext()->SetException("Invalid format string");
				return;
			}

			if (fmt[i + 1] == '{')
			{
				i += 1;
				result += '{';
			}
			else
			{
				// TODO: Parse optional argument index to support for relocating argument
				// e.g. "{1} {0}".format("there", "hello") == "hello there"
				asUINT argIdx = defaultArgIdx++;
				if (argIdx >= (asUINT)gen->GetArgCount())
				{
					asGetActiveContext()->SetException("Index out of range");
					return;
				}
				int typeId = gen->GetArgTypeId(argIdx);
				void* ref = gen->GetArgAddress(argIdx);

				switch (typeId)
				{
				case asTYPEID_BOOL:
					result += *(bool*)ref ? "true" : "false";
					break;

#define AS_STRING_FORMAT_IMPL(tid, type) \
	case tid: result += to_string(*(type*)ref); break

					AS_STRING_FORMAT_IMPL(asTYPEID_INT8, int8_t);
					AS_STRING_FORMAT_IMPL(asTYPEID_INT16, int16_t);
					AS_STRING_FORMAT_IMPL(asTYPEID_INT32, int32_t);
					AS_STRING_FORMAT_IMPL(asTYPEID_INT64, int64_t);

					AS_STRING_FORMAT_IMPL(asTYPEID_UINT8, uint8_t);
					AS_STRING_FORMAT_IMPL(asTYPEID_UINT16, uint16_t);
					AS_STRING_FORMAT_IMPL(asTYPEID_UINT32, uint32_t);
					AS_STRING_FORMAT_IMPL(asTYPEID_UINT64, uint64_t);

					AS_STRING_FORMAT_IMPL(asTYPEID_FLOAT, float);
					AS_STRING_FORMAT_IMPL(asTYPEID_DOUBLE, double);

				default:
					if (typeId & ~asTYPEID_MASK_SEQNBR)
					{
						asIScriptEngine* engine = gen->GetEngine();
						int stringTypeId = engine->GetStringFactory();
						if (typeId == stringTypeId)
						{
							result += *(string*)ref;
						}
						else
						{
							// TODO: Better explanation
							asGetActiveContext()->SetException("Unformattable");
							return;
						}
					}
					else // enums
					{
						// TODO: Format enum name
						result += to_string(*(int*)ref);
					}
				}
			}
		}
		else if (ch == '}')
		{
			if (i + 1 < (asUINT)fmt.size() && fmt[i + 1] == '}')
			{
				i += 1;
				result += '}';
			}
		}
		else
		{
			// Ordinary character
			result += ch;
		}
	}

	new(gen->GetAddressOfReturnLocation()) string(std::move(result));
}

// TODO: variadic: review
static void StringScan(asIScriptGeneric* gen)
{
	asIScriptEngine* engine = gen->GetEngine();

	string& str = *(string*)gen->GetArgObject(0);
	const char* p = str.c_str();
	asUINT scanned = 0;

	for (asUINT i = 1; i < (asUINT)gen->GetArgCount(); ++i)
	{
		while (*p && std::isspace((unsigned char)*p)) p++;
		if (!*p) break;

		int typeId = gen->GetArgTypeId(i);
		void* ref = gen->GetArgAddress(i);

		if (!(typeId & ~asTYPEID_MASK_SEQNBR))
		{
			char* end = 0;
			switch (typeId)
			{
			case asTYPEID_BOOL:
			{
				long val = std::strtol(p, &end, 10);
				if (end == p) goto end_scan;
				*(bool*)ref = (val != 0);
				p = end;
			}
			break;
			case asTYPEID_INT8:
			case asTYPEID_INT16:
			case asTYPEID_INT32:
			case asTYPEID_INT64:
			{
				long long val = std::strtoll(p, &end, 10);
				if (end == p) goto end_scan;

				if (typeId == asTYPEID_INT8) *(int8_t*)ref = (int8_t)val;
				else if (typeId == asTYPEID_INT16) *(int16_t*)ref = (int16_t)val;
				else if (typeId == asTYPEID_INT32) *(int32_t*)ref = (int32_t)val;
				else *(int64_t*)ref = (int64_t)val;

				p = end;
			}
			break;
			case asTYPEID_UINT8:
			case asTYPEID_UINT16:
			case asTYPEID_UINT32:
			case asTYPEID_UINT64:
			{
				unsigned long long val = std::strtoull(p, &end, 10);
				if (end == p) goto end_scan;

				if (typeId == asTYPEID_UINT8) *(uint8_t*)ref = (uint8_t)val;
				else if (typeId == asTYPEID_UINT16) *(uint16_t*)ref = (uint16_t)val;
				else if (typeId == asTYPEID_UINT32) *(uint32_t*)ref = (uint32_t)val;
				else *(uint64_t*)ref = (uint64_t)val;

				p = end;
			}
			break;
			case asTYPEID_FLOAT:
			case asTYPEID_DOUBLE:
			{
				double val = std::strtod(p, &end);
				if (end == p) goto end_scan;

				if (typeId == asTYPEID_FLOAT) *(float*)ref = (float)val;
				else *(double*)ref = (double)val;

				p = end;
			}
			break;
			default: // enum
			{
				long val = std::strtol(p, &end, 10);
				if (end == p) goto end_scan;
				*(int32_t*)ref = (int32_t)val;
				p = end;
			}
			break;
			}
		}
		else if (typeId == engine->GetStringFactory())
		{
			const char* start = p;
			while (*p && !std::isspace((unsigned char)*p)) p++;
			// p advanced at least 1 char because we skipped whitespace before
			
			string val(start, p - start);
			*(string*)ref = std::move(val);
		}
		else // Invalid type
		{
			goto end_scan;
		}

		++scanned;
	}

end_scan:
	gen->SetReturnDWord(scanned);
}

// AngelScript signature:
// int64 parseInt(const string &in val, uint base = 10, uint &out byteCount = 0)
static asINT64 parseInt(const string &val, asUINT base, asUINT *byteCount)
{
	// Only accept base 10 and 16
	if( base != 10 && base != 16 )
	{
		if( byteCount ) *byteCount = 0;
		return 0;
	}

	const char *end = &val[0];

	// Determine the sign
	bool sign = false;
	if( *end == '-' )
	{
		sign = true;
		end++;
	}
	else if( *end == '+' )
		end++;

	asINT64 res = 0;
	if( base == 10 )
	{
		while( *end >= '0' && *end <= '9' )
		{
			res *= 10;
			res += *end++ - '0';
		}
	}
	else if( base == 16 )
	{
		while( (*end >= '0' && *end <= '9') ||
		       (*end >= 'a' && *end <= 'f') ||
		       (*end >= 'A' && *end <= 'F') )
		{
			res *= 16;
			if( *end >= '0' && *end <= '9' )
				res += *end++ - '0';
			else if( *end >= 'a' && *end <= 'f' )
				res += *end++ - 'a' + 10;
			else if( *end >= 'A' && *end <= 'F' )
				res += *end++ - 'A' + 10;
		}
	}

	if( byteCount )
		*byteCount = asUINT(size_t(end - val.c_str()));

	if( sign )
		res = -res;

	return res;
}

// AngelScript signature:
// uint64 parseUInt(const string &in val, uint base = 10, uint &out byteCount = 0)
static asQWORD parseUInt(const string &val, asUINT base, asUINT *byteCount)
{
	// Only accept base 10 and 16
	if (base != 10 && base != 16)
	{
		if (byteCount) *byteCount = 0;
		return 0;
	}

	const char *end = &val[0];

	asQWORD res = 0;
	if (base == 10)
	{
		while (*end >= '0' && *end <= '9')
		{
			res *= 10;
			res += *end++ - '0';
		}
	}
	else if (base == 16)
	{
		while ((*end >= '0' && *end <= '9') ||
			(*end >= 'a' && *end <= 'f') ||
			(*end >= 'A' && *end <= 'F'))
		{
			res *= 16;
			if (*end >= '0' && *end <= '9')
				res += *end++ - '0';
			else if (*end >= 'a' && *end <= 'f')
				res += *end++ - 'a' + 10;
			else if (*end >= 'A' && *end <= 'F')
				res += *end++ - 'A' + 10;
		}
	}

	if (byteCount)
		*byteCount = asUINT(size_t(end - val.c_str()));

	return res;
}

// AngelScript signature:
// double parseFloat(const string &in val, uint &out byteCount = 0)
double parseFloat(const string &val, asUINT *byteCount)
{
	char *end;

	// Set the locale to C so that we are guaranteed to parse the float value correctly
#if defined(_WIN32)
	// WinCE doesn't have setlocale. Some quick testing on my current platform
	// still manages to parse the numbers such as "3.14" even if the decimal for the
	// locale is ",".
#if !defined(_WIN32_WCE)
	// On Windows setlocale is made threadsafe by turning on thread local setlocale
	// ref: https://learn.microsoft.com/en-us/cpp/parallel/multithreading-and-locales?view=msvc-170&redirectedfrom=MSDN
	int oldConfig = _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
	char* tmp = setlocale(LC_NUMERIC, 0);
	string orig = tmp ? tmp : "C";
	setlocale(LC_NUMERIC, "C");
#endif
#else
#if !defined(ANDROID) && !defined(__psp2__)
	// On Linux and other similar systems the threadsafe option is uselocale
	// ref: https://stackoverflow.com/questions/4057319/is-setlocale-thread-safe-function
	locale_t locale = newlocale(LC_NUMERIC_MASK, "C", NULL);
	locale_t orig_locale = uselocale(locale);
#endif
#endif

	double res = std::strtod(val.c_str(), &end);

	// Restore the original locale
#if defined(_WIN32)
#if !defined(_WIN32_WCE)
	setlocale(LC_NUMERIC, orig.c_str());
	_configthreadlocale(oldConfig);
#endif
#else
#if !defined(ANDROID) && !defined(__psp2__)
#endif
	uselocale(orig_locale);
	freelocale(locale);
#endif

	if( byteCount )
		*byteCount = asUINT(size_t(end - val.c_str()));

	return res;
}

// This function returns a string containing the substring of the input string
// determined by the starting index and count of characters.
//
// AngelScript signature:
// string string::substr(uint start = 0, int count = -1) const
static string StringSubString(asUINT start, int count, const string &str)
{
	// Check for out-of-bounds
	string ret;
	if( start < str.length() && count != 0 )
		ret = str.substr(start, (size_t)(count < 0 ? string::npos : count));

	return ret;
}

// String equality comparison.
// Returns true iff lhs is equal to rhs.
//
// For some reason gcc 4.7 has difficulties resolving the
// asFUNCTIONPR(operator==, (const string &, const string &)
// makro, so this wrapper was introduced as work around.
static bool StringEquals(const string& lhs, const string& rhs)
{
	return lhs == rhs;
}

void RegisterStdString_Native(asIScriptEngine *engine)
{
	int r = 0;
	UNUSED_VAR(r);

	// Register the string type
#if AS_CAN_USE_CPP11
	// With C++11 it is possible to use asGetTypeTraits to automatically determine the correct flags to use
	r = engine->RegisterObjectType("string", sizeof(string), asOBJ_VALUE | asGetTypeTraits<string>()); assert( r >= 0 );
#else
	r = engine->RegisterObjectType("string", sizeof(string), asOBJ_VALUE | asOBJ_APP_CLASS_CDAK); assert( r >= 0 );
#endif

	r = engine->RegisterStringFactory("string", GetStdStringFactorySingleton());

	// Register the object operator overloads
	r = engine->RegisterObjectBehaviour("string", asBEHAVE_CONSTRUCT,  "void f()",                    asFUNCTION(ConstructString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("string", asBEHAVE_CONSTRUCT,  "void f(const string &in)",    asFUNCTION(CopyConstructString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("string", asBEHAVE_DESTRUCT,   "void f()",                    asFUNCTION(DestructString),  asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAssign(const string &in)", asMETHODPR(string, operator =, (const string&), string&), asCALL_THISCALL); assert( r >= 0 );
	// Need to use a wrapper on Mac OS X 10.7/XCode 4.3 and CLang/LLVM, otherwise the linker fails
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(const string &in)", asFUNCTION(AddAssignStringToString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
//	r = engine->RegisterObjectMethod("string", "string &opAddAssign(const string &in)", asMETHODPR(string, operator+=, (const string&), string&), asCALL_THISCALL); assert( r >= 0 );

	// Need to use a wrapper for operator== otherwise gcc 4.7 fails to compile
	r = engine->RegisterObjectMethod("string", "bool opEquals(const string &in) const", asFUNCTIONPR(StringEquals, (const string &, const string &), bool), asCALL_CDECL_OBJFIRST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "int opCmp(const string &in) const", asFUNCTION(StringCmp), asCALL_CDECL_OBJFIRST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(const string &in) const", asFUNCTIONPR(operator +, (const string &, const string &), string), asCALL_CDECL_OBJFIRST); assert( r >= 0 );

	// The string length can be accessed through methods or through virtual property
	// TODO: Register as size() for consistency with other types
#if AS_USE_ACCESSORS != 1
	r = engine->RegisterObjectMethod("string", "uint length() const", asFUNCTION(StringLength), asCALL_CDECL_OBJLAST); assert( r >= 0 );
#endif
	r = engine->RegisterObjectMethod("string", "void resize(uint)", asFUNCTION(StringResize), asCALL_CDECL_OBJLAST); assert( r >= 0 );
#if AS_USE_STLNAMES != 1 && AS_USE_ACCESSORS == 1
	// Don't register these if STL names is used, as they conflict with the method size()
	r = engine->RegisterObjectMethod("string", "uint get_length() const property", asFUNCTION(StringLength), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "void set_length(uint) property", asFUNCTION(StringResize), asCALL_CDECL_OBJLAST); assert( r >= 0 );
#endif
	// Need to use a wrapper on Mac OS X 10.7/XCode 4.3 and CLang/LLVM, otherwise the linker fails
//	r = engine->RegisterObjectMethod("string", "bool isEmpty() const", asMETHOD(string, empty), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "bool isEmpty() const", asFUNCTION(StringIsEmpty), asCALL_CDECL_OBJLAST); assert( r >= 0 );

	// Register the index operator, both as a mutator and as an inspector
	// Note that we don't register the operator[] directly, as it doesn't do bounds checking
	r = engine->RegisterObjectMethod("string", "uint8 &opIndex(uint)", asFUNCTION(StringCharAt), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "const uint8 &opIndex(uint) const", asFUNCTION(StringCharAt), asCALL_CDECL_OBJLAST); assert( r >= 0 );

#if AS_NO_IMPL_OPS_WITH_STRING_AND_PRIMITIVE == 0
	// Automatic conversion from values
	r = engine->RegisterObjectMethod("string", "string &opAssign(double)", asFUNCTION(AssignDoubleToString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(double)", asFUNCTION(AddAssignDoubleToString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(double) const", asFUNCTION(AddStringDouble), asCALL_CDECL_OBJFIRST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd_r(double) const", asFUNCTION(AddDoubleString), asCALL_CDECL_OBJLAST); assert( r >= 0 );

	r = engine->RegisterObjectMethod("string", "string &opAssign(float)", asFUNCTION(AssignFloatToString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(float)", asFUNCTION(AddAssignFloatToString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(float) const", asFUNCTION(AddStringFloat), asCALL_CDECL_OBJFIRST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd_r(float) const", asFUNCTION(AddFloatString), asCALL_CDECL_OBJLAST); assert( r >= 0 );

	r = engine->RegisterObjectMethod("string", "string &opAssign(int64)", asFUNCTION(AssignInt64ToString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(int64)", asFUNCTION(AddAssignInt64ToString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(int64) const", asFUNCTION(AddStringInt64), asCALL_CDECL_OBJFIRST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd_r(int64) const", asFUNCTION(AddInt64String), asCALL_CDECL_OBJLAST); assert( r >= 0 );

	r = engine->RegisterObjectMethod("string", "string &opAssign(uint64)", asFUNCTION(AssignUInt64ToString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(uint64)", asFUNCTION(AddAssignUInt64ToString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(uint64) const", asFUNCTION(AddStringUInt64), asCALL_CDECL_OBJFIRST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd_r(uint64) const", asFUNCTION(AddUInt64String), asCALL_CDECL_OBJLAST); assert( r >= 0 );

	r = engine->RegisterObjectMethod("string", "string &opAssign(bool)", asFUNCTION(AssignBoolToString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(bool)", asFUNCTION(AddAssignBoolToString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(bool) const", asFUNCTION(AddStringBool), asCALL_CDECL_OBJFIRST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd_r(bool) const", asFUNCTION(AddBoolString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
#endif

	// Utilities
	r = engine->RegisterObjectMethod("string", "string substr(uint start = 0, int count = -1) const", asFUNCTION(StringSubString), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "int findFirst(const string &in, uint start = 0) const", asFUNCTION(StringFindFirst), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "int findFirstOf(const string &in, uint start = 0) const", asFUNCTION(StringFindFirstOf), asCALL_CDECL_OBJLAST); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "int findFirstNotOf(const string &in, uint start = 0) const", asFUNCTION(StringFindFirstNotOf), asCALL_CDECL_OBJLAST); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "int findLast(const string &in, int start = -1) const", asFUNCTION(StringFindLast), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "int findLastOf(const string &in, int start = -1) const", asFUNCTION(StringFindLastOf), asCALL_CDECL_OBJLAST); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "int findLastNotOf(const string &in, int start = -1) const", asFUNCTION(StringFindLastNotOf), asCALL_CDECL_OBJLAST); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "void insert(uint pos, const string &in other)", asFUNCTION(StringInsert), asCALL_CDECL_OBJLAST); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "void erase(uint pos, int count = -1)", asFUNCTION(StringErase), asCALL_CDECL_OBJLAST); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "int regexFind(const string  &in regex, uint start = 0, uint &out lengthOfMatch = void) const", asFUNCTION(StringRegexFind), asCALL_CDECL_OBJLAST); assert(r >= 0);

	r = engine->RegisterGlobalFunction("uint scan(const string&in str, ?&out ...)", asFUNCTION(StringScan), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterGlobalFunction("string format(const string&in fmt, const ?&in ...)", asFUNCTION(StringFormat), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterGlobalFunction("string formatInt(int64 val, const string &in options = \"\", uint width = 0)", asFUNCTION(formatInt), asCALL_CDECL); assert(r >= 0);
	r = engine->RegisterGlobalFunction("string formatUInt(uint64 val, const string &in options = \"\", uint width = 0)", asFUNCTION(formatUInt), asCALL_CDECL); assert(r >= 0);
	r = engine->RegisterGlobalFunction("string formatFloat(double val, const string &in options = \"\", uint width = 0, uint precision = 0)", asFUNCTION(formatFloat), asCALL_CDECL); assert(r >= 0);
	r = engine->RegisterGlobalFunction("int64 parseInt(const string &in, uint base = 10, uint &out byteCount = 0)", asFUNCTION(parseInt), asCALL_CDECL); assert(r >= 0);
	r = engine->RegisterGlobalFunction("uint64 parseUInt(const string &in, uint base = 10, uint &out byteCount = 0)", asFUNCTION(parseUInt), asCALL_CDECL); assert(r >= 0);
	r = engine->RegisterGlobalFunction("double parseFloat(const string &in, uint &out byteCount = 0)", asFUNCTION(parseFloat), asCALL_CDECL); assert(r >= 0);

#if AS_USE_STLNAMES == 1
	// Same as length
	r = engine->RegisterObjectMethod("string", "uint size() const", asFUNCTION(StringLength), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	// Same as isEmpty
	r = engine->RegisterObjectMethod("string", "bool empty() const", asFUNCTION(StringIsEmpty), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	// Same as findFirst
	r = engine->RegisterObjectMethod("string", "int find(const string &in, uint start = 0) const", asFUNCTION(StringFindFirst), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	// Same as findLast
	r = engine->RegisterObjectMethod("string", "int rfind(const string &in, int start = -1) const", asFUNCTION(StringFindLast), asCALL_CDECL_OBJLAST); assert( r >= 0 );
#endif

	// TODO: Implement the following
	// findAndReplace - replaces a text found in the string
	// replaceRange - replaces a range of bytes in the string
	// multiply/times/opMul/opMul_r - takes the string and multiplies it n times, e.g. "-".multiply(5) returns "-----"
}

static void ConstructStringGeneric(asIScriptGeneric * gen)
{
	new (gen->GetObject()) string();
}

static void CopyConstructStringGeneric(asIScriptGeneric * gen)
{
	string * a = static_cast<string *>(gen->GetArgObject(0));
	new (gen->GetObject()) string(*a);
}

static void DestructStringGeneric(asIScriptGeneric * gen)
{
	string * ptr = static_cast<string *>(gen->GetObject());
	ptr->~string();
}

static void AssignStringGeneric(asIScriptGeneric *gen)
{
	string * a = static_cast<string *>(gen->GetArgObject(0));
	string * self = static_cast<string *>(gen->GetObject());
	*self = *a;
	gen->SetReturnAddress(self);
}

static void AddAssignStringGeneric(asIScriptGeneric *gen)
{
	string * a = static_cast<string *>(gen->GetArgObject(0));
	string * self = static_cast<string *>(gen->GetObject());
	*self += *a;
	gen->SetReturnAddress(self);
}

static void StringEqualsGeneric(asIScriptGeneric * gen)
{
	string * a = static_cast<string *>(gen->GetObject());
	string * b = static_cast<string *>(gen->GetArgAddress(0));
	*(bool*)gen->GetAddressOfReturnLocation() = (*a == *b);
}

static void StringCmpGeneric(asIScriptGeneric * gen)
{
	string * a = static_cast<string *>(gen->GetObject());
	string * b = static_cast<string *>(gen->GetArgAddress(0));

	int cmp = 0;
	if( *a < *b ) cmp = -1;
	else if( *a > *b ) cmp = 1;

	*(int*)gen->GetAddressOfReturnLocation() = cmp;
}

static void StringAddGeneric(asIScriptGeneric * gen)
{
	string * a = static_cast<string *>(gen->GetObject());
	string * b = static_cast<string *>(gen->GetArgAddress(0));
	string ret_val = *a + *b;
	gen->SetReturnObject(&ret_val);
}

static void StringLengthGeneric(asIScriptGeneric * gen)
{
	string * self = static_cast<string *>(gen->GetObject());
	*static_cast<asUINT *>(gen->GetAddressOfReturnLocation()) = (asUINT)self->length();
}

static void StringIsEmptyGeneric(asIScriptGeneric * gen)
{
	string * self = reinterpret_cast<string *>(gen->GetObject());
	*reinterpret_cast<bool *>(gen->GetAddressOfReturnLocation()) = StringIsEmpty(*self);
}

static void StringResizeGeneric(asIScriptGeneric * gen)
{
	string * self = static_cast<string *>(gen->GetObject());
	self->resize(*static_cast<asUINT *>(gen->GetAddressOfArg(0)));
}

static void StringInsert_Generic(asIScriptGeneric *gen)
{
	string * self = static_cast<string *>(gen->GetObject());
	asUINT pos = gen->GetArgDWord(0);
	string *other = reinterpret_cast<string*>(gen->GetArgAddress(1));
	StringInsert(pos, *other, *self);
}

static void StringErase_Generic(asIScriptGeneric *gen)
{
	string * self = static_cast<string *>(gen->GetObject());
	asUINT pos = gen->GetArgDWord(0);
	int count = int(gen->GetArgDWord(1));
	StringErase(pos, count, *self);
}

static void StringFindFirst_Generic(asIScriptGeneric * gen)
{
	string *find = reinterpret_cast<string*>(gen->GetArgAddress(0));
	asUINT start = gen->GetArgDWord(1);
	string *self = reinterpret_cast<string *>(gen->GetObject());
	*reinterpret_cast<int *>(gen->GetAddressOfReturnLocation()) = StringFindFirst(*find, start, *self);
}

static void StringFindLast_Generic(asIScriptGeneric * gen)
{
	string *find = reinterpret_cast<string*>(gen->GetArgAddress(0));
	asUINT start = gen->GetArgDWord(1);
	string *self = reinterpret_cast<string *>(gen->GetObject());
	*reinterpret_cast<int *>(gen->GetAddressOfReturnLocation()) = StringFindLast(*find, start, *self);
}

static void StringFindFirstOf_Generic(asIScriptGeneric * gen)
{
	string *find = reinterpret_cast<string*>(gen->GetArgAddress(0));
	asUINT start = gen->GetArgDWord(1);
	string *self = reinterpret_cast<string *>(gen->GetObject());
	*reinterpret_cast<int *>(gen->GetAddressOfReturnLocation()) = StringFindFirstOf(*find, start, *self);
}

static void StringFindLastOf_Generic(asIScriptGeneric * gen)
{
	string *find = reinterpret_cast<string*>(gen->GetArgAddress(0));
	asUINT start = gen->GetArgDWord(1);
	string *self = reinterpret_cast<string *>(gen->GetObject());
	*reinterpret_cast<int *>(gen->GetAddressOfReturnLocation()) = StringFindLastOf(*find, start, *self);
}

static void StringFindFirstNotOf_Generic(asIScriptGeneric * gen)
{
	string *find = reinterpret_cast<string*>(gen->GetArgAddress(0));
	asUINT start = gen->GetArgDWord(1);
	string *self = reinterpret_cast<string *>(gen->GetObject());
	*reinterpret_cast<int *>(gen->GetAddressOfReturnLocation()) = StringFindFirstNotOf(*find, start, *self);
}

static void StringFindLastNotOf_Generic(asIScriptGeneric * gen)
{
	string *find = reinterpret_cast<string*>(gen->GetArgAddress(0));
	asUINT start = gen->GetArgDWord(1);
	string *self = reinterpret_cast<string *>(gen->GetObject());
	*reinterpret_cast<int *>(gen->GetAddressOfReturnLocation()) = StringFindLastNotOf(*find, start, *self);
}

static void formatInt_Generic(asIScriptGeneric * gen)
{
	asINT64 val = gen->GetArgQWord(0);
	string *options = reinterpret_cast<string*>(gen->GetArgAddress(1));
	asUINT width = gen->GetArgDWord(2);
	new(gen->GetAddressOfReturnLocation()) string(formatInt(val, *options, width));
}

static void formatUInt_Generic(asIScriptGeneric * gen)
{
	asQWORD val = gen->GetArgQWord(0);
	string *options = reinterpret_cast<string*>(gen->GetArgAddress(1));
	asUINT width = gen->GetArgDWord(2);
	new(gen->GetAddressOfReturnLocation()) string(formatUInt(val, *options, width));
}

static void formatFloat_Generic(asIScriptGeneric *gen)
{
	double val = gen->GetArgDouble(0);
	string *options = reinterpret_cast<string*>(gen->GetArgAddress(1));
	asUINT width = gen->GetArgDWord(2);
	asUINT precision = gen->GetArgDWord(3);
	new(gen->GetAddressOfReturnLocation()) string(formatFloat(val, *options, width, precision));
}

static void parseInt_Generic(asIScriptGeneric *gen)
{
	string *str = reinterpret_cast<string*>(gen->GetArgAddress(0));
	asUINT base = gen->GetArgDWord(1);
	asUINT *byteCount = reinterpret_cast<asUINT*>(gen->GetArgAddress(2));
	gen->SetReturnQWord(parseInt(*str,base,byteCount));
}

static void parseUInt_Generic(asIScriptGeneric *gen)
{
	string *str = reinterpret_cast<string*>(gen->GetArgAddress(0));
	asUINT base = gen->GetArgDWord(1);
	asUINT *byteCount = reinterpret_cast<asUINT*>(gen->GetArgAddress(2));
	gen->SetReturnQWord(parseUInt(*str, base, byteCount));
}

static void parseFloat_Generic(asIScriptGeneric *gen)
{
	string *str = reinterpret_cast<string*>(gen->GetArgAddress(0));
	asUINT *byteCount = reinterpret_cast<asUINT*>(gen->GetArgAddress(1));
	gen->SetReturnDouble(parseFloat(*str,byteCount));
}

static void StringCharAtGeneric(asIScriptGeneric * gen)
{
	unsigned int index = gen->GetArgDWord(0);
	string * self = static_cast<string *>(gen->GetObject());

	if (index >= self->size())
	{
		// Set a script exception
		asIScriptContext *ctx = asGetActiveContext();
		ctx->SetException("Out of range");

		gen->SetReturnAddress(0);
	}
	else
	{
		gen->SetReturnAddress(&(self->operator [](index)));
	}
}

#if AS_NO_IMPL_OPS_WITH_STRING_AND_PRIMITIVE == 0
static void AssignInt2StringGeneric(asIScriptGeneric *gen)
{
	asINT64 *a = static_cast<asINT64*>(gen->GetAddressOfArg(0));
	string *self = static_cast<string*>(gen->GetObject());
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%lld", (long long)*a);
	*self = buf;
	gen->SetReturnAddress(self);
}

static void AssignUInt2StringGeneric(asIScriptGeneric *gen)
{
	asQWORD *a = static_cast<asQWORD*>(gen->GetAddressOfArg(0));
	string *self = static_cast<string*>(gen->GetObject());
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)*a);
	*self = buf;
	gen->SetReturnAddress(self);
}

static void AssignDouble2StringGeneric(asIScriptGeneric *gen)
{
	double *a = static_cast<double*>(gen->GetAddressOfArg(0));
	string *self = static_cast<string*>(gen->GetObject());
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", *a);
	*self = buf;
	gen->SetReturnAddress(self);
}

static void AssignFloat2StringGeneric(asIScriptGeneric *gen)
{
	float *a = static_cast<float*>(gen->GetAddressOfArg(0));
	string *self = static_cast<string*>(gen->GetObject());
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", (double)*a);
	*self = buf;
	gen->SetReturnAddress(self);
}

static void AssignBool2StringGeneric(asIScriptGeneric *gen)
{
	bool *a = static_cast<bool*>(gen->GetAddressOfArg(0));
	string *self = static_cast<string*>(gen->GetObject());
	*self = (*a ? "true" : "false");
	gen->SetReturnAddress(self);
}

static void AddAssignDouble2StringGeneric(asIScriptGeneric * gen)
{
	double * a = static_cast<double *>(gen->GetAddressOfArg(0));
	string * self = static_cast<string *>(gen->GetObject());
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", *a);
	*self += buf;
	gen->SetReturnAddress(self);
}

static void AddAssignFloat2StringGeneric(asIScriptGeneric * gen)
{
	float * a = static_cast<float *>(gen->GetAddressOfArg(0));
	string * self = static_cast<string *>(gen->GetObject());
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", (double)*a);
	*self += buf;
	gen->SetReturnAddress(self);
}

static void AddAssignInt2StringGeneric(asIScriptGeneric * gen)
{
	asINT64 * a = static_cast<asINT64 *>(gen->GetAddressOfArg(0));
	string * self = static_cast<string *>(gen->GetObject());
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%lld", (long long)*a);
	*self += buf;
	gen->SetReturnAddress(self);
}

static void AddAssignUInt2StringGeneric(asIScriptGeneric * gen)
{
	asQWORD * a = static_cast<asQWORD *>(gen->GetAddressOfArg(0));
	string * self = static_cast<string *>(gen->GetObject());
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)*a);
	*self += buf;
	gen->SetReturnAddress(self);
}

static void AddAssignBool2StringGeneric(asIScriptGeneric * gen)
{
	bool * a = static_cast<bool *>(gen->GetAddressOfArg(0));
	string * self = static_cast<string *>(gen->GetObject());
	*self += (*a ? "true" : "false");
	gen->SetReturnAddress(self);
}

static void AddString2DoubleGeneric(asIScriptGeneric * gen)
{
	string * a = static_cast<string *>(gen->GetObject());
	double * b = static_cast<double *>(gen->GetAddressOfArg(0));
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", *b);
	string ret_val = *a + buf;
	gen->SetReturnObject(&ret_val);
}

static void AddString2FloatGeneric(asIScriptGeneric * gen)
{
	string * a = static_cast<string *>(gen->GetObject());
	float * b = static_cast<float *>(gen->GetAddressOfArg(0));
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", (double)*b);
	string ret_val = *a + buf;
	gen->SetReturnObject(&ret_val);
}

static void AddString2IntGeneric(asIScriptGeneric * gen)
{
	string * a = static_cast<string *>(gen->GetObject());
	asINT64 * b = static_cast<asINT64 *>(gen->GetAddressOfArg(0));
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%lld", (long long)*b);
	string ret_val = *a + buf;
	gen->SetReturnObject(&ret_val);
}

static void AddString2UIntGeneric(asIScriptGeneric * gen)
{
	string * a = static_cast<string *>(gen->GetObject());
	asQWORD * b = static_cast<asQWORD *>(gen->GetAddressOfArg(0));
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)*b);
	string ret_val = *a + buf;
	gen->SetReturnObject(&ret_val);
}

static void AddString2BoolGeneric(asIScriptGeneric * gen)
{
	string * a = static_cast<string *>(gen->GetObject());
	bool * b = static_cast<bool *>(gen->GetAddressOfArg(0));
	string ret_val = *a + (*b ? "true" : "false");
	gen->SetReturnObject(&ret_val);
}

static void AddDouble2StringGeneric(asIScriptGeneric * gen)
{
	double* a = static_cast<double *>(gen->GetAddressOfArg(0));
	string * b = static_cast<string *>(gen->GetObject());
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", *a);
	string ret_val = string(buf) + *b;
	gen->SetReturnObject(&ret_val);
}

static void AddFloat2StringGeneric(asIScriptGeneric * gen)
{
	float* a = static_cast<float *>(gen->GetAddressOfArg(0));
	string * b = static_cast<string *>(gen->GetObject());
	char buf[64];
	(void)std::snprintf(buf, sizeof(buf), "%g", (double)*a);
	string ret_val = string(buf) + *b;
	gen->SetReturnObject(&ret_val);
}

static void AddInt2StringGeneric(asIScriptGeneric * gen)
{
	asINT64* a = static_cast<asINT64 *>(gen->GetAddressOfArg(0));
	string * b = static_cast<string *>(gen->GetObject());
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%lld", (long long)*a);
	string ret_val = string(buf) + *b;
	gen->SetReturnObject(&ret_val);
}

static void AddUInt2StringGeneric(asIScriptGeneric * gen)
{
	asQWORD* a = static_cast<asQWORD *>(gen->GetAddressOfArg(0));
	string * b = static_cast<string *>(gen->GetObject());
	char buf[32];
	(void)std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)*a);
	string ret_val = string(buf) + *b;
	gen->SetReturnObject(&ret_val);
}

static void AddBool2StringGeneric(asIScriptGeneric * gen)
{
	bool* a = static_cast<bool *>(gen->GetAddressOfArg(0));
	string * b = static_cast<string *>(gen->GetObject());
	string ret_val = string(*a ? "true" : "false") + *b;
	gen->SetReturnObject(&ret_val);
}
#endif

static void StringSubString_Generic(asIScriptGeneric *gen)
{
	// Get the arguments
	string *str   = (string*)gen->GetObject();
	asUINT  start = *(int*)gen->GetAddressOfArg(0);
	int     count = *(int*)gen->GetAddressOfArg(1);

	// Return the substring
	new(gen->GetAddressOfReturnLocation()) string(StringSubString(start, count, *str));
}

// static int StringRegexFind(const string& rex, asUINT start, asUINT& outLengthOfMatch, const string& str)
static void StringRegexFind_Generic(asIScriptGeneric* gen)
{
	// Get the arguments
	string* str = (string*)gen->GetObject();
	string *rex = *(string**)gen->GetAddressOfArg(0);
	asUINT start = *(asUINT*)gen->GetAddressOfArg(1);
	asUINT* outLen = *(asUINT**)gen->GetAddressOfArg(2);

	*(int*)(gen->GetAddressOfReturnLocation()) = StringRegexFind(*rex, start, *outLen, *str);
}

void RegisterStdString_Generic(asIScriptEngine *engine)
{
	int r = 0;
	UNUSED_VAR(r);

	// Register the string type
	r = engine->RegisterObjectType("string", sizeof(string), asOBJ_VALUE | asOBJ_APP_CLASS_CDAK); assert( r >= 0 );

	r = engine->RegisterStringFactory("string", GetStdStringFactorySingleton());

	// Register the object operator overloads
	r = engine->RegisterObjectBehaviour("string", asBEHAVE_CONSTRUCT,  "void f()",                    asFUNCTION(ConstructStringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("string", asBEHAVE_CONSTRUCT,  "void f(const string &in)",    asFUNCTION(CopyConstructStringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("string", asBEHAVE_DESTRUCT,   "void f()",                    asFUNCTION(DestructStringGeneric),  asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAssign(const string &in)", asFUNCTION(AssignStringGeneric),    asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(const string &in)", asFUNCTION(AddAssignStringGeneric), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("string", "bool opEquals(const string &in) const", asFUNCTION(StringEqualsGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "int opCmp(const string &in) const", asFUNCTION(StringCmpGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(const string &in) const", asFUNCTION(StringAddGeneric), asCALL_GENERIC); assert( r >= 0 );

	// Register the object methods
#if AS_USE_ACCESSORS != 1
	r = engine->RegisterObjectMethod("string", "uint length() const", asFUNCTION(StringLengthGeneric), asCALL_GENERIC); assert( r >= 0 );
#endif
	r = engine->RegisterObjectMethod("string", "void resize(uint)",   asFUNCTION(StringResizeGeneric), asCALL_GENERIC); assert( r >= 0 );
#if AS_USE_STLNAMES != 1 && AS_USE_ACCESSORS == 1
	r = engine->RegisterObjectMethod("string", "uint get_length() const property", asFUNCTION(StringLengthGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "void set_length(uint) property", asFUNCTION(StringResizeGeneric), asCALL_GENERIC); assert( r >= 0 );
#endif
	r = engine->RegisterObjectMethod("string", "bool isEmpty() const", asFUNCTION(StringIsEmptyGeneric), asCALL_GENERIC); assert( r >= 0 );

	// Register the index operator, both as a mutator and as an inspector
	r = engine->RegisterObjectMethod("string", "uint8 &opIndex(uint)", asFUNCTION(StringCharAtGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "const uint8 &opIndex(uint) const", asFUNCTION(StringCharAtGeneric), asCALL_GENERIC); assert( r >= 0 );

#if AS_NO_IMPL_OPS_WITH_STRING_AND_PRIMITIVE == 0
	// Automatic conversion from values
	r = engine->RegisterObjectMethod("string", "string &opAssign(double)", asFUNCTION(AssignDouble2StringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(double)", asFUNCTION(AddAssignDouble2StringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(double) const", asFUNCTION(AddString2DoubleGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd_r(double) const", asFUNCTION(AddDouble2StringGeneric), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("string", "string &opAssign(float)", asFUNCTION(AssignFloat2StringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(float)", asFUNCTION(AddAssignFloat2StringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(float) const", asFUNCTION(AddString2FloatGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd_r(float) const", asFUNCTION(AddFloat2StringGeneric), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("string", "string &opAssign(int64)", asFUNCTION(AssignInt2StringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(int64)", asFUNCTION(AddAssignInt2StringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(int64) const", asFUNCTION(AddString2IntGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd_r(int64) const", asFUNCTION(AddInt2StringGeneric), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("string", "string &opAssign(uint64)", asFUNCTION(AssignUInt2StringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(uint64)", asFUNCTION(AddAssignUInt2StringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(uint64) const", asFUNCTION(AddString2UIntGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd_r(uint64) const", asFUNCTION(AddUInt2StringGeneric), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("string", "string &opAssign(bool)", asFUNCTION(AssignBool2StringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string &opAddAssign(bool)", asFUNCTION(AddAssignBool2StringGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd(bool) const", asFUNCTION(AddString2BoolGeneric), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("string", "string opAdd_r(bool) const", asFUNCTION(AddBool2StringGeneric), asCALL_GENERIC); assert( r >= 0 );
#endif

	r = engine->RegisterObjectMethod("string", "string substr(uint start = 0, int count = -1) const", asFUNCTION(StringSubString_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "int findFirst(const string &in, uint start = 0) const", asFUNCTION(StringFindFirst_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "int findFirstOf(const string &in, uint start = 0) const", asFUNCTION(StringFindFirstOf_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "int findFirstNotOf(const string &in, uint start = 0) const", asFUNCTION(StringFindFirstNotOf_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "int findLast(const string &in, int start = -1) const", asFUNCTION(StringFindLast_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "int findLastOf(const string &in, int start = -1) const", asFUNCTION(StringFindLastOf_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "int findLastNotOf(const string &in, int start = -1) const", asFUNCTION(StringFindLastNotOf_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "void insert(uint pos, const string &in other)", asFUNCTION(StringInsert_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "void erase(uint pos, int count = -1)", asFUNCTION(StringErase_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterObjectMethod("string", "int regexFind(const string  &in regex, uint start = 0, uint &out lengthOfMatch = void) const", asFUNCTION(StringRegexFind_Generic), asCALL_GENERIC); assert(r >= 0);

	r = engine->RegisterGlobalFunction("uint scan(const string&in str, ?&out ...)", asFUNCTION(StringScan), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterGlobalFunction("string format(const string&in fmt, const ?&in ...)", asFUNCTION(StringFormat), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterGlobalFunction("string formatInt(int64 val, const string &in options = \"\", uint width = 0)", asFUNCTION(formatInt_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterGlobalFunction("string formatUInt(uint64 val, const string &in options = \"\", uint width = 0)", asFUNCTION(formatUInt_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterGlobalFunction("string formatFloat(double val, const string &in options = \"\", uint width = 0, uint precision = 0)", asFUNCTION(formatFloat_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterGlobalFunction("int64 parseInt(const string &in, uint base = 10, uint &out byteCount = 0)", asFUNCTION(parseInt_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterGlobalFunction("uint64 parseUInt(const string &in, uint base = 10, uint &out byteCount = 0)", asFUNCTION(parseUInt_Generic), asCALL_GENERIC); assert(r >= 0);
	r = engine->RegisterGlobalFunction("double parseFloat(const string &in, uint &out byteCount = 0)", asFUNCTION(parseFloat_Generic), asCALL_GENERIC); assert(r >= 0);
}

void RegisterEASTLString(asIScriptEngine * engine)
{
	if (strstr(asGetLibraryOptions(), "AS_MAX_PORTABILITY"))
		RegisterStdString_Generic(engine);
	else
		RegisterStdString_Native(engine);
}

END_AS_NAMESPACE
