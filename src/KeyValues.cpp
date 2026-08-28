#include "KeyValues.h"

#include <cctype>
#include <cstdlib>

namespace lbsp
{

bool EqualsNoCase(const std::string& a, const std::string& b)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return true;
}

std::string ToLower(std::string s)
{
	for (char& c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

KeyValues::~KeyValues()
{
	for (Entry& e : Entries)
	{
		delete e.block;
	}
}

namespace
{
	struct Tokenizer
	{
		const std::string& text;
		size_t pos = 0;

		explicit Tokenizer(const std::string& t) : text(t) {}

		void SkipWhitespaceAndComments()
		{
			for (;;)
			{
				while (pos < text.size() && std::isspace((unsigned char)text[pos]))
				{
					++pos;
				}
				if (pos + 1 < text.size() && text[pos] == '/' && text[pos + 1] == '/')
				{
					while (pos < text.size() && text[pos] != '\n')
					{
						++pos;
					}
					continue;
				}
				break;
			}
		}

		// Returns false at end of input. outWasQuoted separates a quoted "" from a missing value.
		bool Next(std::string& out, bool& outWasQuoted, bool& outIsBrace)
		{
			SkipWhitespaceAndComments();
			out.clear();
			outWasQuoted = false;
			outIsBrace = false;
			if (pos >= text.size())
			{
				return false;
			}

			const char c = text[pos];
			if (c == '{' || c == '}')
			{
				out.assign(1, c);
				outIsBrace = true;
				++pos;
				return true;
			}
			if (c == '"')
			{
				outWasQuoted = true;
				++pos;
				while (pos < text.size() && text[pos] != '"')
				{
					// Valve does not escape inside VMF strings, and a lone backslash is a path separator far
					// more often than an escape, so backslashes are taken literally.
					out.push_back(text[pos++]);
				}
				if (pos < text.size())
				{
					++pos;	// closing quote
				}
				return true;
			}
			while (pos < text.size() && !std::isspace((unsigned char)text[pos])
				&& text[pos] != '{' && text[pos] != '}' && text[pos] != '"')
			{
				out.push_back(text[pos++]);
			}
			return !out.empty();
		}

		// Looks at the next token without consuming it.
		bool Peek(std::string& out, bool& outWasQuoted, bool& outIsBrace)
		{
			const size_t save = pos;
			const bool ok = Next(out, outWasQuoted, outIsBrace);
			pos = save;
			return ok;
		}
	};

	// Reads entries until the matching '}' (or end of input at the top level).
	bool ParseBlockBody(Tokenizer& tk, KeyValues* into, bool topLevel, std::string* outError)
	{
		for (;;)
		{
			std::string token;
			bool quoted = false, brace = false;
			if (!tk.Next(token, quoted, brace))
			{
				if (topLevel)
				{
					return true;
				}
				if (outError)
				{
					*outError = "unexpected end of file inside a block";
				}
				return false;
			}
			if (brace && token == "}")
			{
				if (topLevel)
				{
					if (outError)
					{
						*outError = "unmatched '}'";
					}
					return false;
				}
				return true;
			}
			if (brace && token == "{")
			{
				// A block with no name of its own. Valve's own files do not do this, but a hand-edited one can;
				// it is kept under an empty key rather than being a parse error.
				KeyValues* child = new KeyValues();
				if (!ParseBlockBody(tk, child, false, outError))
				{
					delete child;
					return false;
				}
				into->AddBlock(std::string(), child);
				continue;
			}

			// token is a key: what follows is either a value or a block.
			std::string next;
			bool nextQuoted = false, nextBrace = false;
			if (!tk.Peek(next, nextQuoted, nextBrace))
			{
				into->Add(token, std::string());
				return topLevel;
			}
			if (nextBrace && next == "{")
			{
				tk.Next(next, nextQuoted, nextBrace);
				KeyValues* child = new KeyValues();
				if (!ParseBlockBody(tk, child, false, outError))
				{
					delete child;
					return false;
				}
				into->AddBlock(token, child);
				continue;
			}
			if (nextBrace)
			{
				if (outError)
				{
					*outError = "'}' where a value was expected after key '" + token + "'";
				}
				return false;
			}
			tk.Next(next, nextQuoted, nextBrace);
			into->Add(token, next);
		}
	}
}

KeyValues* KeyValues::ParseDocument(const std::string& text, std::string* outError)
{
	Tokenizer tk(text);
	KeyValues* root = new KeyValues();
	if (!ParseBlockBody(tk, root, true, outError))
	{
		delete root;
		return nullptr;
	}
	return root;
}

std::string KeyValues::GetString(const std::string& key, const std::string& fallback) const
{
	for (const Entry& e : Entries)
	{
		if (!e.block && EqualsNoCase(e.key, key))
		{
			return e.value;
		}
	}
	return fallback;
}

double KeyValues::GetNumber(const std::string& key, double fallback) const
{
	const std::string s = GetString(key);
	return s.empty() ? fallback : std::atof(s.c_str());
}

int KeyValues::GetInt(const std::string& key, int fallback) const
{
	const std::string s = GetString(key);
	return s.empty() ? fallback : std::atoi(s.c_str());
}

bool KeyValues::GetBool(const std::string& key, bool fallback) const
{
	const std::string s = GetString(key);
	if (s.empty())
	{
		return fallback;
	}
	return s == "1" || EqualsNoCase(s, "true") || EqualsNoCase(s, "yes");
}

bool KeyValues::HasKey(const std::string& key) const
{
	for (const Entry& e : Entries)
	{
		if (EqualsNoCase(e.key, key))
		{
			return true;
		}
	}
	return false;
}

const KeyValues* KeyValues::GetBlock(const std::string& key) const
{
	for (const Entry& e : Entries)
	{
		if (e.block && EqualsNoCase(e.key, key))
		{
			return e.block;
		}
	}
	return nullptr;
}

std::vector<const KeyValues*> KeyValues::GetBlocks(const std::string& key) const
{
	std::vector<const KeyValues*> out;
	for (const Entry& e : Entries)
	{
		if (e.block && EqualsNoCase(e.key, key))
		{
			out.push_back(e.block);
		}
	}
	return out;
}

void KeyValues::Add(const std::string& key, const std::string& value)
{
	Entry e;
	e.key = key;
	e.value = value;
	Entries.push_back(e);
}

void KeyValues::AddBlock(const std::string& key, KeyValues* block)
{
	Entry e;
	e.key = key;
	e.block = block;
	Entries.push_back(e);
}

int ParseNumbers(const std::string& text, double* out, int maxCount)
{
	int count = 0;
	const char* p = text.c_str();
	while (count < maxCount)
	{
		while (*p && (std::isspace((unsigned char)*p) || *p == '[' || *p == ']' || *p == '(' || *p == ')'))
		{
			++p;
		}
		if (!*p)
		{
			break;
		}
		char* end = nullptr;
		const double v = std::strtod(p, &end);
		if (end == p)
		{
			break;
		}
		out[count++] = v;
		p = end;
	}
	return count;
}

bool ParseTextureAxis(const std::string& text, double outAxis[3], double& outShift, double& outScale)
{
	// "[1 0 0 -2304] 0.25": the bracketed part is the axis and the texture shift, the trailing number the scale.
	// The shift is deliberately NOT divided by the scale here - see Compiler.cpp, where the texinfo is built.
	double v[5] = { 0, 0, 0, 0, 1 };
	if (ParseNumbers(text, v, 5) < 5)
	{
		return false;
	}
	outAxis[0] = v[0];
	outAxis[1] = v[1];
	outAxis[2] = v[2];
	outShift = v[3];
	outScale = (v[4] != 0.0) ? v[4] : 1.0;
	return true;
}

bool ParsePlanePoints(const std::string& text, double outPoints[3][3])
{
	double v[9];
	if (ParseNumbers(text, v, 9) < 9)
	{
		return false;
	}
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			outPoints[i][j] = v[i * 3 + j];
		}
	}
	return true;
}

}	// namespace lbsp
