// The compile itself: a VMF in, a BSP out.
#pragma once

#include "Math.h"
#include "Winding.h"

#include <string>
#include <vector>

namespace lbsp
{

class FileSystem;
class KeyValues;
class MaterialCache;

struct CompileOptions
{
	std::string vmfPath;
	std::string bspPath;
	std::string modDir;			// the folder holding gameinfo.txt
	bool bVerbose = false;
	bool bNoCsg = false;		// emit every brush face untouched; for looking at what CSG removed
	bool bNoTexScaleFix = false;// keep going even when a material is missing, writing zeros as vbsp does
};

struct CompileStats
{
	int brushes = 0;
	int entities = 0;
	int brushEntities = 0;
	int facesBuilt = 0;			// before CSG
	int facesEmitted = 0;
	int facesCulledNoDraw = 0;
	int facesCulledCsg = 0;
	int materials = 0;
	int missingMaterials = 0;
	double seconds = 0.0;
};

/** Runs the whole pipeline. Returns false and fills outError on a fatal problem. */
bool CompileMap(const CompileOptions& options, CompileStats& outStats, std::string& outError);

}	// namespace lbsp
