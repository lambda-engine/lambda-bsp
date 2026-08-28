// LambdaBSP - the Lambda Engine map compiler.
//
// Reads a Hammer .vmf and writes a Source .bsp the engine can load. Stands in for Valve's vbsp, which is not
// ours to ship and which compiles for an engine we are not running: no visibility, no lightmaps, no BSP tree,
// because Unreal does the culling and the lighting and the engine never reads those lumps. What is left is the
// part that matters - brushes turned into surfaces, with the texture data recorded correctly.
#include "Compiler.h"
#include "FileSystem.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace lbsp;

namespace
{
	void PrintUsage()
	{
		std::printf(
			"LambdaBSP - Lambda Engine map compiler\n"
			"\n"
			"  LambdaBSP [options] <map.vmf>\n"
			"\n"
			"  -game <dir>   the mod folder holding gameinfo.txt. Its search paths are mounted so materials\n"
			"                resolve exactly as they will in game - which is what makes the texture sizes\n"
			"                written into the BSP correct.\n"
			"  -o <file>     where to write the .bsp (default: beside the .vmf)\n"
			"  -nocsg        keep every brush face whole, even where another brush covers it\n"
			"  -v            list the mounted search paths\n"
			"  -h, --help    this\n"
			"\n"
			"Written for Lambda Engine; the BSP it produces is a normal VBSP v20 file.\n");
	}
}

int main(int argc, char** argv)
{
	CompileOptions options;
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		auto NextArg = [&](const char* what) -> std::string
		{
			if (i + 1 >= argc)
			{
				std::printf("LambdaBSP: %s expects a value\n", what);
				std::exit(1);
			}
			return argv[++i];
		};

		if (arg == "-game")
		{
			options.modDir = NextArg("-game");
		}
		else if (arg == "-o")
		{
			options.bspPath = NextArg("-o");
		}
		else if (arg == "-nocsg")
		{
			options.bNoCsg = true;
		}
		else if (arg == "-v" || arg == "-verbose")
		{
			options.bVerbose = true;
		}
		else if (arg == "-h" || arg == "--help" || arg == "-?")
		{
			PrintUsage();
			return 0;
		}
		else if (!arg.empty() && arg[0] == '-')
		{
			std::printf("LambdaBSP: unknown option '%s'\n", arg.c_str());
			return 1;
		}
		else
		{
			options.vmfPath = arg;
		}
	}

	if (options.vmfPath.empty())
	{
		PrintUsage();
		return 1;
	}
	// Hammer passes the map without its extension when it runs a compiler, so both spellings are accepted.
	if (!FileExistsOnDisk(options.vmfPath) && FileExistsOnDisk(options.vmfPath + ".vmf"))
	{
		options.vmfPath += ".vmf";
	}
	if (options.bspPath.empty())
	{
		const std::string dir = GetDirectoryName(options.vmfPath);
		const std::string name = GetFileNameWithoutExtension(options.vmfPath) + ".bsp";
		options.bspPath = dir.empty() ? name : JoinPath(dir, name);
	}

	std::printf("LambdaBSP: %s\n", options.vmfPath.c_str());

	CompileStats stats;
	std::string error;
	if (!CompileMap(options, stats, error))
	{
		std::printf("LambdaBSP: %s\n", error.c_str());
		return 1;
	}

	// "surfaces" can exceed "brush faces": a face with another brush standing on it is not removed but cut into
	// the pieces still showing, so one face can leave several behind.
	std::printf("  %d brushes, %d brush faces -> %d surfaces (%d buried, %d compiler-only)\n",
		stats.brushes, stats.facesBuilt, stats.facesEmitted, stats.facesCulledCsg, stats.facesCulledNoDraw);
	std::printf("  %d entities (%d brush), %d materials\n",
		stats.entities, stats.brushEntities, stats.materials);
	if (stats.missingMaterials > 0)
	{
		// Said loudly and by name, because this is the failure that does not look like one: vbsp records zero
		// for a material it cannot find, and a zero divides into texture coordinates hundreds of times too
		// dense. The map still compiles, still opens in Hammer, and is wrong only once it is in the game.
		std::printf("  WARNING: %d material%s could not be read - their faces will draw at the wrong scale.\n"
			"           Check -game points at the mod folder whose gameinfo.txt reaches this content.\n",
			stats.missingMaterials, stats.missingMaterials == 1 ? "" : "s");
	}
	std::printf("  -> %s (%.2fs)\n", options.bspPath.c_str(), stats.seconds);
	return 0;
}
