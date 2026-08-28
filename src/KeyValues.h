// Valve's KeyValues, which is three file formats for the price of one.
//
// A VMF, a VMT and Steam's libraryfolders.vdf are the same grammar: quoted or bare tokens in key/value pairs,
// braces for nesting, // for comments. LambdaBSP reads all three, so it parses the grammar once.
//
// Keys repeat - that is not a quirk to be tidied away but how a VMF stores six sides of a brush - so this keeps
// entries in order and offers both "the first one called this" and "all of them".
#pragma once

#include <string>
#include <vector>

namespace lbsp
{

class KeyValues
{
public:
	struct Entry
	{
		std::string key;
		std::string value;			// empty when the entry is a block
		KeyValues* block = nullptr;	// owned
	};

	KeyValues() = default;
	~KeyValues();
	KeyValues(const KeyValues&) = delete;
	KeyValues& operator=(const KeyValues&) = delete;

	/** Parses a whole document. Returns null and fills outError on a malformed file. */
	static KeyValues* ParseDocument(const std::string& text, std::string* outError = nullptr);

	const std::vector<Entry>& GetEntries() const { return Entries; }

	/** First value for a key, or the fallback. Key comparison is case-insensitive, as Valve's is. */
	std::string GetString(const std::string& key, const std::string& fallback = std::string()) const;
	double GetNumber(const std::string& key, double fallback = 0.0) const;
	int GetInt(const std::string& key, int fallback = 0) const;
	/** True for "1", "true" and "yes" - what Valve's StringIsTrue accepts on a %compile var. */
	bool GetBool(const std::string& key, bool fallback = false) const;
	bool HasKey(const std::string& key) const;

	/** First child block with this key, or null. */
	const KeyValues* GetBlock(const std::string& key) const;
	/** Every child block with this key, in file order - "solid" in a world, "side" in a solid. */
	std::vector<const KeyValues*> GetBlocks(const std::string& key) const;

	void Add(const std::string& key, const std::string& value);
	void AddBlock(const std::string& key, KeyValues* block);

private:
	std::vector<Entry> Entries;
};

/** Case-insensitive compare, ASCII - the only alphabet a Valve keyname uses. */
bool EqualsNoCase(const std::string& a, const std::string& b);
std::string ToLower(std::string s);

/**
 * Reads "[x y z shift] scale" (a VMF uaxis/vaxis) into four numbers and a scale.
 * Returns false if it does not look like that at all.
 */
bool ParseTextureAxis(const std::string& text, double outAxis[3], double& outShift, double& outScale);

/** Reads "(x y z) (x y z) (x y z)" - a VMF side's three plane points. */
bool ParsePlanePoints(const std::string& text, double outPoints[3][3]);

/** Reads up to N whitespace-separated numbers, e.g. an "origin" or "_light" value. Returns how many it got. */
int ParseNumbers(const std::string& text, double* out, int maxCount);

}	// namespace lbsp
