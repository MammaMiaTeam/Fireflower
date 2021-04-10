#include <iostream>
//#include <syncstream>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <variant>
#include <thread>
#include <atomic>

#include "common.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"

#include "blz.h"
#include "nfsfsh.h"

#define EXIT_ON_ERROR(x) if((!x)){return -1;}
#define RETURN_ON_ERROR(x) if((!x)){return 0;}
#define CONTINUE_ON_ERROR(x) if((!x)){continue;}


using namespace rapidjson;
namespace fs = std::filesystem;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef u32 CodeTarget;

const CodeTarget arm9Target = 0;
const CodeTarget arm7Target = 1;
const CodeTarget invalidTarget = -1;

inline CodeTarget ov9Target(u32 ovID) {
	return ovID + 1000;
}

inline CodeTarget ov7Target(u32 ovID) {
	return ovID + 2000;
}

inline bool isARM9Target(CodeTarget target) {
	return (target == arm9Target || (target >= 1000 && target < 2000));
}

inline bool isARM9Overlay(CodeTarget target) {
	return (target >= 1000 && target < 2000);
}

inline bool isARM7Overlay(CodeTarget target) {
	return (target >= 2000 && target < 3000);
}

inline bool isOverlay(CodeTarget target) {
	return (target >= 1000 && target < 3000);
}

inline bool isBinary(CodeTarget target) {
	return (target == arm9Target || target == arm7Target);
}

inline u32 getOverlayID(CodeTarget target) {
	return target > 2000 ? target - 2000 : target - 1000;
}

inline std::string getProcessorID(CodeTarget target) {
	return isARM9Target(target) ? "9" : "7";
}

inline std::string getCodeTargetName(CodeTarget target) {

	if (target == arm9Target) {
		return "arm9";
	} else if (target == arm7Target) {
		return "arm7";
	} else if (target < 2000) {
		return "ov9_" + std::to_string(target - 1000);
	} else if (target < 3000) {
		return "ov7_" + std::to_string(target - 2000);
	} else {
		return "?";
	}

}

inline CodeTarget getCodeTarget(const std::string& s) {

	if (s == "arm9") {
		return arm9Target;
	} else if (s == "arm7") {
		return arm7Target;
	} else if (s.starts_with("ov9_")) {

		if (s.find_first_not_of("0123456789", 4) != std::string::npos || s.size() > 7) {
			std::cout << DERROR << "Invalid arm9 overlay target " << s << std::endl;
			return invalidTarget;
		}

		return ov9Target(std::stoul(s.substr(4)));

	} else if (s.starts_with("ov7_")) {

		if (s.find_first_not_of("0123456789", 4) != std::string::npos || s.size() > 7) {
			std::cout << DERROR << "Invalid arm7 overlay target " << s << std::endl;
			return invalidTarget;
		}

		return ov7Target(std::stoul(s.substr(4)));

	} else {
		return invalidTarget;
	}

}


inline bool isCompilableFile(const fs::path& p) {
	const fs::path& ext = p.extension();
	return ext == ".cpp" || ext == ".c" || ext == ".s" || ext == ".S";
}



//armv5te
enum ARMOpcodes : u32 {
	ARM_B = 0x0A000000,
	ARM_BL = 0x0B000000,
	ARM_BLX = 0xFA000000,
	ARM_BX_R = 0x012FFF10,
	ARM_BLX_R = 0x012FFF30,
	ARM_PUSH = 0x09200000,
	ARM_POP = 0x08B00000
};


//armv5te
enum ThumbOpcodes : u16 {
	THUMB_BX_R = 0x4700,
	THUMB_BLX_R = 0x4780,
	THUMB_BL0 = 0xF000,
	THUMB_BL1 = 0xF800,
	THUMB_BLX1 = 0xE800,
	THUMB_PUSH = 0xB400,
	THUMB_POP = 0xBC00
};


enum ARMConditions : u32 {
	COND_EQ = 0x0 << 28,
	COND_NE = 0x1 << 28,
	COND_CS = 0x2 << 28,
	COND_CC = 0x3 << 28,
	COND_MI = 0x4 << 28,
	COND_PL = 0x5 << 28,
	COND_VS = 0x6 << 28,
	COND_VC = 0x7 << 28,
	COND_HI = 0x8 << 28,
	COND_LS = 0x9 << 28,
	COND_GE = 0xA << 28,
	COND_LT = 0xB << 28,
	COND_GT = 0xC << 28,
	COND_LE = 0xD << 28,
	COND_AL = 0xE << 28
};

//armv4t has no support for blx instructions


struct BuildSettings {

	std::vector<fs::path> includeDirs;
	fs::path nitroFSDir;
	fs::path sourceDir;
	fs::path toolchainDir;
	fs::path backupDir;
	fs::path buildDir;
	fs::path objectDir;
	fs::path depsDir;
	fs::path outputFile;
	fs::path symbol7File;
	fs::path symbol9File;
	fs::path libraryDir;

	std::string prebuildCmd;
	std::string postbuildCmd;

	struct Executables {
		fs::path toolPath;
		std::string gcc;
		std::string ld;
	} executables;

	struct Flags {
		std::string cpp;
		std::string c;
		std::string assembly;
		std::string arm9;
		std::string arm7;
	} flags;

	bool pedantic;
	bool useAEABI;

};



struct PatchSettings {

	struct BinarySettings {

		u32 reloc;
		u32 start;
		u32 end;
		bool compress;
		bool enabled;

	} arm9, arm7;

};



struct ARMBinaryProperties {

	u32 offset;
	u32 moduleParams;
	u32 autoloadStart;
	u32 autoloadEnd;
	u32 autoloadRead;
	u32 compressedEnd;

};


struct SectionData {

	u32 start;
	u32 end;
	u32 destination;

};


enum class HookType {
	None,
	Hook,
	Link,
	Safe,
	Replace
};


struct Hook {

	CodeTarget codeTarget;
	HookType hookType;
	u32 hookAddress;
	u32 funcAddress;

};

constexpr u32 noBSS = -1;

struct Patch {

	CodeTarget codeTarget;
	u32 ramAddress;
	u32 binOffset;
	u32 binSize;
	u32 bssSize;
	u32 bssAlign;

};



struct OverlayEntry {

	constexpr inline static u32 compressFlag = 0x01000000;
	constexpr inline static u32 verifyFlag = 0x02000000;

	u32 ovID;
	u32 start;
	u32 size;
	u32 bss;
	u32 initStart;
	u32 initEnd;
	u32 fileID;
	u32 flags;

};


struct DependencyTracker {

	std::unordered_map<std::string, u64> dependencies;
	std::unordered_map<std::string, u64> trackers;
	std::unordered_map<std::string, u64> oldTrackers;
	std::unordered_set<std::string> compilationObjects;

	u64 jsonTrackedModifiedTime = -1;
	u64 jsonLastModifiedTime = 0;

};



typedef std::map<CodeTarget, OverlayEntry> OverlayTable;
typedef std::unordered_map<CodeTarget, std::set<fs::path>> CodeTargetMap;
typedef std::unordered_multimap<CodeTarget, SectionData> SectionMap;
typedef std::unordered_map<std::string, Hook> HookMap;
typedef std::unordered_map<CodeTarget, u32> SafeMap;
typedef std::unordered_map<std::string, fs::path> FileIDSymbols;
typedef std::vector<Patch> PatchList;
typedef std::variant<Patch, Hook> Fixup;


struct HookSymbols {

	HookMap hooks7;
	HookMap hooks9;
	SafeMap safeCounts;

	inline HookMap& getSymbolMap(bool arm9) {
		return arm9 ? hooks9 : hooks7;
	}

	inline void incSafe(CodeTarget target, u32 bytes) {
		safeCounts[target] += bytes;
	}

};

const std::string& buildTargetFilename = "buildroot.txt";
fs::path jsonPath;

bool loadBuildSettings(Document& root);

bool populateBuild(Document& root, BuildSettings& settings);
bool populatePatch(Document& root, PatchSettings& settings, const OverlayTable& ovt);
bool populateBinarySettings(const Value& jsonNode, PatchSettings::BinarySettings& settings);
bool populateFileIDs(const BuildSettings& settings, Document& root, FileIDSymbols& fidSymbols);
bool populateCodeTargets(const BuildSettings& settings, Document& root, CodeTargetMap& codeTargets);

bool createDirectory(const fs::path& p, const std::string& name);
bool createObjectDirectories(const BuildSettings& settings);
bool createDependencyDirectories(const BuildSettings& settings);
bool compileSource(const BuildSettings& settings, const CodeTargetMap& targets, DependencyTracker& tracker);
bool collectHooks(const BuildSettings& settings, CodeTargetMap& codeTargets, HookSymbols& hookSymbols);
bool generateLinkerScripts(const BuildSettings& buildSettings, const PatchSettings& patchSettings, CodeTargetMap& targets, HookSymbols& hookSymbols);
bool linkSource(const BuildSettings& settings);
bool parseElf(const BuildSettings& settings, HookSymbols& hookSymbols, std::vector<Fixup>& fixups);
bool patchBinaries(const BuildSettings& buildSettings, const PatchSettings& patchSettings, OverlayTable& ovt, const std::vector<Fixup>& fixups);
bool generateFileIDs(const BuildSettings& settings, const DependencyTracker& tracker, const OverlayTable& ovt, const FileIDSymbols& fidSymbols);

bool executePrebuildCommand(const BuildSettings& buildSettings);
bool executePostbuildCommand(const BuildSettings& buildSettings);

void loadDependencies(const BuildSettings& settings, DependencyTracker& tracker);
void trackDependencies(const BuildSettings& settings, DependencyTracker& tracker, const fs::path& dep, bool old);
void saveDependencies(const BuildSettings& settings, DependencyTracker& tracker);

void write(const SectionMap& sections, CodeTarget target, u32 address, const std::vector<u8>& data, std::vector<u8>& binary);
u32 readWord(const SectionMap& sections, CodeTarget target, u32 address, std::vector<u8>& binary);
void writeWord(const SectionMap& sections, CodeTarget target, u32 address, u32 value, std::vector<u8>& binary);
void writeHalfword(const SectionMap& sections, CodeTarget target, u32 address, u16 value, std::vector<u8>& binary);
bool getSectionAddressOffset(const SectionMap& sections, CodeTarget target, u32 address, u32 size, u32& offset);
void checkSafeInstruction(u32 opcode);

bool fixBinarySections(SectionMap& sections, CodeTarget target, u32 offset);
bool addBinarySections(SectionMap& sections, CodeTarget target, const ARMBinaryProperties& properties, const std::vector<u8>& binary);

bool loadBinary(const BuildSettings& settings, CodeTarget target, ARMBinaryProperties& properties, std::vector<u8>& binary);
bool saveBinary(const BuildSettings& settings, CodeTarget target, const ARMBinaryProperties& properties, std::vector<u8>& binary, bool compress);
bool loadOverlay(const BuildSettings& settings, CodeTarget target, std::vector<u8>& binary);
bool saveOverlay(const BuildSettings& settings, CodeTarget target, std::vector<u8>& binary, OverlayTable& ovt);
bool loadOverlayTable(const BuildSettings& settings, OverlayTable& ovt);
bool saveOverlayTable(const BuildSettings& settings, const OverlayTable& ovt);

bool backupNitroFSFile(const BuildSettings& settings, const std::string& path);
bool backupFiles(const BuildSettings& settings, OverlayTable& ovt);
bool loadROMHeader(const BuildSettings& settings, std::vector<u8>& header);

bool loadPatch(const BuildSettings& settings, const Patch& patch, std::vector<u8>& data);
void patchBinary(const BuildSettings& settings, const Patch& patchInfo, const std::vector<u8>& patch, const ARMBinaryProperties& properties, std::vector<u8>& binary);

bool loadARMBinaryProperties(const BuildSettings& settings, CodeTarget target, const std::vector<u8>& binary, ARMBinaryProperties& properties);
bool compileSet(const BuildSettings& settings, CodeTarget target, const std::set<fs::path>& files, const std::string& includeFlags, DependencyTracker& tracker);
bool needsCompilation(const BuildSettings& settings, const DependencyTracker& tracker, const fs::path& src);
void deleteUnreferencedObjects(const BuildSettings& settings, const DependencyTracker& tracker);
std::string getPathString(const fs::path& p);
fs::path getObjectPath(const BuildSettings& settings, const fs::path& src);
fs::path getDependencyPath(const BuildSettings& settings, const fs::path& src);
u64 timeLastModified(const fs::path& p);
bool removeFile(const fs::path& p, const std::string& name);
bool removeDirectory(const fs::path& p, const std::string& name);
bool jsonChanged(const DependencyTracker& tracker);

bool getSourceSet(const Value& v, const std::string& key, const fs::path& sourceDir, std::set<fs::path>& paths);
void scanDefaultTarget(CodeTargetMap& codeTargets, CodeTarget target, const fs::path& sourceDir);

std::string getHexString(u32 hex);
std::string jsonGetTypename(const Value& v);
bool jsonReadBool(const Value& v, const std::string& key, bool& b);
bool jsonReadHex(const Value& v, const std::string& key, u32& h);
bool jsonReadUnsigned(const Value& v, const std::string& key, u32& h);
bool jsonReadString(const Value& v, const std::string& key, std::string& s);
bool jsonReadPath(const Value& v, const std::string& key, fs::path& p, bool forceExist);
bool jsonReadDir(const Value& v, const std::string& key, fs::path& p, bool forceExist);
bool jsonReadDirArray(const Value& v, const std::string& key, std::vector<fs::path>& paths, bool forceExist);



int main(int argc, char** argv) {

	Document d;
	BuildSettings buildSettings{};
	PatchSettings patchSettings{};
	FileIDSymbols fidSyms{};
	CodeTargetMap codeTargets{};
	HookSymbols hookSymbols{};
	std::vector<Fixup> fixups{};
	OverlayTable ovt;
	DependencyTracker tracker{};

	EXIT_ON_ERROR(loadBuildSettings(d))
	EXIT_ON_ERROR(populateBuild(d, buildSettings))

	EXIT_ON_ERROR(createDirectory(buildSettings.backupDir, "backup"))
	EXIT_ON_ERROR(createDirectory(buildSettings.buildDir, "build"))
	EXIT_ON_ERROR(createObjectDirectories(buildSettings))
	EXIT_ON_ERROR(createDependencyDirectories(buildSettings))
	EXIT_ON_ERROR(backupFiles(buildSettings, ovt))

	EXIT_ON_ERROR(populatePatch(d, patchSettings, ovt))
	EXIT_ON_ERROR(populateFileIDs(buildSettings, d, fidSyms))
	EXIT_ON_ERROR(populateCodeTargets(buildSettings, d, codeTargets))
	
	EXIT_ON_ERROR(executePrebuildCommand(buildSettings))

	loadDependencies(buildSettings, tracker);
	EXIT_ON_ERROR(generateFileIDs(buildSettings, tracker, ovt, fidSyms))
	EXIT_ON_ERROR(compileSource(buildSettings, codeTargets, tracker))
	deleteUnreferencedObjects(buildSettings, tracker);
	saveDependencies(buildSettings, tracker);

	EXIT_ON_ERROR(collectHooks(buildSettings, codeTargets, hookSymbols))
	EXIT_ON_ERROR(generateLinkerScripts(buildSettings, patchSettings, codeTargets, hookSymbols))
	EXIT_ON_ERROR(linkSource(buildSettings))
	EXIT_ON_ERROR(parseElf(buildSettings, hookSymbols, fixups))
	EXIT_ON_ERROR(patchBinaries(buildSettings, patchSettings, ovt, fixups))

	EXIT_ON_ERROR(saveOverlayTable(buildSettings, ovt))

	EXIT_ON_ERROR(executePostbuildCommand(buildSettings))

	std::cout << DINFO << "Build successfully finished" << std::endl;
	
	return 0;

}





bool loadBuildSettings(Document& root) {

	fs::path buildTargetPath(buildTargetFilename);

	if (!fs::exists(buildTargetPath) || !fs::is_regular_file(buildTargetPath)) {
		std::cout << DERROR << "Could not find " << buildTargetFilename << std::endl;
		return false;
	}

	std::ifstream buildTargetFile(buildTargetPath);

	if (!buildTargetFile.is_open()) {
		std::cout << DERROR << "Failed to open " << buildTargetFilename << std::endl;
		return false;
	}

	std::string buildSettingsFilename;
	std::getline(buildTargetFile, buildSettingsFilename);

	buildTargetFile.close();

	jsonPath = buildSettingsFilename.c_str();

	if (!fs::exists(jsonPath) || !fs::is_regular_file(jsonPath)) {
		std::cout << DERROR << "Could not find JSON file " << buildSettingsFilename << std::endl;
		return false;
	}

	std::ifstream buildSettingsFile(jsonPath);

	if (!buildSettingsFile.is_open()) {
		std::cout << DERROR << "Failed to open " << buildSettingsFilename << std::endl;
		return false;
	}

	IStreamWrapper isw(buildSettingsFile);
	root.ParseStream(isw);

	buildSettingsFile.close();

	if (root.HasParseError()) {
		std::cout << DERROR << "Failed to parse " << buildSettingsFilename << ": Invalid JSON" << std::endl;
		return false;
	}

	fs::current_path(jsonPath.parent_path());

	return true;

}



bool populateBuild(Document& root, BuildSettings& settings) {

	if (!root["build"].IsObject()) {
		std::cout << DERROR << "Missing JSON object 'build'" << std::endl;
		return false;
	}

	const Value& buildNode = root["build"].GetObject();

	RETURN_ON_ERROR(jsonReadDirArray(buildNode, "include-directories", settings.includeDirs, true))
	RETURN_ON_ERROR(jsonReadDir(buildNode, "source", settings.sourceDir, true))
	RETURN_ON_ERROR(jsonReadDir(buildNode, "filesystem", settings.nitroFSDir, true))
	RETURN_ON_ERROR(jsonReadDir(buildNode, "toolchain", settings.toolchainDir, true))
	RETURN_ON_ERROR(jsonReadPath(buildNode, "backup", settings.backupDir, false))
	RETURN_ON_ERROR(jsonReadPath(buildNode, "build", settings.buildDir, false))
	RETURN_ON_ERROR(jsonReadPath(buildNode, "output", settings.outputFile, false))

	settings.objectDir = settings.buildDir / "object";
	settings.depsDir = settings.buildDir / "deps";

	if (buildNode["symbols7"].IsString()) {
		RETURN_ON_ERROR(jsonReadPath(buildNode, "symbols7", settings.symbol7File, true))
	}

	if (buildNode["symbols9"].IsString()) {
		RETURN_ON_ERROR(jsonReadPath(buildNode, "symbols9", settings.symbol9File, true))
	}

	if (buildNode["pre-build"].IsString()) {
		RETURN_ON_ERROR(jsonReadString(buildNode, "pre-build", settings.prebuildCmd))
	}

	if (buildNode["post-build"].IsString()) {
		RETURN_ON_ERROR(jsonReadString(buildNode, "post-build", settings.postbuildCmd))
	}

	if (!buildNode["executables"].IsObject()) {
		std::cout << DERROR << "Missing JSON object 'build/executables'" << std::endl;
		return false;
	}

	RETURN_ON_ERROR(jsonReadString(buildNode["executables"], "gcc", settings.executables.gcc))
	RETURN_ON_ERROR(jsonReadString(buildNode["executables"], "ld", settings.executables.ld))

	settings.executables.gcc = fs::path(settings.toolchainDir / "ff-gcc" / "bin" / settings.executables.gcc).string();
	settings.executables.ld = fs::path(settings.toolchainDir / "ff-gcc" / "bin" / settings.executables.ld).string();

	if (!fs::exists(settings.executables.gcc) || !fs::is_regular_file(settings.executables.gcc)) {
		std::cout << DERROR << "Could not find gcc executable as " << settings.executables.gcc << std::endl;
		return false;
	}

	if (!fs::exists(settings.executables.ld) || !fs::is_regular_file(settings.executables.ld)) {
		std::cout << DERROR << "Could not find ld executable as " << settings.executables.ld << std::endl;
		return false;
	}

	if (buildNode["flags"]["c++"].IsString()) {
		settings.flags.cpp = buildNode["flags"]["c++"].GetString();
	}

	if (buildNode["flags"]["c"].IsString()) {
		settings.flags.c = buildNode["flags"]["c"].GetString();
	}

	if (buildNode["flags"]["asm"].IsString()) {
		settings.flags.assembly = buildNode["flags"]["asm"].GetString();
	}

	if (buildNode["flags"]["arm9"].IsString()) {
		settings.flags.arm9 = buildNode["flags"]["arm9"].GetString();
	}

	if (buildNode["flags"]["arm7"].IsString()) {
		settings.flags.arm7 = buildNode["flags"]["arm7"].GetString();
	}

	if (buildNode["pedantic"].IsBool()) {
		settings.pedantic = buildNode["pedantic"].GetBool();
	} else {
		settings.pedantic = true;
	}

	if (buildNode["allow-eabi-extensions"].IsBool()) {
		settings.useAEABI = buildNode["allow-eabi-extensions"].GetBool();
	} else {
		settings.useAEABI = false;
	}

	if (settings.useAEABI) {

		bool result = jsonReadDir(buildNode, "library", settings.libraryDir, true);

		if (result) {
			std::cout << DINFO << "Building with aeabi extensions" << std::endl;
		} else {
			std::cout << DWARNING << "Failed to find libgcc.a. Supply a valid path or disable aeabi extensions." << std::endl;
			settings.useAEABI = false;
		}

	}

	return true;

}




bool populatePatch(Document& root, PatchSettings& settings, const OverlayTable& ovt) {

	if (!root["patch"].IsObject()) {
		std::cout << DERROR << "Missing JSON object 'patch'" << std::endl;
		return false;
	}

	const Value& patchNode = root["patch"].GetObject();

	settings.arm9.enabled = false;
	settings.arm7.enabled = false;

	for (auto& v : patchNode.GetObject()) {

		const std::string& key = v.name.GetString();
		const Value& targetNode = v.value;

		if (key == "arm9") {

			RETURN_ON_ERROR(populateBinarySettings(targetNode, settings.arm9))

		} else if (key == "arm7") {

			RETURN_ON_ERROR(populateBinarySettings(targetNode, settings.arm7))

		} else {

			std::cout << DERROR << "Unknown patch node '" << key << "'" << std::endl;
			return false;

		}
	
	}

	return true;

}




bool populateBinarySettings(const Value& jsonNode, PatchSettings::BinarySettings& settings) {

	settings.enabled = true;

	RETURN_ON_ERROR(jsonReadHex(jsonNode, "reloc", settings.reloc))
	RETURN_ON_ERROR(jsonReadHex(jsonNode, "start", settings.start))
	RETURN_ON_ERROR(jsonReadHex(jsonNode, "end", settings.end))

	if (jsonNode["compress"].IsBool()) {
		settings.compress = jsonNode["compress"].GetBool();
	}

	return true;

}



bool populateFileIDs(const BuildSettings& settings, Document& root, FileIDSymbols& fidSymbols) {

	if (!root["file-id"].IsObject()) {
		return true;
	}

	const Value& fidNode = root["file-id"].GetObject();

	for (auto& v : fidNode.GetObject()) {

		std::string symbol = v.name.GetString();

		if (symbol.empty() || std::find_if(symbol.begin(), symbol.end(), [](char c) { return !(isalnum(c) || c == '_'); }) != symbol.end() || isdigit(symbol[0])) {
			std::cout << DWARNING << "Invalid symbol " << symbol << std::endl;
			continue;
		}

		if (!v.value.IsString()) {
			std::cout << DWARNING << "File path for symbol " << symbol << " is not a string" << std::endl;
			continue;
		}

		fs::path p = settings.nitroFSDir / "root" / v.value.GetString();

		if (!fs::exists(p) || !fs::is_regular_file(p)) {
			std::cout << DWARNING << "File " << p.string() << " for symbol " << symbol << " does not exist" << std::endl;
			continue;
		}

		if (fidSymbols.contains(symbol)) {
			std::cout << DWARNING << "Multiple file ID symbol references to '" << symbol << "'" << std::endl;
			continue;
		}

		fidSymbols[symbol] = fs::absolute(p);

	}

	return true;

}



bool createDirectory(const fs::path& p, const std::string& name) {

	if (!fs::exists(p) || !fs::is_directory(p)) {

		bool dirCreated = false;

		try {

			dirCreated = fs::create_directory(p);

		} catch (fs::filesystem_error& e) {

			std::cout << DERROR << "Failed to create " << name << " directory " << p.string() << ": " << e.what() << std::endl;
			return false;

		}

		if (!dirCreated) {
			std::cout << DERROR << "Failed to create " << name << " directory " << p.string() << std::endl;
			return false;
		}

	}

	return true;

}




bool compileSource(const BuildSettings& settings, const CodeTargetMap& codeTargets, DependencyTracker& tracker) {

	std::cout << DINFO << "Started compilation scan" << std::endl;

	const fs::path& sourcePath = settings.sourceDir;
	std::string includeFlags;

	for (const fs::path& include : settings.includeDirs) {
		includeFlags += " -I" + include.string();
	}

	fs::path ffcPath(settings.toolchainDir / "internal" / "ffc.h");
	fs::path fidPath(settings.toolchainDir / "internal" / "fid.h");

	if (fs::exists(fidPath) && fs::is_regular_file(fidPath)) {
		includeFlags += " -include " + fidPath.string();
	}

	includeFlags += " -include " + ffcPath.string();

	std::vector<std::string> debugOutput;
	std::vector<std::string> commands;
	std::vector<fs::path> newDeps;

	const std::string* flags = nullptr;
	const std::string* arch = nullptr;

	for (const auto& e : codeTargets) {
		
		CodeTarget target = e.first;
		const auto& sourceFiles = e.second;

		for (const fs::path& source : sourceFiles) {

			const std::string& extension = source.extension().string();
			fs::path objectPath = getObjectPath(settings, source);
			fs::path depPath = getDependencyPath(settings, source);
			std::string defines;

			if (!isCompilableFile(source)) {
				continue;
			}

			std::string objectPathString = getPathString(objectPath);

			if (tracker.compilationObjects.contains(objectPathString)) {
				std::cout << DERROR << "Fatal error: Multiple references to object file " << objectPathString << std::endl;
				return false;
			}

			tracker.compilationObjects.insert(objectPathString);

			if (!needsCompilation(settings, tracker, source)) {
				trackDependencies(settings, tracker, depPath, true);
				continue;
			}

			if (extension == ".cpp") {

				debugOutput.push_back(DINFO + std::string("Compiling C++ source ") + source.string());
				flags = &settings.flags.cpp;
				defines += " -D__FFC_LANG_CPP";

			} else if (extension == ".c") {

				debugOutput.push_back(DINFO + std::string("Compiling C source ") + source.string());
				flags = &settings.flags.c;
				defines += " -D__FFC_LANG_C";

			} else {

				debugOutput.push_back(DINFO + std::string("Compiling S source ") + source.string());
				flags = &settings.flags.assembly;
				defines += " -D__FFC_LANG_ASM";

			}

			if (isARM9Target(target)) {

				arch = &settings.flags.arm9;
				defines += " -D__FFC_ARCH_NUM=9";

			} else {

				arch = &settings.flags.arm7;
				defines += " -D__FFC_ARCH_NUM=7";

			}

			commands.push_back(settings.executables.gcc + " " + *flags + " " + *arch + " -c " + source.string() + " -o " + objectPathString + " -MMD -MF " + depPath.string() + includeFlags + defines);
			newDeps.push_back(depPath);

		}

	}

	std::atomic_bool threadsRunning = true;
	std::atomic_bool successful = true;
	std::atomic_uint compilationIndex = 0;
	std::atomic_bool pedantic = settings.pedantic;

	auto compileFunction = [&]() {

		while (threadsRunning) {

			u32 i = compilationIndex.fetch_add(1);

			if (i >= commands.size()) {
				threadsRunning = false;
				return;
			}

			std::cout << debugOutput.at(i) << std::endl;
			
			if (std::system(commands.at(i).c_str())) {
					
				successful = false;

				if (pedantic) {
					threadsRunning = false;
					return;
				}

			}

		}

	};


	std::cout << DINFO << "Compiling..." << std::endl;


	constexpr const u32 threadCount = 8;

	std::thread threads[threadCount];

	for (u32 i = 0; i < threadCount; i++) {
		threads[i] = std::move(std::thread(compileFunction));
	}

	for (u32 i = 0; i < threadCount; i++) {
		threads[i].join();
	}

	if (!successful) {

		std::cout << DERROR << "Compilation failed" << std::endl;
		return false;

	}

	for (const auto& depPath : newDeps) {
		trackDependencies(settings, tracker, depPath, false);
	}

	std::cout << DINFO << "Compilation successful" << std::endl;

	return true;

}


/*
bool compileSet(const BuildSettings& settings, CodeTarget target, const std::set<fs::path>& files, const std::string& includeFlags, DependencyTracker& tracker) {

	const std::string* flags = nullptr;
	const std::string* arch = nullptr;

	for (const fs::path& source : files) {

		const std::string& extension = source.extension().string();
		fs::path objectPath = getObjectPath(settings, source);
		fs::path depPath = getDependencyPath(settings, source);
		s32 status = 0;
		std::string cmd;
		std::string defines;

		if (!isCompilableFile(source)) {
			continue;
		}

		std::string objectPathString = getPathString(objectPath);

		if (tracker.compilationObjects.contains(objectPathString)) {
			std::cout << DERROR << "Fatal error: Multiple references to object file " << objectPathString << std::endl;
			return false;
		}

		tracker.compilationObjects.insert(objectPathString);

		if (!needsCompilation(settings, tracker, source)) {
			trackDependencies(settings, tracker, depPath, true);
			continue;
		}

		if (extension == ".cpp") {
			std::cout << DINFO << "Compiling C++ source " << source.string() << std::endl;
			flags = &settings.flags.cpp;
			defines += " -D__FFC_LANG_CPP";
		} else if (extension == ".c") {
			std::cout << DINFO << "Compiling C source " << source.string() << std::endl;
			flags = &settings.flags.c;
			defines += " -D__FFC_LANG_C";
		} else {
			std::cout << DINFO << "Compiling ASM source " << source.string() << std::endl;
			flags = &settings.flags.assembly;
			defines += " -D__FFC_LANG_ASM";
		}

		if (isARM9Target(target)) {

			arch = &settings.flags.arm9;
			defines += " -D__FFC_ARCH_NUM=9";

		} else {

			arch = &settings.flags.arm7;
			defines += " -D__FFC_ARCH_NUM=7";

		}

		cmd = settings.executables.gcc + " " + *flags + " " + *arch + " -c " + source.string() + " -o " + objectPathString + " -MMD -MF " + depPath.string() + includeFlags + defines;
		status = std::system(cmd.c_str());

		if (status) {

			std::cout << DWARNING << "Failed with status " << status << std::endl;
			status = 0;
			state = false;

			if (settings.pedantic) {
				return state;
			}

		} else {

			trackDependencies(settings, tracker, depPath, false);

		}

	}

	return state;

}
*/



bool populateCodeTargets(const BuildSettings& settings, Document& root, CodeTargetMap& codeTargets) {

	if (!root["main"].IsObject()) {
		std::cout << DERROR << "Missing JSON object 'main'" << std::endl;
		return false;
	}

	const Value& codeNode = root["main"].GetObject();

	for (auto& v : codeNode.GetObject()) {

		std::string target = v.name.GetString();

		if (target == "default-target") {
			continue;
		}

		CodeTarget codeTarget = getCodeTarget(target);

		if (codeTarget != invalidTarget) {
			RETURN_ON_ERROR(getSourceSet(codeNode, target, settings.sourceDir, codeTargets[codeTarget]))
		} else {
			std::cout << DERROR << "Invalid code target " << target << std::endl;
			return false;
		}

	}

	if (codeNode["default-target"].IsString()) {

		std::string defaultTarget = codeNode["default-target"].GetString();
		CodeTarget codeTarget = getCodeTarget(defaultTarget);

		if (codeTarget != invalidTarget) {
			scanDefaultTarget(codeTargets, codeTarget, settings.sourceDir);
		} else {
			std::cout << DERROR << "Invalid default target " << defaultTarget << std::endl;
			return false;
		}

	}

	return true;

}




bool generateLinkerScripts(const BuildSettings& buildSettings, const PatchSettings& patchSettings, CodeTargetMap& codeTargets, HookSymbols& hookSymbols) {

	if (codeTargets.empty()) {
		std::cout << DERROR << "No input files, cancelling" << std::endl;
		return false;
	}

	for (auto& e : codeTargets) {

		std::set<fs::path> tempSet;

		for (fs::path sourceFile : e.second) {
			tempSet.insert(fs::relative(sourceFile, buildSettings.sourceDir).replace_extension(".o").string());
		}

		e.second.swap(tempSet);

	}

	for (u32 a = 0; a < 2; a++) {

		const std::string& procName = a ? "arm9" : "arm7";
		const std::string& armLinkerFilename = procName + ".x";
		const fs::path& scriptPath = buildSettings.buildDir / armLinkerFilename;
		u32 armStart = a ? patchSettings.arm9.start : patchSettings.arm7.start;
		u32 armEnd = a ? patchSettings.arm9.end : patchSettings.arm7.end;

		if (fs::exists(scriptPath) && fs::is_regular_file(scriptPath)) {
			removeFile(scriptPath, "old script");
		}

		std::string linkerScript = "/* Auto-generated linker script */\n\n";

		if (a && !buildSettings.symbol9File.empty()) {
			linkerScript += "INCLUDE " + buildSettings.symbol9File.string() + "\n";
		} else if (!a && !buildSettings.symbol7File.empty()) {
			linkerScript += "INCLUDE " + buildSettings.symbol7File.string() + "\n";
		}

		linkerScript += "SEARCH_DIR(" + buildSettings.objectDir.string() + ")\n\n";
		linkerScript += "INPUT(\n";

		u32 inputFiles = 0;

		for (const auto& e : codeTargets) {

			if (a != isARM9Target(e.first)) {
				continue;
			}

			u32 i = 0;
			u32 targetInputFiles = e.second.size();
			const std::string& target = getCodeTargetName(e.first);
			linkerScript += "/* " + target + " */\t";

			if (target.size() < 6) {
				linkerScript += "\t";
			}

			for (const fs::path& sourceFile : e.second) {

				linkerScript += sourceFile.string();

				if (i && (i % 4 == 3) && i != targetInputFiles - 1) {
					linkerScript += "\n\t\t\t\t";
				} else if (i != targetInputFiles - 1) {
					linkerScript += " ";
				}

				i++;

			}

			linkerScript += "\n";
			inputFiles += i;

		}

		if (!inputFiles) {
			continue;
		}


		linkerScript += ")\n\n";
		linkerScript += "MEMORY\n{\n";
		linkerScript += "\tldpatch (rwx): ORIGIN = 0x00000000, LENGTH = 1000000\n";
		linkerScript += "\t" + procName + " (rwx): ORIGIN = " + getHexString(armStart) + ", LENGTH = " + std::to_string(armEnd - armStart) + '\n';
		
		linkerScript += "}\n\n";
		linkerScript += "SECTIONS\n{\n";

		static const std::string sections[] = {
			"(.safe.*)", "(.hook.*)", "(.rlnk.*)", "(.text)", "(.text.*)", "(.rodata)", "(.rodata.*)", "(.init_array)", "(.data)", "(.bss)", "(.bss.*)"
		};

		static constexpr u32 sectionCount = 9;
		
		for (const auto& e : codeTargets) {

			if (a != isARM9Target(e.first)) {
				continue;
			}

			const std::string& indent = isBinary(e.first) ? "\t" : "\t\t";

			const std::string& target = getCodeTargetName(e.first);
			linkerScript += "\t.text." + target + " : ALIGN(4) {\n";
			linkerScript += "\t\t. += " + std::to_string(hookSymbols.safeCounts[e.first]) + ";\n";

			for (u32 i = 0; i < sectionCount; i++) {

				for (const fs::path& sourceFile : e.second) {
					linkerScript += "\t\t" + sourceFile.string() + sections[i] + "\n";
				}

			}

			linkerScript += "\t\t. = ALIGN(4);\n\t} >" + target + " AT>ldpatch\n\n";
			linkerScript += "\t.bss." + target + " : ALIGN(4) {\n";

			for (const fs::path& sourceFile : e.second) {
				linkerScript += "\t\t" + sourceFile.string() + sections[9] + "\n";
				linkerScript += "\t\t" + sourceFile.string() + sections[10] + "\n";
			}

			linkerScript += "\t\t. = ALIGN(4); \n\t} >" + target + " AT>ldpatch\n\n";

		}

		HookMap& hooks = hookSymbols.getSymbolMap(a);

		for (auto it = hooks.begin(); it != hooks.end();) {

			if (it->second.hookType != HookType::Replace) {
				it++;
				continue;
			}

			std::stringstream hookAddressStream;
			hookAddressStream << "0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << it->second.hookAddress;
			const std::string& hookAddress = hookAddressStream.str();
			const std::string& section = ".over." + getCodeTargetName(it->second.codeTarget) + "." + hookAddress;

			linkerScript += "\t" + section + " " + hookAddress + " : SUBALIGN(1) {\n\t\t*(" + section + ")\n\t} AT>ldpatch\n\n";

			it = hooks.erase(it);

		}

		linkerScript += "\t/DISCARD/ : {*(.*)}\n";
		linkerScript += "}\n";

		std::ofstream scriptFile(scriptPath, std::ios::out | std::ios::trunc);

		if (!scriptFile.is_open()) {
			std::cout << DERROR << "Failed to open " << procName << " linker script file " << armLinkerFilename << std::endl;
			return false;
		}

		scriptFile.write(linkerScript.c_str(), linkerScript.size());
		scriptFile.close();

		std::cout << DINFO << "Generated linker script " << armLinkerFilename << std::endl;

	}

	return true;

}




bool linkSource(const BuildSettings& settings) {

	std::string libraryImport = settings.useAEABI ? " -lgcc -L" + settings.libraryDir.string() : "";

	for (u32 a = 0; a < 2; a++) {

		const std::string& archName = a ? "arm9" : "arm7";
		const fs::path& armLinkerFilename = settings.buildDir / (archName + ".x");
		const fs::path& elfOutputFilename = settings.buildDir / (archName + ".elf");

		if (!fs::exists(armLinkerFilename) || !fs::is_regular_file(armLinkerFilename)) {
			continue;
		}

		std::cout << DINFO << "Linking " << armLinkerFilename.string() << std::endl;

		std::string cmd = settings.executables.ld + " -T " + armLinkerFilename.string() + " --nmagic" + libraryImport + " -o " + elfOutputFilename.string();
		s32 status = std::system(cmd.c_str());

		if (status) {
			std::cout << DERROR << "Failed to link objects: Linker returned " << status << std::endl;
			return false;
		}

	}

	std::cout << DINFO << "Linking successful" << std::endl;

	return true;

}




bool patchBinaries(const BuildSettings& buildSettings, const PatchSettings& patchSettings, OverlayTable& ovt, const std::vector<Fixup>& fixups) {

	std::cout << DINFO << "Patching arm9.bin" << std::endl;

	for (auto& e : fixups) {

		if (std::holds_alternative<Patch>(e)) {
			std::cout << "Patch: " << getCodeTargetName(std::get<Patch>(e).codeTarget) << ", start=0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << std::get<Patch>(e).ramAddress << ", size=0x" << std::get<Patch>(e).binSize << ", bss=0x" << std::get<Patch>(e).bssSize << std::endl;
		} else {
			std::cout << "Hook: " << getCodeTargetName(std::get<Hook>(e).codeTarget) << ", hook=0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << std::get<Hook>(e).hookAddress << std::endl;
		}

	}

	SectionMap sections;

	for (const auto& e : ovt) {
		sections.emplace(e.first, SectionData{ e.second.start, e.second.start + e.second.size, 0 });
	}

	std::vector<u8> binary;
	CodeTarget currentTarget = invalidTarget;
	ARMBinaryProperties armBinaryProperties{};
	std::vector<u8> safePatch;
	u32 patchStart = 0;


	for (u32 i = 0; i < fixups.size(); i++) {

		const Fixup& fix = fixups[i];
		bool isPatch = std::holds_alternative<Patch>(fix);
		CodeTarget newTarget = isPatch ? std::get<Patch>(fix).codeTarget : std::get<Hook>(fix).codeTarget;

		if (newTarget != currentTarget) {

			std::cout << DINFO << "Patching target " << getCodeTargetName(newTarget) << std::endl;

			if (isBinary(currentTarget)) {

				RETURN_ON_ERROR(saveBinary(buildSettings, currentTarget, armBinaryProperties, binary, false))

			} else if (isOverlay(currentTarget)) {

				RETURN_ON_ERROR(saveOverlay(buildSettings, currentTarget, binary, ovt))

			}

			if (isBinary(newTarget)) {

				RETURN_ON_ERROR(loadBinary(buildSettings, newTarget, armBinaryProperties, binary))
				RETURN_ON_ERROR(addBinarySections(sections, newTarget, armBinaryProperties, binary))

			} else if (isOverlay(newTarget)) {

				RETURN_ON_ERROR(loadOverlay(buildSettings, newTarget, binary))

			} else {

				std::cout << DERROR << "Invalid target detected for " << (isPatch ? "patch" : "hook") << " at address 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << (isPatch ? std::get<Patch>(fix).ramAddress : std::get<Hook>(fix).hookAddress) << std::endl;
				return false;

			}

			for (u32 j = i; j < fixups.size(); j++) {

				const Fixup& peekFix = fixups[j];
				bool peekIsPatch = std::holds_alternative<Patch>(peekFix);
				CodeTarget peekTarget = peekIsPatch ? std::get<Patch>(peekFix).codeTarget : std::get<Hook>(peekFix).codeTarget;

				if (peekTarget != newTarget) {
					break;
				}

				if (peekIsPatch && std::get<Patch>(peekFix).bssSize != noBSS) {
					patchStart = std::get<Patch>(peekFix).ramAddress;
					break;
				}

			}

			safePatch.clear();
			currentTarget = newTarget;

		}

		if (isPatch) {

			const Patch& patch = std::get<Patch>(fix);

			std::vector<u8> data;
			RETURN_ON_ERROR(loadPatch(buildSettings, patch, data))

			if (patch.bssSize == noBSS) {

				write(sections, currentTarget, patch.ramAddress, data, binary);

			} else {

				if (isBinary(currentTarget)) {

					bool arm9 = currentTarget == arm9Target;
					u32 heapRelocation = (arm9 ? patchSettings.arm9.start : patchSettings.arm7.start) + patch.binSize + (patch.bssAlign - patch.binSize % patch.bssAlign) + patch.bssSize;
					u32 totalPatchSize = patch.binSize + patch.bssSize;
					u32 kbs = totalPatchSize / 1024;
					u32 bytes = (totalPatchSize % 1024) * 10 / 1024;

					std::cout << DINFO << "Relocating " << getCodeTargetName(currentTarget) << " heap to 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << heapRelocation << std::dec << ", shrinking by " << kbs << "." << bytes << "KB" << std::endl;

					std::copy(safePatch.begin(), safePatch.end(), data.begin());
					writeWord(sections, currentTarget, arm9 ? patchSettings.arm9.reloc : patchSettings.arm7.reloc, heapRelocation, binary);
					patchBinary(buildSettings, patch, data, armBinaryProperties, binary);

				} else {

					std::cout << DWARNING << "Not yet implemented: Patches for targets other than arm7/arm9" << std::endl;

				}

			}

		} else {

			const Hook& hook = std::get<Hook>(fix);

			u32 hookAddress = hook.hookAddress;
			u32 funcAddress = hook.funcAddress;

			bool hookThumb = hookAddress & 1;
			bool funcThumb = funcAddress & 1;
			hookAddress &= ~1;
			funcAddress &= ~1;

			if (!hookThumb && (hookAddress % 4) == 2) {
				std::cout << DWARNING << "Address 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << " is not a valid ARM address: Did you forget to set the thumb bit?" << std::endl;
				continue;
			}

			std::cout << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << hookAddress << ", " << funcAddress << std::endl;

			switch (hook.hookType) {

				case HookType::Hook:

					if (!hookThumb && !funcThumb) {

						s32 signedOffset = (funcAddress - hookAddress - 8) / 4;
						u32 opcode = COND_AL | ARM_B | (signedOffset & 0xFFFFFF);
						writeWord(sections, currentTarget, hookAddress, opcode, binary);

					} else if (!hookThumb && funcThumb) {

						std::cout << DWARNING << "Error in hook from 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << hook.hookAddress << " to 0x" << hook.funcAddress << ": Cannot create opcode 'b' for ARM -> Thumb transition" << std::endl;
						break;

					} else if (hookThumb && !funcThumb) {

						std::cout << DWARNING << "Error in hook from 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << hook.hookAddress << " to 0x" << hook.funcAddress << ": Cannot create opcode 'b' for Thumb -> ARM transition" << std::endl;
						break;

					} else {

						std::cout << DWARNING << "Error in hook from 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << hook.hookAddress << " to 0x" << hook.funcAddress << ": Cannot create opcode 'b' for Thumb -> Thumb transition" << std::endl;
						break;

					}

					break;

				case HookType::Link:

					if (hookThumb != funcThumb && !isARM9Target(currentTarget)) {
						std::cout << DWARNING << "Cannot create thumb-interworking veneer: blx not supported on armv4" << std::endl;
						break;
					}

					if (!hookThumb && !funcThumb) {

						s32 signedOffset = (funcAddress - hookAddress - 8) / 4;
						u32 opcode = COND_AL | ARM_BL | (signedOffset & 0xFFFFFF);
						writeWord(sections, currentTarget, hookAddress, opcode, binary);

					} else if (!hookThumb && funcThumb) {

						s32 signedOffset = (funcAddress - hookAddress - 8) / 4;
						u32 opcode = ARM_BLX | ((funcAddress % 4 / 2) << 23) | (signedOffset & 0xFFFFFF);
						writeWord(sections, currentTarget, hookAddress, opcode, binary);

					} else if (hookThumb && !funcThumb) {

						s32 signedOffset = (funcAddress - hookAddress - 4) / 2;
						u16 opcode0 = THUMB_BL0 | (signedOffset & 0x3FF800) >> 11;
						u16 opcode1 = THUMB_BLX1 | (signedOffset & 0x7FF);
						writeWord(sections, currentTarget, hookAddress, opcode1 << 16 | opcode0, binary);

					} else {

						s32 signedOffset = (funcAddress - hookAddress - 4) / 2;
						u16 opcode0 = THUMB_BL0 | (signedOffset & 0x3FF800) >> 11;
						u16 opcode1 = THUMB_BL1 | (signedOffset & 0x7FF);
						writeWord(sections, currentTarget, hookAddress, opcode1 << 16 | opcode0, binary);

					}

					break;

				case HookType::Safe:

					if (!hookThumb) {

						if (funcThumb && !isARM9Target(currentTarget)) {
							std::cout << DWARNING << "Cannot create thumb-interworking veneer: blx (immediate) not supported on armv4" << std::endl;
							break;
						}

						u32 safeOffset = safePatch.size();
						s32 signedOffset0 = (patchStart + safeOffset - hookAddress - 8) / 4;
						s32 signedOffset1 = (funcAddress - patchStart - safeOffset - 16) / 4;
						s32 signedOffset2 = (hookAddress - patchStart - safeOffset - 20) / 4;

						u32 hookOpcode = COND_AL | ARM_B | (signedOffset0 & 0xFFFFFF);
						u32 replaceOpcode = readWord(sections, currentTarget, hookAddress, binary);
						checkSafeInstruction(replaceOpcode);

						u32 opcode0 = replaceOpcode;
						u32 opcode1 = COND_AL | ARM_PUSH | 0xD5FFF;
						u32 opcode2 = COND_AL | (funcThumb ? ARM_BLX : ARM_BL) | (signedOffset1 & 0xFFFFFF);
						u32 opcode3 = COND_AL | ARM_POP | 0xD5FFF;
						u32 opcode4 = COND_AL | ARM_B | (signedOffset2 & 0xFFFFFF);

						writeWord(sections, currentTarget, hookAddress, hookOpcode, binary);

						safePatch.resize(safeOffset + 20);
						*reinterpret_cast<u32*>(&safePatch[safeOffset]) = opcode0;
						*reinterpret_cast<u32*>(&safePatch[safeOffset + 4]) = opcode1;
						*reinterpret_cast<u32*>(&safePatch[safeOffset + 8]) = opcode2;
						*reinterpret_cast<u32*>(&safePatch[safeOffset + 12]) = opcode3;
						*reinterpret_cast<u32*>(&safePatch[safeOffset + 16]) = opcode4;

					} else {
						std::cout << DERROR << "Fatal error: Safe hook hooked in Thumb mode" << std::endl;
					}

					break;

				default:
					break;

			}

		}

	}

	if (isBinary(currentTarget)) {

		RETURN_ON_ERROR(saveBinary(buildSettings, currentTarget, armBinaryProperties, binary, false))

	} else if (isOverlay(currentTarget)) {

		RETURN_ON_ERROR(saveOverlay(buildSettings, currentTarget, binary, ovt))

	}

	return true;

}




bool generateFileIDs(const BuildSettings& settings, const DependencyTracker& tracker, const OverlayTable& ovt, const FileIDSymbols& fidSymbols) {

	const fs::path& fidPath = settings.toolchainDir / "internal" / "fid.h";

	if (!jsonChanged(tracker)) {
		return true;
	}

	const std::string& fntFilename = "fnt.bin";

	const fs::path& fntPath = settings.nitroFSDir / fntFilename;
	u32 fntSize = fs::file_size(fntPath);
	std::vector<u8> fnt;

	std::ifstream fntFile(fntPath, std::ios::in | std::ios::binary);

	if (!fntFile.is_open()) {
		std::cout << DERROR << "Failed to open filename table " << fntPath.string() << std::endl;
		return false;
	}

	fnt.resize(fntSize);
	fntFile.read(reinterpret_cast<char*>(fnt.data()), fntSize);
	fntFile.close();

	u16 freeOverlayFileID = 0;

	for (const auto& e : ovt) {
		freeOverlayFileID = std::max(static_cast<u32>(freeOverlayFileID), e.second.fileID + 1);
	}

	NDSDirectory rootDir = NFSFSH::readFntTree(fnt);
	u16 freeFileID = std::max(freeOverlayFileID, NFSFSH::findNextFreeFileID(rootDir));
	u16 freeDirID = NFSFSH::findNextFreeDirID(rootDir);

	NFSFSH::addNewFiles(rootDir, settings.nitroFSDir / "root", freeFileID, freeDirID);

	std::unordered_map<std::string, u16> fids;

	for (const auto& e : fidSymbols) {

		const std::string& symbol = e.first;
		const fs::path& filePath = e.second;

		fids[symbol] = NFSFSH::getFileID(rootDir, fs::relative(filePath, settings.nitroFSDir / "root"));

	}

	std::ofstream fidFile(fidPath, std::ios::out | std::ios::trunc);

	if (!fidFile.is_open()) {
		std::cout << DERROR << "Failed to open FID file " << fidPath.string() << std::endl;
		return false;
	}

	fidFile << "#ifndef FID_H\n#define FID_H\n\n";
	fidFile << "/* Auto-generated File ID symbols */\n";
	fidFile << "#ifndef __FFC_LANG_ASM\n\n";
	fidFile << "\tnamespace FID {\n\n";

	for (const auto& e : fids) {
		fidFile << "\t\tconstexpr unsigned short " << e.first << " = " << e.second << ";\n";
	}

	fidFile << "\n\t};\n";
	fidFile << "\n#endif\n";
	fidFile << "\n#endif  // FID_H";
	fidFile.close();

	return true;

}




void write(const SectionMap& sections, CodeTarget target, u32 address, const std::vector<u8>& data, std::vector<u8>& binary) {

	u32 offset = 0;

	if (!getSectionAddressOffset(sections, target, address, data.size(), offset)) {
		std::cout << DWARNING << "Address 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << address << " not in section " << getCodeTargetName(target) << std::endl;
		return;
	}

	std::copy(data.begin(), data.end(), binary.begin() + offset);

}



void writeHalfword(const SectionMap& sections, CodeTarget target, u32 address, u16 value, std::vector<u8>& binary) {

	u32 offset = 0;

	if (!getSectionAddressOffset(sections, target, address, 2, offset)) {
		std::cout << DWARNING << "Address 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << address << " not in section " << getCodeTargetName(target) << std::endl;
		return;
	}

	*reinterpret_cast<u16*>(&binary[offset]) = value;

}



void writeWord(const SectionMap& sections, CodeTarget target, u32 address, u32 value, std::vector<u8>& binary) {

	u32 offset = 0;

	if (!getSectionAddressOffset(sections, target, address, 4, offset)) {
		std::cout << DWARNING << "Address 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << address << " not in section " << getCodeTargetName(target) << std::endl;
		return;
	}

	*reinterpret_cast<u32*>(&binary[offset]) = value;

}



u32 readWord(const SectionMap& sections, CodeTarget target, u32 address, std::vector<u8>& binary) {

	u32 offset = 0;

	if (!getSectionAddressOffset(sections, target, address, 4, offset)) {
		std::cout << DWARNING << "Address 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << address << " not in section " << getCodeTargetName(target) << std::endl;
		return 0;
	}

	return *reinterpret_cast<u32*>(&binary[offset]);

}



bool fixBinarySections(SectionMap& sections, CodeTarget target, u32 offset) {

	if (!isBinary(target)) {
		std::cout << "Target " << getCodeTargetName(target) << " does not represent a valid ARM binary target" << std::endl;
		return false;
	}

	auto targetIt = sections.equal_range(target);

	for (auto& it = targetIt.first; it != targetIt.second; it++) {

		if (it->second.destination) {
			it->second.destination += offset;
		}

	}

	return true;

}




bool addBinarySections(SectionMap& sections, CodeTarget target, const ARMBinaryProperties& properties, const std::vector<u8>& binary) {

	if (!isBinary(target)) {
		std::cout << "Target " << getCodeTargetName(target) << " does not represent a valid ARM binary target" << std::endl;
		return false;
	}

	sections.emplace(target, SectionData{ properties.offset, properties.offset + properties.autoloadRead, 0 });
	u32 readPtr = properties.autoloadRead;

	for (u32 i = 0; i < (properties.autoloadEnd - properties.autoloadStart) / 12; i++) {

		u32 start = *reinterpret_cast<const u32*>(&binary[properties.autoloadStart + i * 12]);
		u32 size = *reinterpret_cast<const u32*>(&binary[properties.autoloadStart + i * 12 + 4]);

		sections.emplace(target, SectionData{ start, start + size, readPtr });
		readPtr += size;

	}

	return true;

}




bool loadBinary(const BuildSettings& settings, CodeTarget target, ARMBinaryProperties& properties, std::vector<u8>& binary) {

	if (!isBinary(target)) {
		std::cout << "Target " << getCodeTargetName(target) << " does not represent a valid ARM binary target" << std::endl;
		return false;
	}

	fs::path armPath = settings.backupDir / (getCodeTargetName(target) + ".bin");
	u32 armSize = fs::file_size(armPath);

	std::ifstream armFile(armPath, std::ios::in | std::ios::binary);

	if (!armFile.is_open()) {
		std::cout << DERROR << "Failed to open ARM binary file " << armPath.string() << std::endl;
		return false;
	}

	binary.resize(armSize);
	armFile.read(reinterpret_cast<char*>(binary.data()), armSize);
	armFile.close();

	RETURN_ON_ERROR(loadARMBinaryProperties(settings, target, binary, properties))

	return true;

}




void patchBinary(const BuildSettings& settings, const Patch& patchInfo, const std::vector<u8>& patch, const ARMBinaryProperties& properties, std::vector<u8>& binary) {

	u32 patchSize = patch.size();
	binary.resize(binary.size() + patchInfo.binSize + 12);

	std::copy(binary.begin() + properties.autoloadStart, binary.begin() + properties.autoloadEnd, binary.begin() + properties.autoloadStart + patchSize + 12);
	std::copy(binary.begin() + properties.autoloadRead, binary.begin() + properties.autoloadStart, binary.begin() + properties.autoloadRead + patch.size());
	std::copy(patch.begin(), patch.end(), binary.begin() + properties.autoloadRead);

	*reinterpret_cast<u32*>(&binary[properties.autoloadStart + patchSize]) = patchInfo.ramAddress;
	*reinterpret_cast<u32*>(&binary[properties.autoloadStart + patchSize + 4]) = patchInfo.binSize;
	*reinterpret_cast<u32*>(&binary[properties.autoloadStart + patchSize + 8]) = patchInfo.bssSize;

	*reinterpret_cast<u32*>(&binary[properties.moduleParams]) = properties.offset + properties.autoloadStart + patchSize;
	*reinterpret_cast<u32*>(&binary[properties.moduleParams + 4]) = properties.offset + properties.autoloadEnd + patchSize + 12;

}




bool saveBinary(const BuildSettings& settings, CodeTarget target, const ARMBinaryProperties& properties, std::vector<u8>& binary, bool compress) {

	if (!isBinary(target)) {
		std::cout << "Target " << getCodeTargetName(target) << " does not represent a valid ARM binary target" << std::endl;
		return false;
	}

	fs::path armPath = settings.nitroFSDir / (getCodeTargetName(target) + ".bin");
	u32 finalSize = binary.size();

	if (compress) {

		std::cout << DINFO << "Recompressing ARM binary file " << armPath.string() << std::endl;

		u32 blockSize = target == arm9Target ? 0x4000 : 0x400;
		u32 nextCompressableBlock = ((properties.offset + blockSize - 1) / blockSize) * blockSize;

		if (nextCompressableBlock - properties.offset < (blockSize / 4)) {
			nextCompressableBlock += blockSize;
		}

		nextCompressableBlock -= properties.offset;

		if (nextCompressableBlock > binary.size()) {
			std::cout << DERROR << "Cannot compress " << getCodeTargetName(target) << ".bin: Requested offset exceeds " << getCodeTargetName(target) << ".bin size" << std::endl;
			return false;
		}

		std::vector<u8> armc(binary.begin() + nextCompressableBlock, binary.end());

		BLZ::compress(armc);

		finalSize = nextCompressableBlock + armc.size();

		if (finalSize > binary.size()) {
			std::cout << DERROR << "Cannot BLZ compress " << getCodeTargetName(target) << ".bin: Compression non-effective" << std::endl;
			return false;
		}

		std::copy(armc.begin(), armc.end(), binary.begin() + nextCompressableBlock);
		*reinterpret_cast<u32*>(&binary[properties.moduleParams + 0x14]) = finalSize + properties.offset;

	}

	std::ofstream armFile(armPath, std::ios::out | std::ios::binary | std::ios::trunc);

	if (!armFile.is_open()) {
		std::cout << DERROR << "Failed to open " << armPath.string() << std::endl;
		return false;
	}

	armFile.write(reinterpret_cast<const char*>(binary.data()), finalSize);
	armFile.close();

	fs::path headerPath = settings.nitroFSDir / "header.bin";

	if (!fs::exists(headerPath) || !fs::is_regular_file(headerPath)) {
		std::cout << DERROR << "Fatal error: header.bin not found" << std::endl;
		return false;
	}

	std::fstream headerFile(headerPath, std::ios::in | std::ios::out | std::ios::binary);

	if (!headerFile.is_open()) {
		std::cout << DERROR << "Failed to open header.bin" << std::endl;
		return false;
	}

	headerFile.seekp(0x2C + (target != arm9Target) * 0x10);
	headerFile.write(reinterpret_cast<const char*>(&finalSize), 4);
	headerFile.close();

	return true;

}



bool loadOverlay(const BuildSettings& settings, CodeTarget target, std::vector<u8>& binary) {

	if (!isOverlay(target)) {
		std::cout << "Target " << getCodeTargetName(target) << " does not represent a valid overlay target" << std::endl;
		return false;
	}

	u32 ovID = getOverlayID(target);

	const std::string& ovPrefix = "overlay" + getProcessorID(target);
	const fs::path& overlayDir = settings.backupDir / ovPrefix;
	const std::string& overlayFilename = ovPrefix + "_" + std::to_string(ovID) + ".bin";
	const fs::path& overlayPath = overlayDir / overlayFilename;
	u32 overlaySize = fs::file_size(overlayPath);

	std::ifstream overlayFile(overlayPath, std::ios::in | std::ios::binary);

	if (!overlayFile.is_open()) {
		std::cout << DERROR << "Failed to open overlay file " << overlayPath.string() << std::endl;
		return false;
	}

	binary.resize(overlaySize);
	overlayFile.read(reinterpret_cast<char*>(binary.data()), overlaySize);
	overlayFile.close();

	return true;

}



bool saveOverlay(const BuildSettings& settings, CodeTarget target, std::vector<u8>& binary, OverlayTable& ovt) {

	if (!isOverlay(target)) {
		std::cout << "Target " << getCodeTargetName(target) << " does not represent a valid overlay target" << std::endl;
		return false;
	}

	OverlayEntry& entry = ovt[target];

	//if (entry.flags & OverlayEntry::compressFlag) {
	//	std::cout << DINFO << "Compressing overlay " << getCodeTargetName(target) << std::endl;
	//	BLZ::compress(binary);
	//}
	entry.flags &= ~entry.compressFlag;

	u32 ovID = getOverlayID(target);
	std::string ovPrefix = "overlay" + getProcessorID(target);
	fs::path overlayPath = settings.nitroFSDir / ovPrefix / (ovPrefix + "_" + std::to_string(ovID) + ".bin");

	std::cout << DINFO << "Saving overlay " << getCodeTargetName(target) << " to filesystem" << std::endl;

	std::ofstream overlayFile(overlayPath, std::ios::out | std::ios::binary | std::ios::trunc);

	if (!overlayFile.is_open()) {
		std::cout << DERROR << "Failed to open overlay file " << overlayPath.string() << std::endl;
		return false;
	}

	overlayFile.write(reinterpret_cast<const char*>(binary.data()), binary.size());
	overlayFile.close();

	entry.flags = fs::file_size(overlayPath) | (entry.flags & 0xFF000000);

	return true;

}



bool loadOverlayTable(const BuildSettings& settings, OverlayTable& ovt) {

	for (u32 a = 0; a < 2; a++) {

		const fs::path& ovtPath = settings.backupDir / (a ? "arm9ovt.bin" : "arm7ovt.bin");
		u32 ovtSize = fs::file_size(ovtPath);

		std::ifstream ovtFile(ovtPath, std::ios::in | std::ios::binary);

		if (!ovtFile.is_open()) {
			std::cout << "Failed to open overlay table file " << ovtPath.string() << std::endl;
			return false;
		}

		u32 overlays = ovtSize / 32;
		OverlayEntry entry{};

		for (u32 i = 0; i < overlays; i++) {

			ovtFile.read(reinterpret_cast<char*>(&entry), 32);
			ovt[a ? ov9Target(entry.ovID) : ov7Target(entry.ovID)] = entry;

		}

		ovtFile.close();

	}

	return true;

}



bool saveOverlayTable(const BuildSettings& settings, const OverlayTable& ovt) {

	auto ovtIter = ovt.begin();

	for (u32 a = 0; a < 2; a++) {

		const fs::path& ovtPath = settings.nitroFSDir / (a ? "arm9ovt.bin" : "arm7ovt.bin");
		std::ofstream ovtFile(ovtPath, std::ios::out | std::ios::binary | std::ios::trunc);

		if (!ovtFile.is_open()) {
			std::cout << "Failed to open overlay table file " << ovtPath.string() << std::endl;
			return false;
		}

		for (auto& it = ovtIter; it != ovt.end(); it++) {

			if (!a && isARM9Target(it->first)) {
				break;
			}

			ovtFile.write(reinterpret_cast<const char*>(&it->second), 32);

		}

		ovtFile.close();

	}

	return true;

}




bool backupNitroFSFile(const BuildSettings& settings, const std::string& path) {

	fs::path filePath = settings.nitroFSDir / path;
	fs::path fileBackup = settings.backupDir / path;

	if (!fs::exists(fileBackup) || !fs::is_regular_file(fileBackup)) {

		if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
			std::cout << DERROR << "Failed to find NitroFS file " << filePath.string() << std::endl;
			return false;
		}

		bool copied = false;

		try {

			copied = fs::copy_file(filePath, fileBackup);

		} catch (fs::filesystem_error& e) {

			std::cout << DERROR << "Failed to copy NitroFS file " << filePath.string() << " to backup directory: " << e.what() << std::endl;
			return false;

		}

		if (!copied) {
			std::cout << DERROR << "Failed to copy NitroFS file " << filePath.string() << " to backup directory" << std::endl;
			return false;
		}

	}

	return true;

}



bool backupFiles(const BuildSettings& settings, OverlayTable& ovt) {

	const fs::path& nobackupPath = settings.backupDir / ".nobackup";

	if (fs::exists(nobackupPath) && fs::is_regular_file(nobackupPath)) {
		RETURN_ON_ERROR(loadOverlayTable(settings, ovt))
		return true;
	}

	std::cout << DINFO << "Backing up NitroFS files" << std::endl;

	const fs::path& backup9Path = settings.backupDir / "overlay9";
	const fs::path& backup7Path = settings.backupDir / "overlay7";
	const fs::path& backup9cPath = settings.backupDir / "overlay9c";
	const fs::path& backup7cPath = settings.backupDir / "overlay7c";

	std::cout << DINFO << "Backing up ROM header" << std::endl;
	RETURN_ON_ERROR(backupNitroFSFile(settings, "header.bin"))

	std::cout << DINFO << "Backing up overlay tables" << std::endl;
	RETURN_ON_ERROR(backupNitroFSFile(settings, "arm7ovt.bin"))
	RETURN_ON_ERROR(backupNitroFSFile(settings, "arm9ovt.bin"))
	RETURN_ON_ERROR(loadOverlayTable(settings, ovt))

	RETURN_ON_ERROR(createDirectory(backup9Path, "overlay"))
	RETURN_ON_ERROR(createDirectory(backup7Path, "overlay"))
	RETURN_ON_ERROR(createDirectory(backup9cPath, "compressed overlay"))
	RETURN_ON_ERROR(createDirectory(backup7cPath, "compressed overlay"))

	std::cout << DINFO << "Backing up ARM binaries" << std::endl;

	for (u32 a = 0; a < 2; a++) {

		CodeTarget target = a ? arm9Target : arm7Target;
		const std::string& armFilename = getCodeTargetName(target) + ".bin";

		RETURN_ON_ERROR(backupNitroFSFile(settings, armFilename))

		fs::path armPath = settings.backupDir / armFilename;
		u32 armSize = fs::file_size(armPath);

		std::ifstream armFile(armPath, std::ios::in | std::ios::binary);

		if (!armFile.is_open()) {
			std::cout << DERROR << "Failed to open ARM binary file " << armPath.string() << std::endl;
			return false;
		}

		std::vector<u8> binary;
		binary.resize(armSize);
		armFile.read(reinterpret_cast<char*>(binary.data()), armSize);
		armFile.close();

		ARMBinaryProperties properties{};
		RETURN_ON_ERROR(loadARMBinaryProperties(settings, target, binary, properties))

		if (properties.compressedEnd) {

			const std::string& armcBinary = getCodeTargetName(target) + "c.bin";
			fs::path armcPath = settings.backupDir / armcBinary;

			try {
				fs::rename(armPath, armcPath);
			} catch (fs::filesystem_error& e) {
				std::cout << DERROR << "Failed to move ARM binary " << armPath.string() << ": " << e.what() << std::endl;
				return false;
			}

			BLZ::decompress(binary);
			*reinterpret_cast<u32*>(&binary[properties.moduleParams + 0x14]) = 0;
			properties.compressedEnd = 0;

			std::ofstream armdFile(armPath, std::ios::out | std::ios::binary | std::ios::trunc);

			if (!armdFile.is_open()) {
				std::cout << DERROR << "Failed to open decompressed ARM binary file " << armPath.string() << std::endl;
				return false;
			}

			armdFile.write(reinterpret_cast<const char*>(binary.data()), binary.size());
			armdFile.close();

		}

	}

	std::cout << DINFO << "Backing up overlays" << std::endl;

	for (const auto& e : ovt) {

		u32 ovID = e.second.ovID;
		const std::string procID = getProcessorID(e.first);
		const std::string& overlaySubpath = "overlay" + procID + "_" + std::to_string(ovID) + ".bin";

		RETURN_ON_ERROR(backupNitroFSFile(settings, "overlay" + procID + "/" + overlaySubpath))

		if (e.second.flags & OverlayEntry::compressFlag) {

			fs::path overlayPath = (procID == "9" ? backup9Path : backup7Path) / overlaySubpath;
			fs::path overlaycPath = (procID == "9" ? backup9cPath : backup7cPath) / overlaySubpath;

			try {
				fs::rename(overlayPath, overlaycPath);
			} catch (fs::filesystem_error& e) {
				std::cout << DERROR << "Failed to move overlay " << overlayPath.string() << ": " << e.what() << std::endl;
				return false;
			}

			std::ifstream overlaycFile(overlaycPath, std::ios::in | std::ios::binary);

			if (!overlaycFile.is_open()) {
				std::cout << DERROR << "Failed to open compressed overlay file " << overlaycPath.string() << std::endl;
				return false;
			}

			std::vector<u8> binary;
			binary.resize(fs::file_size(overlaycPath));
			overlaycFile.read(reinterpret_cast<char*>(binary.data()), binary.size());

			BLZ::decompress(binary);

			std::ofstream overlaydFile(overlayPath, std::ios::out | std::ios::binary | std::ios::trunc);

			if (!overlaydFile.is_open()) {
				std::cout << DERROR << "Failed to open decompressed overlay file " << overlayPath.string() << std::endl;
				return false;
			}

			overlaydFile.write(reinterpret_cast<const char*>(binary.data()), binary.size());
			overlaydFile.close();

		}

	}

	std::ofstream nobackupFile(nobackupPath);

	if (!nobackupFile.is_open()) {
		std::cout << DERROR << "Fatal error: Failed to secure backup state" << std::endl;
		return false;
	}

	nobackupFile.close();

	return true;

}



bool loadROMHeader(const BuildSettings& settings, std::vector<u8>& header) {

	fs::path headerPath = settings.backupDir / "header.bin";
	u32 headerSize = fs::file_size(headerPath);

	if (headerSize < 256) {
		std::cout << DERROR << "Invalid ROM header file " << headerPath.string() << ": Expected a minimum of 256 bytes, got " << headerSize << " bytes" << std::endl;
		return false;
	}

	std::ifstream headerFile(headerPath, std::ios::in | std::ios::binary);

	if (!headerFile.is_open()) {
		std::cout << DERROR << "Failed to open ROM header file " << headerPath.string() << std::endl;
		return false;
	}

	header.resize(256);
	headerFile.read(reinterpret_cast<char*>(header.data()), 256);
	headerFile.close();

	return true;

}




bool loadPatch(const BuildSettings& settings, const Patch& patch, std::vector<u8>& binary) {

	fs::path elfPath = settings.buildDir / (isARM9Target(patch.codeTarget) ? "arm9.elf" : "arm7.elf");

	if (!fs::exists(elfPath) || !fs::is_regular_file(elfPath)) {
		std::cout << DERROR << "Failed to find " << elfPath.string() << std::endl;
		return false;
	}

	u32 elfFilesize = fs::file_size(elfPath);

	if (elfFilesize < (patch.binOffset + patch.binSize)) {
		std::cout << DERROR << "Patch section out of bounds: Filesize=0x" << std::uppercase << std::hex << elfFilesize << ", offset=0x" << patch.binOffset << ", size=0x" << patch.binSize << std::endl;
		return false;
	}

	std::ifstream elfFile(elfPath, std::ios::in | std::ios::binary);

	if (!elfFile.is_open()) {
		std::cout << DERROR << "Failed to open " << elfPath.string() << std::endl;
		return false;
	}

	binary.resize(patch.binSize);
	elfFile.seekg(patch.binOffset, std::ios::beg);
	elfFile.read(reinterpret_cast<char*>(binary.data()), patch.binSize);
	elfFile.close();

	return true;

}




bool getSectionAddressOffset(const SectionMap& sections, CodeTarget target, u32 address, u32 size, u32& offset) {

	if (!sections.contains(target)) {
		std::cout << DWARNING << "Section " << getCodeTargetName(target) << " neither represents a valid binary nor an overlay" << std::endl;
		return false;
	}

	auto targetIt = sections.equal_range(target);

	for (auto& it = targetIt.first; it != targetIt.second; it++) {

		const SectionData& sdat = it->second;

		if (address >= sdat.start && (address + size) <= sdat.end) {
			offset = address - sdat.start + sdat.destination;
			return true;
		}

	}

	return false;

}




void checkSafeInstruction(u32 opcode) {

	u32 condition = (opcode & 0xF0000000) >> 28;
	u32 code2 = (opcode & 0x0E000000) >> 25;

	if (condition != 0xF) {

		bool bit4 = (opcode & 0x10) >> 4;
		bool bit7 = (opcode & 0x80) >> 7;
		u32 ext47 = (opcode & 0xF0) >> 4;
		bool sbit = (opcode & 0x100000) >> 20;
		u32 code3 = (opcode & 0x1E00000) >> 21;
		u32 reg0 = (opcode & 0xF0000) >> 16;
		u32 reg1 = (opcode & 0xF000) >> 12;

		switch (code2) {

			case 0:

				if (sbit && !bit4) {
					std::cout << DWARNING << "Data processing instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: CPSR non-secured" << std::endl;
				} else if (sbit && !bit7 && bit4) {
					std::cout << DWARNING << "Data processing instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: CPSR non-secured" << std::endl;
				} else if (sbit && ext47 == 0x90 && ((code3 & 0xE) == 0 || (code3 & 0xC) == 4)) {
					std::cout << DWARNING << "Multiply instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: CPSR non-secured" << std::endl;
				} else if (!sbit && code3 == 9 && ext47 == 1) {
					std::cout << DWARNING << "Branch exchange instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " has potential side-effects: Orphaned code block after instruction" << std::endl;
				} else if (!sbit && (code3 & 0xD) == 9 && ext47 == 0) {
					std::cout << DWARNING << "PSR move instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: CPSR non-secured" << std::endl;
				} else if (!sbit && (code3 & 0xC) == 8 && ext47 == 5) {
					std::cout << DWARNING << "Saturating ALU instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: CPSR non-secured" << std::endl;
				} else if (!sbit && code3 == 9 && ext47 == 7) {
					std::cout << DWARNING << "Breakpoint instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: Breakpoint out of place" << std::endl;
				} else if (!sbit && ((code3 == 8 && (ext47 & 0x9) == 8) || (code3 == 9 && (ext47 & 0xB) == 8))) {
					std::cout << DWARNING << "Signed multiply (type 2) instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: CPSR non-secured" << std::endl;
				} else if (bit4 && ext47 > 9 && (reg0 == 0xF || reg1 == 0xF)) {
					std::cout << DWARNING << "Load/Store instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: Broken PC-relative expression" << std::endl;
				} else if (bit4 && !bit7 && (((code3 & 0xC) == 0x1000 && sbit) || ((code3 & 0xC) != 0x1000)) && (reg0 == 0xF || reg1 == 0xF)) {
					std::cout << DWARNING << "Data processing instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: Broken PC-relative expression" << std::endl;
				}

				break;

			case 1:

				if (sbit) {
					std::cout << DWARNING << "Load/Store instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: CPSR non-secured" << std::endl;
				} else if (!sbit && (code3 & 0xD) == 9) {
					std::cout << DWARNING << "PSR move instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: CPSR non-secured" << std::endl;
				}

				break;

			case 2:

				if (reg0 == 0xF || reg1 == 0xF) {
					std::cout << DWARNING << "Load/Store instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: Broken PC-relative expression" << std::endl;
				}

				break;

			case 3:
				break;

			case 4:

				if (opcode & (1 << 15)) {
					std::cout << DWARNING << "Load/Store multiple instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: PC used in register list" << std::endl;
				}

				break;

			case 5:
				std::cout << DWARNING << "Branch instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: Broken branch offset" << std::endl;
				break;

			case 6:
				std::cout << DWARNING << "Coprocessor load/store instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: Broken PC-relative expression" << std::endl;
				break;

			case 7:

				if (reg0 == 0xF || reg1 == 0xF) {
					std::cout << DWARNING << "Coprocessor load/store instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: Broken PC-relative expression" << std::endl;
				}

				break;

		}

	} else {

		if (code2 == 5) {
			std::cout << DWARNING << "Branch instruction 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << opcode << " potentially unsafe: Broken branch offset" << std::endl;
		}

	}

}




bool collectHooks(const BuildSettings& settings, CodeTargetMap& codeTargets, HookSymbols& hookSymbols) {

	std::cout << DINFO << "Collecting hooks" << std::endl;

	for (auto& e : codeTargets) {

		bool arm9 = isARM9Target(e.first);
		std::set<fs::path>& srcPaths = e.second;

		for (const fs::path& srcPath : srcPaths) {

			const fs::path& objPath = getObjectPath(settings, srcPath);
			std::cout << objPath.string() << std::endl;
			if (!fs::exists(objPath) || !fs::is_regular_file(objPath)) {
				std::cout << DERROR << "Fatal error: Failed to find object file " << objPath.string() << std::endl;
				return false;
			}

			std::ifstream objFile(objPath, std::ios::in | std::ios::binary);

			if (!objFile.is_open()) {
				std::cout << DERROR << "Fatal error: Failed to open object file " << objPath.string() << std::endl;
				return false;
			}

			std::vector<u8> data;
			data.resize(fs::file_size(objPath));

			objFile.read(reinterpret_cast<char*>(data.data()), data.size());
			objFile.close();

			std::unordered_map<u32, Hook> hookSections;

			u32 shdr = *reinterpret_cast<u32*>(&data[0x20]);
			u16 shnum = *reinterpret_cast<u16*>(&data[0x30]);
			u16 shstrndx = *reinterpret_cast<u16*>(&data[0x32]);

			u32 shstrtab = *reinterpret_cast<u32*>(&data[shdr + shstrndx * 0x28 + 0x10]);
			u32 symtab = 0;
			u32 symtabsize = 0;
			u32 strtab = 0;

			for (u32 i = 0; i < shnum; i++) {

				u32 shstrtaboffset = *reinterpret_cast<u32*>(&data[shdr + i * 0x28]);
				std::string shname(reinterpret_cast<const char*>(&data[shstrtab + shstrtaboffset]));

				if (shname.starts_with(".hook") || shname.starts_with(".rlnk") || shname.starts_with(".safe") || shname.starts_with(".over")) {

					HookType hookType = HookType::None;
					u32 hookEndIndex = shname.find_first_of('.', 1);
					std::string hookTypename = shname.substr(1, hookEndIndex - 1);

					if (hookTypename == "hook") {
						hookType = HookType::Hook;
					} else if (hookTypename == "rlnk") {
						hookType = HookType::Link;
					} else if (hookTypename == "safe") {
						hookType = HookType::Safe;
					} else if (hookTypename == "over") {
						hookType = HookType::Replace;
					}

					u32 targetEndIndex = shname.find_last_of('.');
					std::string target = shname.substr(hookEndIndex + 1, targetEndIndex - hookEndIndex - 1);
					std::string address = shname.substr(targetEndIndex + 1);

					CodeTarget hookTarget = getCodeTarget(target);

					if (hookTarget == invalidTarget) {
						std::cout << DWARNING << "Invalid hook target " << target << std::endl;
						continue;
					}

					u32 hookAddress;

					try {
						hookAddress = std::stoul(address, nullptr, 16);
					} catch (std::exception&) {
						std::cout << DWARNING << "Invalid hook address " << address << std::endl;
						continue;
					}

					if (hookType == HookType::Safe) {

						if (hookAddress & 1) {
							std::cout << DWARNING << "Cannot make safe hook at 0x" << std::setw(8) << std::setfill('0') << std::uppercase << std::hex << hookAddress << " from Thumb mode" << std::endl;
							continue;
						}

						hookSymbols.incSafe(e.first, 20);

					}

					hookSections[i] = Hook{ hookTarget, hookType, hookAddress, 0xFFFFFFFF };

				}

				if (shname == ".symtab") {
					symtab = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x10]);
					symtabsize = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x14]);
				}

				if (shname == ".strtab") {
					strtab = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x10]);
				}

			}

			if (!symtab) {
				std::cout << "Error while parsing object file " << objPath.string() << ": Missing symbol table" << std::endl;
				return false;
			}

			if (!strtab) {
				std::cout << "Error while parsing object file " << objPath.string() << ": Missing string table" << std::endl;
				return false;
			}

			for (u32 i = 0; i < symtabsize / 0x10; i++) {

				u32 strtaboffset = *reinterpret_cast<u32*>(&data[symtab + i * 0x10]);
				u32 symoffset = *reinterpret_cast<u32*>(&data[symtab + i * 0x10 + 0x4]);
				u16 symsection = *reinterpret_cast<u16*>(&data[symtab + i * 0x10 + 0xE]);
				std::string symname(reinterpret_cast<const char*>(&data[strtab + strtaboffset]));
				
				if (hookSections.contains(symsection) && symoffset < 2 && !symname.empty() && symname[0] != '$') {
					hookSymbols.getSymbolMap(arm9)[symname] = hookSections[symsection];
				}

			}

		}

	}

	for (auto& [s, h] : hookSymbols.hooks9) {
		std::cout << s << ": " << std::hex << h.funcAddress << std::endl;
	}

	return true;

}



bool parseElf(const BuildSettings& settings, HookSymbols& hookSymbols, std::vector<Fixup>& fixups) {

	std::cout << DINFO << "Fixing hook symbol addresses" << std::endl;

	for (u32 a = 0; a < 2; a++) {

		const std::string& elfFilename = a ? "arm9.elf" : "arm7.elf";
		fs::path elfPath = settings.buildDir / elfFilename;

		if (!fs::exists(elfPath) || !fs::is_regular_file(elfPath)) {
			continue;
		}

		std::ifstream elfFile(elfPath, std::ios::in | std::ios::binary);

		if (!elfFile.is_open()) {
			std::cout << DERROR << "Fatal error: Failed to open " << elfFilename << std::endl;
			return false;
		}

		std::vector<u8> data;
		data.resize(fs::file_size(elfPath));

		elfFile.read(reinterpret_cast<char*>(data.data()), data.size());
		elfFile.close();

		u32 shdr = *reinterpret_cast<u32*>(&data[0x20]);
		u16 shnum = *reinterpret_cast<u16*>(&data[0x30]);
		u16 shstrndx = *reinterpret_cast<u16*>(&data[0x32]);

		u32 shstrtab = *reinterpret_cast<u32*>(&data[shdr + shstrndx * 0x28 + 0x10]);
		u32 symtab = 0;
		u32 symtabsize = 0;
		u32 strtab = 0;
		u32 strtabsize = 0;

		HookMap& hooks = hookSymbols.getSymbolMap(a);
		std::map<CodeTarget, Patch> elfBinaries;

		for (u32 i = 0; i < shnum; i++) {

			u32 shstrtaboffset = *reinterpret_cast<u32*>(&data[shdr + i * 0x28]);
			std::string shname(reinterpret_cast<const char*>(&data[shstrtab + shstrtaboffset]));

			if (shname == ".symtab") {

				symtab = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x10]);
				symtabsize = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x14]);

			} else if (shname == ".strtab") {

				strtab = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x10]);
				strtabsize = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x14]);

			} else if (shname.starts_with(".text")) {

				CodeTarget target = getCodeTarget(shname.substr(6));

				if (target == invalidTarget) {
					std::cout << DERROR << "Invalid code target for section " << shname << std::endl;
					return false;
				}

				elfBinaries[target].codeTarget = target;
				elfBinaries[target].ramAddress = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x0C]);
				elfBinaries[target].binOffset = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x10]);
				elfBinaries[target].binSize = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x14]);

			} else if (shname.starts_with(".bss")) {

				CodeTarget target = getCodeTarget(shname.substr(5));

				if (target == invalidTarget) {
					std::cout << DERROR << "Invalid code target for section " << shname << std::endl;
					return false;
				}

				elfBinaries[target].bssSize = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x14]);
				elfBinaries[target].bssAlign = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x20]);

			} else if (shname.starts_with(".over")) {

				u32 subsectionIndex = shname.find_last_of('.');
				CodeTarget target = getCodeTarget(shname.substr(6, shname.find_last_of('.') - 6));

				if (target == invalidTarget) {
					std::cout << DERROR << "Invalid code target for section " << shname << std::endl;
					return false;
				}

				u32 rplcAddress = 0;

				try {
					rplcAddress = std::stoul(shname.substr(subsectionIndex + 1), nullptr, 16);
				} catch (std::exception&) {
					std::cout << DWARNING << "Invalid replace address " << rplcAddress << std::endl;
					continue;
				}

				u32 rplcOffset = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x10]);
				u32 rplcSize = *reinterpret_cast<u32*>(&data[shdr + i * 0x28 + 0x14]);

				fixups.push_back(Patch{ target, rplcAddress, rplcOffset, rplcSize, noBSS, noBSS });

			}

		}


		if (!symtab) {
			std::cout << "Error while parsing " << elfFilename << ": Missing symbol table" << std::endl;
			return false;
		}

		if (!strtab) {
			std::cout << "Error while parsing " << elfFilename << ": Missing string table" << std::endl;
			return false;
		}


		for (const auto& e : elfBinaries) {
			fixups.push_back(e.second);
		}


		std::unordered_map<u32, std::string> hookSyms;
		u32 strindex = 0;

		while (strindex < strtabsize) {

			std::string symname(reinterpret_cast<const char*>(&data[strtab + strindex]));

			if (hooks.contains(symname)) {
				hookSyms[strindex] = symname;
			}

			strindex += symname.size() + 1;

		}

		for (u32 i = 0; i < symtabsize / 0x10; i++) {

			u32 strtaboffset = *reinterpret_cast<u32*>(&data[symtab + i * 0x10]);

			if (hookSyms.contains(strtaboffset)) {

				std::string symname(reinterpret_cast<const char*>(&data[strtab + strtaboffset]));
				u32 symaddress = *reinterpret_cast<u32*>(&data[symtab + i * 0x10 + 0x4]);
				hooks[hookSyms[strtaboffset]].funcAddress = symaddress;

			}

		}

	}

	for (const auto& e : hookSymbols.hooks7) {
		fixups.push_back(e.second);
	}

	for (const auto& e : hookSymbols.hooks9) {
		fixups.push_back(e.second);
	}

	std::sort(fixups.begin(), fixups.end(), [](const Fixup& a, const Fixup& b) {

		CodeTarget ca = invalidTarget;
		CodeTarget cb = invalidTarget;
		bool pa = std::holds_alternative<Patch>(a);
		bool pb = std::holds_alternative<Patch>(b);

		if (pa) {
			ca = std::get<Patch>(a).codeTarget;
		} else {
			ca = std::get<Hook>(a).codeTarget;
		}

		if (pb) {
			cb = std::get<Patch>(b).codeTarget;
		} else {
			cb = std::get<Hook>(b).codeTarget;
		}

		if (ca == cb) {

			if (pa && pb) {

				u32 ba = std::get<Patch>(a).bssSize;
				u32 bb = std::get<Patch>(b).bssSize;

				return ba > bb;

			} else {

				return pb;

			}

		} else {

			return ca < cb;

		}

	});

	return true;

}



bool createObjectDirectories(const BuildSettings& settings) {

	RETURN_ON_ERROR(createDirectory(settings.objectDir, "object"))
		const fs::path& sourcePath = settings.sourceDir;

	for (const fs::path& source : fs::recursive_directory_iterator(sourcePath)) {

		if (fs::is_directory(source)) {

			fs::path objectDir = settings.objectDir / fs::relative(source, settings.sourceDir);

			if (fs::exists(objectDir) && fs::is_directory(objectDir)) {
				continue;
			}

			RETURN_ON_ERROR(createDirectory(objectDir, "object"))

		}

	}

	return true;

}



bool createDependencyDirectories(const BuildSettings& settings) {

	RETURN_ON_ERROR(createDirectory(settings.depsDir, "dependency"))
		const fs::path& sourcePath = settings.sourceDir;

	for (const fs::path& source : fs::recursive_directory_iterator(sourcePath)) {

		if (fs::is_directory(source)) {

			fs::path depsDir = settings.depsDir / fs::relative(source, settings.sourceDir);

			if (fs::exists(depsDir) && fs::is_directory(depsDir)) {
				continue;
			}

			RETURN_ON_ERROR(createDirectory(depsDir, "dependency"))

		}

	}

	return true;

}




void loadDependencies(const BuildSettings& settings, DependencyTracker& tracker) {

	fs::path trackerPath = settings.buildDir / "tracker.bin";
	tracker.jsonLastModifiedTime = timeLastModified(jsonPath);

	if (fs::exists(trackerPath) && fs::is_regular_file(trackerPath) && fs::file_size(trackerPath) > 8) {

		std::ifstream trackerFile(trackerPath, std::ios::in | std::ios::binary);

		if (!trackerFile.is_open()) {
			std::cout << DERROR << "Failed to open tracking file " << trackerPath.string() << std::endl;
			return;
		}

		trackerFile.read(reinterpret_cast<char*>(&tracker.jsonTrackedModifiedTime), 8);

		if (trackerFile.peek() != EOF) {

			while (true) {

				u16 length = 0;
				trackerFile.read(reinterpret_cast<char*>(&length), 2);

				std::string entry;
				entry.resize(length);
				trackerFile.read(&entry[0], length);

				u64 date = 0;
				trackerFile.read(reinterpret_cast<char*>(&date), 8);

				fs::path p(entry);

				if (fs::exists(p) && fs::is_regular_file(p)) {
					tracker.dependencies[entry] = date;
				}

				if (trackerFile.peek() == EOF) {
					break;
				}

			}

		}

		trackerFile.close();

	}

}





void trackDependencies(const BuildSettings& settings, DependencyTracker& tracker, const fs::path& dep, bool old) {

	if (!fs::exists(dep) || !fs::is_regular_file(dep)) {
		std::cout << DWARNING << "Dependency file " << dep.string() << " not generated, disabling dependency tracking for target" << std::endl;
		return;
	}

	std::ifstream depsFile(dep, std::ios::in);

	if (!depsFile.is_open()) {
		std::cout << DERROR << "Failed to open dependency file " << dep.string() << std::endl;
		return;
	}

	std::string line;
	fs::path subDep;
	std::getline(depsFile, line);

	while (std::getline(depsFile, line)) {

		if (line.back() == '\\') {
			line = line.substr(1, line.length() - 3);
		} else {
			line = line.substr(1, line.length() - 1);
		}

		//Fix for the dependency path bug from GCC
		if (u32 bugOffset = line.find("\\:"); bugOffset != std::string::npos) {
			line.replace(bugOffset, 2, ":");
		}

		subDep = fs::path(line);
		std::string subDepString = getPathString(subDep);

		if (old) {

			if (!tracker.oldTrackers.contains(subDepString)) {
				tracker.oldTrackers[subDepString] = tracker.dependencies[subDepString];
			}

		} else {

			if (!tracker.trackers.contains(subDepString)) {
				tracker.trackers[subDepString] = timeLastModified(subDep);
			}

		}

	}

	depsFile.close();

}




void saveDependencies(const BuildSettings& settings, DependencyTracker& tracker) {

	fs::path trackerPath = settings.buildDir / "tracker.bin";

	if (fs::exists(trackerPath) && fs::is_regular_file(trackerPath)) {
		removeFile(trackerPath, "tracker");
	}

	for (auto& e : tracker.oldTrackers) {

		if (!tracker.trackers.contains(e.first)) {
			tracker.trackers[e.first] = e.second;
		}

	}

	std::ofstream trackerFile(trackerPath, std::ios::out | std::ios::binary);

	if (!trackerFile.is_open()) {
		std::cout << DERROR << "Failed to open tracking file " << trackerPath.string() << std::endl;
		return;
	}

	trackerFile.write(reinterpret_cast<const char*>(&tracker.jsonLastModifiedTime), 8);

	for (auto& e : tracker.trackers) {

		std::string path = getPathString(e.first);
		u16 length = path.length();
		u64 date = e.second;
		trackerFile.write(reinterpret_cast<const char*>(&length), 2);
		trackerFile.write(reinterpret_cast<const char*>(&path[0]), length);
		trackerFile.write(reinterpret_cast<const char*>(&date), 8);

	}

	trackerFile.close();

}




bool needsCompilation(const BuildSettings& settings, const DependencyTracker& tracker, const fs::path& source) {

	fs::path depsPath = getDependencyPath(settings, source);
	std::string srcString = getPathString(source);

	if (jsonChanged(tracker)) {
		return true;
	}

	if (!tracker.dependencies.contains(srcString)) {
		return true;
	}

	if (timeLastModified(source) > tracker.dependencies.at(srcString)) {
		return true;
	}


	if (!fs::exists(depsPath) || !fs::is_regular_file(depsPath)) {
		return true;
	}

	std::ifstream depsFile(depsPath, std::ios::in);

	if (!depsFile.is_open()) {
		std::cout << DERROR << "Failed to open dependency file " << depsPath.string() << std::endl;
		return true;
	}

	std::string line;
	fs::path subDep;
	std::getline(depsFile, line);
	std::getline(depsFile, line);

	while (std::getline(depsFile, line)) {

		if (line.back() == '\\') {
			line = line.substr(1, line.length() - 3);
		} else {
			line = line.substr(1, line.length() - 1);
		}

		subDep = fs::path(line);
		std::string subDepString = getPathString(subDep);

		if (!(fs::exists(subDep) && fs::is_regular_file(subDep) && tracker.dependencies.contains(subDepString) && timeLastModified(subDep) == tracker.dependencies.at(subDepString))) {
			depsFile.close();
			return true;
		}

	}

	depsFile.close();

	return false;

}



void deleteUnreferencedObjects(const BuildSettings& settings, const DependencyTracker& tracker) {

	std::vector<fs::path> purgePaths;

	for (const fs::path& object : fs::recursive_directory_iterator(settings.objectDir)) {

		if (fs::is_regular_file(object)) {

			std::string objectString = getPathString(object);

			if (!tracker.compilationObjects.contains(objectString)) {

				purgePaths.push_back(object);
				purgePaths.push_back((settings.depsDir / fs::relative(object, settings.objectDir)).replace_extension(".d"));

			}

		}

	}

	for (const fs::path& object : fs::recursive_directory_iterator(settings.objectDir)) {

		if (fs::is_directory(object) && fs::is_empty(object)) {

			purgePaths.push_back(object);
			purgePaths.push_back(settings.depsDir / fs::relative(object, settings.objectDir));

		}

	}

	for (const fs::path& p : purgePaths) {
		removeFile(p, "orphaned");
	}

}



bool loadARMBinaryProperties(const BuildSettings& settings, CodeTarget target, const std::vector<u8>& binary, ARMBinaryProperties& properties) {

	if (!isBinary(target)) {
		std::cout << "Target " << getCodeTargetName(target) << " does not represent a valid ARM binary target" << std::endl;
		return false;
	}

	std::vector<u8> header;
	RETURN_ON_ERROR(loadROMHeader(settings, header))

	bool arm9 = target == arm9Target;
	u32 armEntry = *reinterpret_cast<const u32*>(&header[0x24 + !arm9 * 0x10]);
	u32 armOffset = *reinterpret_cast<const u32*>(&header[0x28 + !arm9 * 0x10]);
	u32 armSize = binary.size();
	u32 entryOffset = armEntry - armOffset;
	u32 moduleParams = -1;
	u32 a, b, c;

	if (arm9) {

		for (u32 i = entryOffset; i < (armSize - 4) && i < (entryOffset + 0x400); i += 4) {

			a = *reinterpret_cast<const u32*>(&binary[i + 0]);
			b = *reinterpret_cast<const u32*>(&binary[i + 4]);

			if (a == 0xDEC00621 && b == 0x2106C0DE) {
				moduleParams = i - 0x1C;
			}

		}

	} else {

		for (u32 i = entryOffset; i < (armSize - 8) && i < (entryOffset + 0x1A0); i += 4) {

			a = *reinterpret_cast<const u32*>(&binary[i + 0]);
			b = *reinterpret_cast<const u32*>(&binary[i + 4]);
			c = *reinterpret_cast<const u32*>(&binary[i + 8]);

			if (a == 0xE5901000 && b == 0xE5902004 && c == 0xE5903008 && i) {

				a = *reinterpret_cast<const u32*>(&binary[i - 4]);

				if ((a & 0xFFFFF000) == 0xE59F0000) {

					a &= 0xFFF;
					a += i + 4;

					if (a < armSize) {

						a = *reinterpret_cast<const u32*>(&binary[a]);
						a -= armOffset;

						if (a < armSize) {
							moduleParams = a;
						}

					}

				}

			}

		}

	}

	if (moduleParams == -1) {
		std::cout << DERROR << "Failed to find crt0 module params" << std::endl;
		return false;
	}

	properties.offset = armOffset;
	properties.moduleParams = moduleParams;
	properties.autoloadStart = *reinterpret_cast<const u32*>(&binary[moduleParams + 0x0]) - armOffset;
	properties.autoloadEnd = *reinterpret_cast<const u32*>(&binary[moduleParams + 0x4]) - armOffset;
	properties.autoloadRead = *reinterpret_cast<const u32*>(&binary[moduleParams + 0x8]) - armOffset;
	properties.compressedEnd = *reinterpret_cast<const u32*>(&binary[moduleParams + 0x14]);

	if (properties.compressedEnd != 0) {
		properties.compressedEnd -= armOffset;
	}

	return true;

}



bool executePrebuildCommand(const BuildSettings& buildSettings) {

	const std::string& cmd = buildSettings.prebuildCmd;

	if (cmd.empty()) {
		return true;
	}

	std::cout << DINFO << "Executing pre-build step command \"" << cmd << "\"" << std::endl;
	int status = std::system(cmd.c_str());

	if (status) {
		std::cout << DERROR << "Pre-build step returned " << status << std::endl;
		return false;
	}

	return true;

}



bool executePostbuildCommand(const BuildSettings& buildSettings) {

	const std::string& cmd = buildSettings.postbuildCmd;

	if (cmd.empty()) {
		return true;
	}

	std::cout << DINFO << "Executing post-build step command \"" << cmd << "\"" << std::endl;
	int status = std::system(cmd.c_str());

	if (status) {
		std::cout << DERROR << "Post-build step returned " << status << std::endl;
		return false;
	}

	return true;

}



bool removeFile(const fs::path& p, const std::string& name) {

	if (fs::exists(p) && fs::is_regular_file(p)) {

		bool removed = false;

		try {

			removed = fs::remove(p);

		} catch (fs::filesystem_error& e) {

			std::cout << DERROR << "Failed to delete " << name << " file " << p.string() << ": " << e.what() << std::endl;
			return false;

		}

		if (!removed) {
			std::cout << DERROR << "Failed to create " << name << " file " << p.string() << std::endl;
			return false;
		}

	}

	return true;

}




bool removeDirectory(const fs::path& p, const std::string& name) {

	if (fs::exists(p) && fs::is_directory(p)) {

		bool removed = false;

		try {

			removed = fs::remove_all(p);

		} catch (fs::filesystem_error& e) {

			std::cout << DERROR << "Failed to delete " << name << " directory " << p.string() << ": " << e.what() << std::endl;
			return false;

		}

		if (!removed) {
			std::cout << DERROR << "Failed to create " << name << " directory " << p.string() << std::endl;
			return false;
		}

	}

	return true;

}




bool jsonChanged(const DependencyTracker& tracker) {
	return tracker.jsonLastModifiedTime != tracker.jsonTrackedModifiedTime;
}




std::string getHexString(u32 hex) {

	std::stringstream ss;
	ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << hex;
	return ss.str();

}



bool getSourceSet(const Value& v, const std::string& key, const fs::path& sourceDir, std::set<fs::path>& paths) {

	const Value& x = v[key.c_str()];

	if (x.IsArray()) {

		for (auto& e : x.GetArray()) {

			fs::path p = sourceDir / e.GetString();

			if (fs::exists(p)) {

				if (fs::is_directory(p)) {

					for (const fs::path& s : fs::recursive_directory_iterator(p)) {

						if (fs::is_regular_file(s) && isCompilableFile(s)) {
							paths.insert(s);
						}

					}

				} else if (fs::is_regular_file(p)) {

					if (isCompilableFile(p)) {
						paths.insert(p);
					} else {
						std::cout << DWARNING << "Explicit file " << p.string() << " is not compilable, skipping" << std::endl;
					}

				} else {
					std::cout << DWARNING << "Filesystem object " << p.string() << " is neither a file nor a directory, skipping" << std::endl;
				}


			} else {
				std::cout << DERROR << "Unable to find path " << p.string() << std::endl;
				return false;
			}

		}

	} else if (!x.IsNull()) {

		std::cout << DERROR << "Expected type Array for key '" << key << "', got " << jsonGetTypename(x) << std::endl;
		return false;

	}

	return true;

}



void scanDefaultTarget(CodeTargetMap& codeTargets, CodeTarget target, const fs::path& sourceDir) {

	for (const fs::path& p : fs::recursive_directory_iterator(sourceDir)) {

		if (fs::is_regular_file(p)) {

			if (!isCompilableFile(p)) {
				continue;
			}

			bool doAdd = true;

			for (const auto& s : codeTargets) {
				doAdd = doAdd && !s.second.contains(p);
			}

			if (doAdd) {
				codeTargets[target].insert(p);
			}

		}

	}

}



std::string getPathString(const fs::path& p) {

	std::string s = p.string();
	std::replace(s.begin(), s.end(), '\\', '/');
	return s;

}



fs::path getObjectPath(const BuildSettings& settings, const fs::path& src) {
	return (settings.objectDir / fs::relative(src, settings.sourceDir)).replace_extension(".o");
}


fs::path getDependencyPath(const BuildSettings& settings, const fs::path& src) {
	return (settings.depsDir / fs::relative(src, settings.sourceDir)).replace_extension(".d");
}



u64 timeLastModified(const fs::path& p) {
	return std::chrono::duration_cast<std::chrono::milliseconds>(fs::last_write_time(p).time_since_epoch()).count();
}



std::string jsonGetTypename(const Value& v) {

	static const char* kTypeNames[] = { "Null", "False", "True", "Object", "Array", "String", "Number" };
	return kTypeNames[v.GetType()];

}




bool jsonReadBool(const Value& v, const std::string& key, bool& b) {

	const Value& x = v[key.c_str()];

	if (x.IsBool()) {
		b = x.GetBool();
	} else {
		std::cout << DERROR << "Expected type Bool for key '" << key << "', got " << jsonGetTypename(x) << std::endl;
		return false;
	}

	return true;

}




bool jsonReadHex(const Value& v, const std::string& key, u32& h) {

	const Value& x = v[key.c_str()];

	if (x.IsString()) {
		h = std::stoul(x.GetString(), nullptr, 16);
	} else {
		std::cout << DERROR << "Expected type Hex for key '" << key << "', got " << jsonGetTypename(x) << std::endl;
		return false;
	}

	return true;

}



bool jsonReadUnsigned(const Value& v, const std::string& key, u32& h) {

	const Value& x = v[key.c_str()];

	if (x.IsUint()) {
		h = x.GetUint();
	} else {
		std::cout << DERROR << "Expected type Uint for key '" << key << "', got " << jsonGetTypename(x) << std::endl;
		return false;
	}

	return true;

}



bool jsonReadString(const Value& v, const std::string& key, std::string& s) {

	const Value& x = v[key.c_str()];

	if (x.IsString()) {
		s = x.GetString();
	} else {
		std::cout << DERROR << "Expected type String for key '" << key << "', got " << jsonGetTypename(x) << std::endl;
		return false;
	}

	return true;

}


bool jsonReadPath(const Value& v, const std::string& key, fs::path& p, bool forceExist) {

	const Value& x = v[key.c_str()];

	if (x.IsString()) {

		p = fs::absolute(fs::path(x.GetString()));

		if ((!fs::exists(p) || !fs::is_regular_file(p)) && forceExist) {
			std::cout << DERROR << "Failed to find file '" << p.string() << "'" << std::endl;
			return false;
		}

	} else {

		std::cout << DERROR << "Expected type String for key '" << key << "', got " << jsonGetTypename(x) << std::endl;
		return false;

	}

	return true;

}



bool jsonReadDir(const Value& v, const std::string& key, fs::path& p, bool forceExist) {

	const Value& x = v[key.c_str()];

	if (x.IsString()) {
		
		p = fs::absolute(fs::path(x.GetString()));

		if ((!fs::exists(p) || !fs::is_directory(p)) && forceExist) {
			std::cout << DERROR << "Failed to find directory " << p.string() << std::endl;
			return false;
		}

	} else {

		std::cout << DERROR << "Expected type String for key '" << key << "', got " << jsonGetTypename(x) << std::endl;
		return false;

	}

	return true;

}




bool jsonReadDirArray(const Value& v, const std::string& key, std::vector<fs::path>& paths, bool forceExist) {

	const Value& x = v[key.c_str()];

	if (x.IsArray()) {

		for (auto& i : x.GetArray()) {

			fs::path p = fs::absolute((i.GetString()));

			if ((!fs::exists(p) || !fs::is_directory(p)) && forceExist) {
				std::cout << DERROR << "Failed to find directory " << p.string() << std::endl;
				return false;
			}

			paths.push_back(p);

		}

	} else {

		std::cout << DERROR << "Expected type Array for key '" << key << "', got " << jsonGetTypename(x) << std::endl;
		return false;

	}

	return true;

}