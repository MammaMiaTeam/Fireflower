#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <map>

#include "common.h"
#include "crc.h"

#define RKEEP ("KEEP")
#define RADJUST ("ADJUST")
#define RCALC ("CALC")
#define REMPTY ("")
#define AFILE (1)
#define ADIR (2)
#define AVALUE (4)
#define AKEEP (8)
#define AADJUST (16)
#define ACALC (32)
#define ARG(x, y) RuleParams{x, y}
#define FSTREAM_OPEN_CHECK(x) \
			if (!fileStream.is_open()) { \
				std::cout << DERROR << "Failed to open file " << x##Path << std::endl; \
				return -1; \
			}
#define FILESIZE_CHECK(x, y) \
			if (x##Size > y) { \
				std::cout << DERROR << "File size of " << x##Path << " with " << x##Size << " bytes exceeds " << y << " bytes" << std::endl; \
				return -1; \
			}





namespace fs = std::filesystem;

const unsigned oneGB = 1073741824U;



struct BuildRule {
	std::string name;
	std::string arg;
};

struct RuleParams {
	std::string val;
	unsigned type;
};

std::string typeNames[] = {
	"Regular file",
	"Directory",
	"Hex value",
	"KEEP directive",
	"ADJUST directive",
	"CALC directive"
};


struct OverlayEntry {
	unsigned start;
	unsigned end;
	unsigned short fileID;
};


typedef std::vector<unsigned char> NitroROM;




void romCheckBounds(NitroROM& rom, unsigned offset, unsigned size) {

	while (rom.size() < offset + size) {

		if (rom.size() >= oneGB) {

			std::cout << DERROR << "Nitro ROM trying to grow larger than 1GB, aborting" << std::endl;
			rom.clear();
			std::exit(-1);

		}
		else {

			std::cout << DWARNING << "Nitro ROM size specified in header too small, resizing from " << rom.size() << " to " << (rom.size() * 2) << " bytes" << std::endl;
			rom.resize(rom.size() * 2, 0xFF);

		}

	}

}





NDSDirectory buildFntTree(unsigned char* fnt, unsigned dirID, unsigned fntSize) {

	NDSDirectory dir;
	unsigned dirOffset = (dirID & 0xFFF) * 8;
	unsigned subOffset = *reinterpret_cast<unsigned*>(&fnt[dirOffset]);
	dir.firstFileID = *reinterpret_cast<unsigned short*>(&fnt[dirOffset + 4]);
	dir.directoryID = dirID;

	unsigned relOffset = 0;
	unsigned char len = 0;
	std::string name;

	while (subOffset + relOffset < fntSize) {

		len = fnt[subOffset + relOffset];
		relOffset++;

		if (len == 0x80) {
			std::cout << DWARNING << "FNT identifier 0x80 detected (reserved), skipping dir node" << std::endl;
			break;
		} else if (len == 0x00) {
			break;
		} 

		bool isSubdir = len & 0x80;
		len &= 0x7F;

		name = std::string(reinterpret_cast<const char*>(&fnt[subOffset + relOffset]), len);
		relOffset += len;

		if (isSubdir) {

			NDSDirectory subDir = buildFntTree(fnt, *reinterpret_cast<unsigned short*>(&fnt[subOffset + relOffset]), fntSize);
			subDir.dirName = name;
			dir.dirs.push_back(subDir);
			relOffset += 2;

		} else {

			dir.files.push_back(name);

		}

	}

	return dir;

}



unsigned short fntFindNextFreeFileID(const NDSDirectory& dir) {

	unsigned short fileFree = dir.firstFileID + dir.files.size();

	for (unsigned i = 0; i < dir.dirs.size(); i++) {
		fileFree = std::max(fileFree, fntFindNextFreeFileID(dir.dirs[i]));
	}

	return fileFree;

}


unsigned short fntFindNextFreeDirID(const NDSDirectory& dir) {

	unsigned short dirFree = dir.directoryID + 1;
	
	for (unsigned i = 0; i < dir.dirs.size(); i++) {
		dirFree = std::max(dirFree, fntFindNextFreeDirID(dir.dirs[i]));
	}

	return dirFree;

}



unsigned fntDirectoryIndex(NDSDirectory& parent, const std::string& dataDir) {

	for (unsigned i = 0; i < parent.dirs.size(); i++) {

		if (dataDir == parent.dirs[i].dirName) {
			return i;
		}

	}

	return -1;

}



void fntAddNewFiles(NDSDirectory& ndsDir, const fs::path& dataDir, unsigned short& freeFileID, unsigned short& freeDirID) {

	for (const fs::path& p : fs::directory_iterator(dataDir)) {

		if (!fs::is_directory(p)) {
			continue;
		}

		unsigned i = fntDirectoryIndex(ndsDir, p.filename().string());

		if (i == -1) {

			NDSDirectory dir;
			dir.firstFileID = freeFileID;
			dir.directoryID = freeDirID;
			dir.dirName = p.filename().string();
			
			for (const fs::path& sp : fs::directory_iterator(p)) {

				if (fs::is_regular_file(sp)) {

					std::cout << DINFO << "File " << sp.string() << " obtained File ID " << (dir.firstFileID + dir.files.size()) << std::endl;
					dir.files.push_back(sp.filename().string());

				}

			}

			freeFileID += dir.files.size();
			freeDirID++;

			fntAddNewFiles(dir, p, freeFileID, freeDirID);
			
			ndsDir.dirs.push_back(dir);

		} else {

			fntAddNewFiles(ndsDir.dirs[i], p, freeFileID, freeDirID);

		}

	}

}



void fntPrintDirs(const NDSDirectory& dir, const std::string& path) {

	for (unsigned i = 0; i < dir.files.size(); i++) {
		V_PRINT("File ID " << (dir.firstFileID + i) << ": " << path << "\\" << dir.files[i])
	}

	for (unsigned i = 0; i < dir.dirs.size(); i++) {
		fntPrintDirs(dir.dirs[i], path + "\\" + dir.dirs[i].dirName);
	}

}



unsigned fntDirectoryCount(const NDSDirectory& dir) {

	unsigned count = dir.dirs.size();

	for (unsigned i = 0; i < dir.dirs.size(); i++) {
		count += fntDirectoryCount(dir.dirs[i]);
	}

	return count;

}



unsigned fntByteCountFn(const NDSDirectory& dir) {

	unsigned bytes = 0;

	for (unsigned i = 0; i < dir.files.size(); i++) {
		bytes += dir.files[i].length() + 1;
	}

	for (unsigned i = 0; i < dir.dirs.size(); i++) {
		bytes += dir.dirs[i].dirName.length() + 3;
		bytes += fntByteCountFn(dir.dirs[i]);
	}

	bytes++;

	return bytes;

}



unsigned fntByteCountHeader(const NDSDirectory& root) {
	return (fntDirectoryCount(root) + 1) * 8;
}



unsigned fntWriteDirectory(const NDSDirectory& dir, unsigned char* fnt, unsigned offset, unsigned parentID) {

	unsigned* ufnt = reinterpret_cast<unsigned*>(fnt);
	unsigned short* sfnt = reinterpret_cast<unsigned short*>(fnt);
	ufnt[(dir.directoryID & 0xFFF) * 2] = offset;
	sfnt[(dir.directoryID & 0xFFF) * 4 + 2] = dir.firstFileID;
	sfnt[(dir.directoryID & 0xFFF) * 4 + 3] = parentID;

	for (unsigned i = 0; i < dir.files.size(); i++) {

		const std::string& filename = dir.files[i];

		fnt[offset] = filename.length();
		filename.copy(reinterpret_cast<char*>(&fnt[offset + 1]), filename.length());
		offset += filename.length() + 1;

	}

	for (unsigned i = 0; i < dir.dirs.size(); i++) {

		const NDSDirectory& subdir = dir.dirs[i];
		const std::string& dirname = subdir.dirName;

		fnt[offset] = dirname.length() + 0x80;
		dirname.copy(reinterpret_cast<char*>(&fnt[offset + 1]), dirname.length());
		fnt[offset + dirname.length() + 1] = subdir.directoryID & 0xFF;
		fnt[offset + dirname.length() + 2] = (subdir.directoryID & 0xFF00) >> 8;
		offset += dirname.length() + 3;

	}

	fnt[offset] = 0x00;
	offset++;

	for (unsigned i = 0; i < dir.dirs.size(); i++) {
		offset = fntWriteDirectory(dir.dirs[i], fnt, offset, dir.directoryID);
	}

	return offset;

}




void fntRebuild(NitroROM& rom, unsigned fntOffset, const NDSDirectory& root, unsigned& size) {

	unsigned fntHeaderSize = fntByteCountHeader(root);
	unsigned fntFnSize = fntByteCountFn(root);
	size = fntHeaderSize + fntFnSize;

	romCheckBounds(rom, fntOffset, size);
	fntWriteDirectory(root, &rom[fntOffset], fntHeaderSize, fntHeaderSize / 8);

}



unsigned alignAddress(unsigned address, unsigned align) {
	return ((address + align - 1) & ~(align - 1));
}



unsigned alignAndClear(NitroROM& rom, unsigned address, unsigned align) {

	unsigned alignedAddress = alignAddress(address, align);
	romCheckBounds(rom, alignedAddress, 4);
	std::fill(&rom[address], &rom[alignedAddress], 0);

	return alignedAddress;

}




void nfsAddAndLink(NitroROM& rom, unsigned fatOffset, const NDSDirectory& dir, const fs::path& p, unsigned& romOffset) {

	unsigned short dirFileID = dir.firstFileID;

	for (unsigned i = 0; i < dir.files.size(); i++) {

		fs::path filePath(p.string() + '\\' + dir.files[i]);
		unsigned fileSize = fs::file_size(filePath);

		if (fileSize > oneGB) {

			std::cout << DWARNING << "File size of " << filePath.string() << " with " << fileSize << " bytes exceeds 1GB, skipping" << std::endl;
			dirFileID++;
			continue;

		}

		std::ifstream fileStream(filePath, std::ios::binary | std::ios::in);

		if (!fileStream.is_open()) {

			std::cout << DWARNING << "Failed to open file " << filePath.string() << ", skipping" << std::endl;
			dirFileID++;
			continue;

		}

		romCheckBounds(rom, romOffset, fileSize);
		fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), fileSize);
		fileStream.close();

		unsigned* fatPtr = reinterpret_cast<unsigned*>(&rom[fatOffset]);
		fatPtr[dirFileID * 2] = romOffset;
		fatPtr[dirFileID * 2 + 1] = romOffset + fileSize;

		V_PRINT("Added and linked " << filePath.string() << " (File ID " << dirFileID << ") to FAT")

		romOffset += fileSize;
		romOffset = alignAddress(romOffset, 4);
		dirFileID++;

	}

	for (unsigned i = 0; i < dir.dirs.size(); i++) {
		nfsAddAndLink(rom, fatOffset, dir.dirs[i], p.string() + '\\' + dir.dirs[i].dirName, romOffset);
	}

}



void fntGenRootDir(NDSDirectory& rootDir, const fs::path& rootPath, unsigned short fid) {

	rootDir.firstFileID = fid;
	rootDir.directoryID = 0xF000;
	rootDir.dirName = "";

	for (const fs::path& p : fs::directory_iterator(rootPath)) {

		if (fs::is_regular_file(p)) {
			rootDir.files.push_back(p.filename().string());
		}

	}

}






int main(int argc, char** argv){

	if (argc != 3) {
		std::cout << "Invalid arguments. Call with .\\nds-build.exe <build_rules> <nds_out>" << std::endl;
		return -1;
	}

	fs::path buildRulePath(argv[1]);
	fs::path ndsOutputPath(argv[2]);

	if (!fs::exists(buildRulePath) || !fs::is_regular_file(buildRulePath)) {
		std::cout << DERROR << "Build rule file " << buildRulePath.string() << " is not a valid file" << std::endl;
		return -1;
	}

	if (fs::exists(ndsOutputPath) && fs::is_regular_file(ndsOutputPath)) {
		std::cout << DERROR << "NDS output file " << ndsOutputPath.string() << " already exists" << std::endl;
		return -1;
	}

	std::ifstream buildRuleFile;
	buildRuleFile.open(buildRulePath);

	if (!buildRuleFile.is_open()) {
		std::cout << DERROR << "Failed to open file " << buildRulePath.string() << std::endl;
		return -1;
	}

	std::cout << DINFO << "Reading build rules from " << buildRulePath.string() << std::endl;

	std::stringstream ruleStream;
	std::string ruleLine;
	std::vector<BuildRule> buildRules;
	BuildRule currentRule;

	while (buildRuleFile.good()) {

		std::getline(buildRuleFile, ruleLine);
		ruleStream << ruleLine;
		ruleStream >> currentRule.name >> currentRule.arg;

		if (!currentRule.name.empty() && !currentRule.arg.empty()) {
			buildRules.push_back(currentRule);
		}

		ruleStream.clear();
		currentRule.name.clear();
		currentRule.arg.clear();

	}

	buildRuleFile.close();

	std::unordered_map<std::string, RuleParams> finalRules;
	finalRules["rom_header"]	= ARG(REMPTY, AFILE);
	finalRules["arm9_entry"]	= ARG(RKEEP, AVALUE | AKEEP);
	finalRules["arm9_load"]		= ARG(RKEEP, AVALUE | AKEEP);
	finalRules["arm7_entry"]	= ARG(RKEEP, AVALUE | AKEEP);
	finalRules["arm7_load"]		= ARG(RKEEP, AVALUE | AKEEP);
	finalRules["fnt"]			= ARG(REMPTY, AFILE);
	finalRules["file_mode"]		= ARG(RADJUST, AKEEP | AADJUST | ACALC);
	finalRules["arm9"]			= ARG(REMPTY, AFILE);
	finalRules["arm7"]			= ARG(REMPTY, AFILE);
	finalRules["arm9ovt"]		= ARG(REMPTY, AFILE);
	finalRules["arm7ovt"]		= ARG(REMPTY, AFILE);
	finalRules["icon"]			= ARG(REMPTY, AFILE);
	finalRules["rsa_sig"]		= ARG(REMPTY, AFILE);
	finalRules["data"]			= ARG(REMPTY, ADIR);
	finalRules["ovt_repl_flag"]	= ARG(REMPTY, AVALUE);
	finalRules["ov9"]			= ARG(REMPTY, ADIR);
	finalRules["ov7"]			= ARG(REMPTY, ADIR);

	std::cout << DINFO << "Parsing build rules" << std::endl;

	for (unsigned i = 0; i < buildRules.size(); i++) {

		BuildRule& rule = buildRules[i];

		if (finalRules.find(rule.name) == finalRules.end()) {
			std::cout << DWARNING << "Unknown rule '" << rule.name << "'" << std::endl;
			continue;
		}

		RuleParams& params = finalRules[rule.name];


		if ((params.type & AFILE) && fs::exists(rule.arg) && fs::is_regular_file(rule.arg)) {
			finalRules[rule.name].val = rule.arg;
			finalRules[rule.name].type = AFILE;
			continue;
		}

		if ((params.type & ADIR) && fs::exists(rule.arg) && fs::is_directory(rule.arg)) {
			finalRules[rule.name].val = rule.arg;
			finalRules[rule.name].type = ADIR;
			continue;
		}

		if ((params.type & AKEEP) && rule.arg == RKEEP) {
			finalRules[rule.name].val = rule.arg;
			finalRules[rule.name].type = AKEEP;
			continue;
		}

		if ((params.type & ACALC) && rule.arg == RCALC) {
			finalRules[rule.name].val = rule.arg;
			finalRules[rule.name].type = ACALC;
			continue;
		}

		if ((params.type & AADJUST) && rule.arg == RADJUST) {
			finalRules[rule.name].val = rule.arg;
			finalRules[rule.name].type = AADJUST;
			continue;
		}

		if (params.type & AVALUE) {

			unsigned value = -1;

			try {

				value = std::stoul(rule.arg, nullptr, 16);

				if (value == -1) {
					throw std::runtime_error("No conversion performed");
				}

			} catch (std::exception&) {

				std::cout << DERROR << "Failed to read value " << rule.arg << " from rule '" << rule.name << "'" << std::endl;
				return -1;

			}

			std::stringstream ss;
			std::string sValue = "0x";

			ss << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << value;
			ss >> sValue;

			finalRules[rule.name].val = sValue;
			finalRules[rule.name].type = AVALUE;
			continue;

		}

		std::cout << DERROR << "Unable to parse argument " << rule.arg << " for rule '" << rule.name << "'" << std::endl;
		std::cout << DINDENT << rule.name << " must be one of the following:" << std::endl;

		for (unsigned i = 0; i < 6; i++) {

			unsigned bit = 1 << i;

			if (params.type & bit) {
				std::cout << "\t- " << typeNames[i] << std::endl;
			}

		}

		return -1;

	}

	for (auto& rule : finalRules) {
		
		if (rule.second.val == REMPTY) {
			std::cout << DERROR << "Missing value for rule '" << rule.first << "'" << std::endl;
			return -1;
		}

	}



	std::cout << DINFO << "Starting rebuild with the following parameters:" << std::endl;

	for (auto& rule : finalRules) {
		std::cout << DINDENT << "\t- " << rule.first << ": " << rule.second.val << std::endl;
	}



	NitroROM rom;
	NDSDirectory rootDir;

	std::ifstream fileStream;
	std::map<unsigned, OverlayEntry> ov7Entries;
	std::map<unsigned, OverlayEntry> ov9Entries;

	unsigned char* romHeader = nullptr;
	unsigned short freeOvFileID = 0;
	unsigned short freeFileID = 0;
	unsigned char ovUpdateID = static_cast<unsigned char>(std::stoul(finalRules["ovt_repl_flag"].val, nullptr, 16));
	unsigned ovt9Offset, ovt7Offset, arm9Offset, arm7Offset, fntOffset, iconOffset, fatOffset;
	unsigned romHeaderSize, fntSize, ovt7Size, ovt9Size, fatSize, arm7Size, arm9Size, romOffset, iconSize, rsaSize, dataSize;

	fs::path romHeaderPath(finalRules["rom_header"].val);
	fs::path fntPath(finalRules["fnt"].val);
	fs::path rootPath(finalRules["data"].val);
	fs::path ovt7Path(finalRules["arm7ovt"].val);
	fs::path ovt9Path(finalRules["arm9ovt"].val);
	fs::path arm7Path(finalRules["arm7"].val);
	fs::path arm9Path(finalRules["arm9"].val);
	fs::path ov7Path(finalRules["ov7"].val);
	fs::path ov9Path(finalRules["ov9"].val);
	fs::path iconPath(finalRules["icon"].val);
	fs::path rsaPath(finalRules["rsa_sig"].val);
	fs::path dataPath;



	std::cout << DINFO << "Reading ROM header" << std::endl;



	romHeaderSize = fs::file_size(romHeaderPath);

	if (romHeaderSize != 0x200 && romHeaderSize != 0x4000) {
		std::cout << DERROR << "Invalid size of ROM header: Must be 0x200 or 0x400" << std::endl;
		return -1;
	}

	fileStream.open(romHeaderPath, std::ios::binary | std::ios::in);

	FSTREAM_OPEN_CHECK(romHeader)

	romHeader = new unsigned char[0x4000];
	fileStream.read(reinterpret_cast<char*>(romHeader), romHeaderSize);
	fileStream.close();

	if (romHeaderSize == 0x200) {
		std::fill(romHeader + 0x200, romHeader + 0x4000, 0);
	}

	if (romHeader[20] > 13) {
		std::cout << DERROR << "Final ROM size in header exceeds 1GB" << std::endl;
		delete[] romHeader;
		return -1;
	}



	std::cout << DINFO << "Creating ROM from ROM header" << std::endl;



	rom.resize(0x20000 << romHeader[20], 0xFF);
	romOffset = 0;
	std::memcpy(rom.data(), romHeader, 0x4000);
	romOffset += 0x4000;



	std::cout << DINFO << "Adding ARM9 binary " << arm9Path.string() << std::endl;



	arm9Size = fs::file_size(arm9Path);
	FILESIZE_CHECK(arm9, 0x3BFE00)

	fileStream.open(arm9Path, std::ios::binary | std::ios::in);
	FSTREAM_OPEN_CHECK(arm9)
	romCheckBounds(rom, romOffset, arm9Size);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), arm9Size);
	fileStream.close();

	arm9Offset = romOffset;
	romOffset += arm9Size;
	romOffset = std::max(0x8000U, romOffset);



	std::cout << DINFO << "Adding ARM9 Overlay Table " << ovt9Path.string() << std::endl;



	ovt9Size = fs::file_size(ovt9Path);
	FILESIZE_CHECK(ovt9, oneGB)

	if (ovt9Size % 0x20) {
		std::cout << DERROR << "File " << ovt9Path.string() << " does not represent a valid ARM9 Overlay Table: Entries must have a size of 0x20 bytes" << std::endl;
		return -1;
	}

	if (ovt9Size) {
		romOffset = alignAddress(romOffset, 16);
	} else {
		romOffset = alignAddress(romOffset, 4);
	}

	romCheckBounds(rom, romOffset, 4);
	ovt9Offset = romOffset;

	if (ovt9Size) {

		fileStream.open(ovt9Path, std::ios::binary | std::ios::in);
		FSTREAM_OPEN_CHECK(ovt9)
		romCheckBounds(rom, romOffset, ovt9Size);
		fileStream.read(reinterpret_cast<char*>(&rom[ovt9Offset]), ovt9Size);
		fileStream.close();

		for (unsigned i = 0; i < ovt9Size / 32; i++) {

			OverlayEntry e = { 0, 0, -1 };

			if (rom[ovt9Offset + i * 32 + 31] != ovUpdateID){

				unsigned short fid = *reinterpret_cast<unsigned short*>(&rom[ovt9Offset + i * 32 + 24]);
				freeOvFileID = std::max(freeOvFileID + 0, fid + 1);
				e.fileID = fid;

			}
			
			ov9Entries[*reinterpret_cast<unsigned*>(&rom[ovt9Offset + i * 32])] = e;

		}


	}

	romOffset += ovt9Size;



	std::cout << DINFO << "Adding ARM9 Overlay files" << std::endl;



	for (const auto& e : ov9Entries) {

		unsigned ovID = e.first;
		dataPath = ov9Path.string() + "\\overlay9_" + std::to_string(ovID) + ".bin";

		if (fs::exists(dataPath) && fs::is_regular_file(dataPath)) {

			dataSize = fs::file_size(dataPath);
			FILESIZE_CHECK(data, oneGB)

			fileStream.open(dataPath, std::ios::binary | std::ios::in);
			FSTREAM_OPEN_CHECK(data)
			romCheckBounds(rom, romOffset, dataSize);
			fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), dataSize);
			fileStream.close();

			ov9Entries[ovID].start = romOffset;
			ov9Entries[ovID].end = romOffset + dataSize;

			romOffset += dataSize;

			V_PRINT("Added " << dataPath.string())

		} else {

			std::cout << DERROR << "Could not find ARM9 Overlay file " << ovID << ": Filename must be formatted as overlay9_x where x represents the Overlay ID" << std::endl;
			return -1;

		}

	}

	romOffset = alignAddress(romOffset, 512);



	std::cout << DINFO << "Adding ARM7 binary " << arm7Path.string() << std::endl;



	arm7Size = fs::file_size(arm7Path);
	FILESIZE_CHECK(arm7, 0x3BFE00)

	fileStream.open(arm7Path, std::ios::binary | std::ios::in);
	FSTREAM_OPEN_CHECK(arm7)
	romCheckBounds(rom, romOffset, arm7Size);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), arm7Size);
	fileStream.close();

	arm7Offset = romOffset;
	romOffset += arm7Size;
	romOffset = alignAddress(romOffset, 4);



	std::cout << DINFO << "Adding ARM7 Overlay Table " << ovt7Path.string() << std::endl;



	ovt7Size = fs::file_size(ovt7Path);
	FILESIZE_CHECK(ovt7, oneGB)

	if (ovt7Size % 0x20) {
		std::cout << DERROR << "File " << ovt7Path.string() << " does not represent a valid ARM7 Overlay Table: Entries must have a size of 0x20 bytes" << std::endl;
		return -1;
	}

	if (ovt7Size) {
		romOffset = alignAddress(romOffset, 16);
	} else {
		romOffset = alignAddress(romOffset, 4);
	}

	romCheckBounds(rom, romOffset, 4);
	ovt7Offset = romOffset;

	if (ovt7Size) {

		fileStream.open(ovt7Path, std::ios::binary | std::ios::in);
		FSTREAM_OPEN_CHECK(ovt7)
		romCheckBounds(rom, romOffset, ovt7Size);
		fileStream.read(reinterpret_cast<char*>(&rom[ovt7Offset]), ovt7Size);
		fileStream.close();

		for (unsigned i = 0; i < ovt7Size / 32; i++) {

			OverlayEntry e = { 0, 0, -1 };

			if (rom[ovt7Offset + i * 32 + 31] != ovUpdateID) {

				unsigned short fid = *reinterpret_cast<unsigned short*>(&rom[ovt7Offset + i * 32 + 24]);
				freeOvFileID = std::max(freeOvFileID + 0, fid + 1);
				e.fileID = fid;

			}

			ov7Entries[*reinterpret_cast<unsigned*>(&rom[ovt7Offset + i * 32])] = e;

		}


	}

	romOffset += ovt7Size;



	std::cout << DINFO << "Adding ARM7 Overlay files" << std::endl;



	for (const auto& e : ov7Entries) {

		unsigned ovID = e.first;
		dataPath = ov7Path.string() + "\\overlay7_" + std::to_string(ovID) + ".bin";

		if (fs::exists(dataPath) && fs::is_regular_file(dataPath)) {

			dataSize = fs::file_size(dataPath);
			FILESIZE_CHECK(data, oneGB)

			fileStream.open(dataPath, std::ios::binary | std::ios::in);
			FSTREAM_OPEN_CHECK(data)
			romCheckBounds(rom, romOffset, dataSize);
			fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), dataSize);
			fileStream.close();

			ov7Entries[ovID].start = romOffset;
			ov7Entries[ovID].end = romOffset + dataSize;

			romOffset += dataSize;

			V_PRINT("Added " << dataPath.string())

		} else {

			std::cout << DERROR << "Could not find ARM7 Overlay file " << ovID << ": Filename must be formatted as overlay7_x where x represents the Overlay ID" << std::endl;
			return -1;

		}

	}

	romOffset = alignAddress(romOffset, 4);



	std::cout << DINFO << "Entering file mode " << finalRules["file_mode"].val << std::endl;



	switch (finalRules["file_mode"].type) {

		case AADJUST:
			{



				std::cout << DINFO << "Reading File Name Table " << fntPath.string() << std::endl;



				fntSize = fs::file_size(fntPath);
				FILESIZE_CHECK(fnt, oneGB)

				fileStream.open(fntPath, std::ios::binary | std::ios::in);
				FSTREAM_OPEN_CHECK(fnt)
				romCheckBounds(rom, romOffset, fntSize);
				fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), fntSize);
				fileStream.close();



				std::cout << DINFO << "Building FNT directory tree" << std::endl;



				rootDir = buildFntTree(&rom[romOffset], 0xF000, fntSize);
				freeFileID = std::max(freeOvFileID, fntFindNextFreeFileID(rootDir));

				unsigned short freeDirID = fntFindNextFreeDirID(rootDir);



				std::cout << DINFO << "Assigning file IDs to Overlays" << std::endl;



				for (unsigned i = 0; i < ovt9Size / 32; i++) {

					if (rom[ovt9Offset + i * 32 + 31] == ovUpdateID) {

						unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt9Offset + i * 32]);
						rom[ovt9Offset + i * 32 + 24] = freeFileID & 0x00FF;
						rom[ovt9Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
						rom[ovt9Offset + i * 32 + 26] = 0;
						rom[ovt9Offset + i * 32 + 27] = 0;
						rom[ovt9Offset + i * 32 + 31] = 3;
						ov9Entries[ovID].fileID = freeFileID;
						std::cout << DINFO << "ARM9 Overlay " << ovID << " obtained file ID " << freeFileID << std::endl;
						freeFileID++;

					}

				}

				for (unsigned i = 0; i < ovt7Size / 32; i++) {

					if (rom[ovt7Offset + i * 32 + 31] == ovUpdateID) {

						unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt7Offset + i * 32]);
						rom[ovt7Offset + i * 32 + 24] = freeFileID & 0x00FF;
						rom[ovt7Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
						rom[ovt7Offset + i * 32 + 26] = 0;
						rom[ovt7Offset + i * 32 + 27] = 0;
						rom[ovt7Offset + i * 32 + 31] = 3;
						ov7Entries[ovID].fileID = freeFileID;
						std::cout << DINFO << "ARM7 Overlay " << ovID << " obtained file ID " << freeFileID << std::endl;
						freeFileID++;

					}

				}

				fntAddNewFiles(rootDir, rootPath, freeFileID, freeDirID);
				fntRebuild(rom, romOffset, rootDir, fntSize);
				fntOffset = romOffset;

				romOffset += fntSize;
				romOffset = alignAddress(romOffset, 4);

				fntPrintDirs(rootDir, rootPath.string());

			}
			break;



		case ACALC:
			{

				unsigned short freeDirID = fntFindNextFreeDirID(rootDir);
				freeFileID = freeOvFileID;



				std::cout << DINFO << "Assigning file IDs to Overlays" << std::endl;



				for (unsigned i = 0; i < ovt9Size / 32; i++) {

					if (rom[ovt9Offset + i * 32 + 31] == ovUpdateID) {

						unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt9Offset + i * 32]);
						rom[ovt9Offset + i * 32 + 24] = freeFileID & 0x00FF;
						rom[ovt9Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
						rom[ovt9Offset + i * 32 + 26] = 0;
						rom[ovt9Offset + i * 32 + 27] = 0;
						rom[ovt9Offset + i * 32 + 31] = 3;
						ov9Entries[ovID].fileID = freeFileID;
						std::cout << DINFO << "ARM9 Overlay " << ovID << " obtained file ID " << freeFileID << std::endl;
						freeFileID++;

					}

				}

				for (unsigned i = 0; i < ovt7Size / 32; i++) {

					if (rom[ovt7Offset + i * 32 + 31] == ovUpdateID) {

						unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt7Offset + i * 32]);
						rom[ovt7Offset + i * 32 + 24] = freeFileID & 0x00FF;
						rom[ovt7Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
						rom[ovt7Offset + i * 32 + 26] = 0;
						rom[ovt7Offset + i * 32 + 27] = 0;
						rom[ovt7Offset + i * 32 + 31] = 3;
						ov7Entries[ovID].fileID = freeFileID;
						std::cout << DINFO << "ARM7 Overlay " << ovID << " obtained file ID " << freeFileID << std::endl;
						freeFileID++;

					}

				}



				std::cout << DINFO << "Building FNT directory tree" << std::endl;



				fntGenRootDir(rootDir, rootPath, freeFileID);
				freeFileID += rootDir.files.size();
				fntAddNewFiles(rootDir, rootPath, freeFileID, freeDirID);

				fntOffset = romOffset;
				fntRebuild(rom, fntOffset, rootDir, fntSize);

				romOffset += fntSize;
				romOffset = alignAddress(romOffset, 4);

				fntPrintDirs(rootDir, rootPath.string());

			}
			break;



		case AKEEP:
			{



				std::cout << DINFO << "Reading File Name Table " << fntPath.string() << std::endl;



				fntSize = fs::file_size(fntPath);
				FILESIZE_CHECK(fnt, oneGB)

				fileStream.open(fntPath, std::ios::binary | std::ios::in);
				FSTREAM_OPEN_CHECK(fnt)
				romCheckBounds(rom, romOffset, fntSize);
				fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), fntSize);
				fileStream.close();



				std::cout << DINFO << "Extracting FNT directory tree" << std::endl;



				rootDir = buildFntTree(&rom[romOffset], 0xF000, fntSize);
				freeFileID = std::max(freeOvFileID, fntFindNextFreeFileID(rootDir));
				fntOffset = romOffset;

				romOffset += fntSize;
				romOffset = alignAddress(romOffset, 4);



				std::cout << DINFO << "Assigning file IDs to Overlays" << std::endl;



				for (unsigned i = 0; i < ovt9Size / 32; i++) {

					if (rom[ovt9Offset + i * 32 + 31] == ovUpdateID) {

						unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt9Offset + i * 32]);
						rom[ovt9Offset + i * 32 + 24] = freeFileID & 0x00FF;
						rom[ovt9Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
						rom[ovt9Offset + i * 32 + 26] = 0;
						rom[ovt9Offset + i * 32 + 27] = 0;
						rom[ovt9Offset + i * 32 + 31] = 3;
						ov9Entries[ovID].fileID = freeFileID;
						std::cout << DINFO << "ARM9 Overlay " << ovID << " obtained file ID " << freeFileID << std::endl;
						freeFileID++;

					}

				}

				for (unsigned i = 0; i < ovt7Size / 32; i++) {

					if (rom[ovt7Offset + i * 32 + 31] == ovUpdateID) {

						unsigned ovID = *reinterpret_cast<unsigned*>(&rom[ovt7Offset + i * 32]);
						rom[ovt7Offset + i * 32 + 24] = freeFileID & 0x00FF;
						rom[ovt7Offset + i * 32 + 25] = (freeFileID & 0xFF00) >> 8;
						rom[ovt7Offset + i * 32 + 26] = 0;
						rom[ovt7Offset + i * 32 + 27] = 0;
						rom[ovt7Offset + i * 32 + 31] = 3;
						ov7Entries[ovID].fileID = freeFileID;
						std::cout << DINFO << "ARM7 Overlay " << ovID << " obtained file ID " << freeFileID << std::endl;
						freeFileID++;

					}

				}

				fntPrintDirs(rootDir, rootPath.string());

			}
		break;



	}



	std::cout << DINFO << "Allocating File Allocation Table" << std::endl;



	fatSize = freeFileID * 8;
	romCheckBounds(rom, romOffset, fatSize);

	fatOffset = romOffset;
	std::memset(&rom[fatOffset], 0x00, fatSize);

	romOffset += fatSize;
	romOffset = alignAddress(romOffset, 512);



	std::cout << DINFO << "Linking Overlays to FAT" << std::endl;



	for (const auto& ov : ov9Entries) {

		const OverlayEntry& ov9e = ov.second;
		unsigned* fatPtr = reinterpret_cast<unsigned*>(&rom[fatOffset]);

		fatPtr[ov9e.fileID * 2] = ov9e.start;
		fatPtr[ov9e.fileID * 2 + 1] = ov9e.end;

		V_PRINT("Linked ARM9 Overlay " << ov.first << " with file ID " << ov9e.fileID << " to FAT")

	}

	for (const auto& ov : ov7Entries) {

		const OverlayEntry& ov7e = ov.second;
		unsigned* fatPtr = reinterpret_cast<unsigned*>(&rom[fatOffset]);

		fatPtr[ov7e.fileID * 2] = ov7e.start;
		fatPtr[ov7e.fileID * 2 + 1] = ov7e.end;

		V_PRINT("Linked ARM7 Overlay " << ov.first << " with file ID " << ov7e.fileID << " to FAT")

	}
	
	
	
	std::cout << DINFO << "Adding Icon / Title " << iconPath.string() << std::endl;



	iconSize = fs::file_size(iconPath);

	fileStream.open(iconPath, std::ios::binary | std::ios::in);
	FSTREAM_OPEN_CHECK(icon)

	unsigned short version;
	fileStream.read(reinterpret_cast<char*>(&version), 2);

	switch (version) {

	default:
		std::cout << DWARNING << "Invalid Icon / Title ID, defaulting to 0x840" << std::endl;
		[[fallthrough]];
	case 0x0001:
		FILESIZE_CHECK(icon, 0x0840)
		iconSize = 0x0840;
		break;
	case 0x0002:
		FILESIZE_CHECK(icon, 0x0940)
		iconSize = 0x0940;
		break;
	case 0x0003:
		FILESIZE_CHECK(icon, 0x0A40)
		iconSize = 0x0A40;
		break;
	case 0x0103:
		FILESIZE_CHECK(icon, 0x23C0)
		iconSize = 0x23C0;
		break;

	}

	fileStream.seekg(-2, std::ios::cur);

	romCheckBounds(rom, romOffset, iconSize);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), iconSize);
	fileStream.close();

	iconOffset = romOffset;

	romOffset += iconSize;
	romOffset = alignAddress(romOffset, 512);



	std::cout << DINFO << "Adding NitroROM filesystem" << std::endl;



	nfsAddAndLink(rom, fatOffset, rootDir, rootPath, romOffset);



	std::cout << DINFO << "Adding RSA signature " << rsaPath.string() << std::endl;



	rsaSize = fs::file_size(rsaPath);
	
	if (rsaSize != 0x88) {
		std::cout << DERROR << "Invalid RSA signature size: Expected 136 bytes, got " << rsaSize << std::endl;
		return -1;
	}
	
	fileStream.open(rsaPath, std::ios::binary | std::ios::in);
	FSTREAM_OPEN_CHECK(rsa)
	romCheckBounds(rom, romOffset, rsaSize);
	fileStream.read(reinterpret_cast<char*>(&rom[romOffset]), rsaSize);
	fileStream.close();



	std::cout << DINFO << "Done building ROM" << std::endl;
	std::cout << DINFO << "Fixing ROM header" << std::endl;



	unsigned* urom = reinterpret_cast<unsigned*>(rom.data());
	urom[8] = arm9Offset;
	urom[11] = arm9Size;
	urom[12] = arm7Offset;
	urom[15] = arm7Size;
	urom[16] = fntOffset;
	urom[17] = fntSize;
	urom[18] = fatOffset;
	urom[19] = fatSize;
	urom[20] = ovt9Size ? ovt9Offset : 0;
	urom[21] = ovt9Size;
	urom[22] = ovt7Size ? ovt7Offset : 0;
	urom[23] = ovt7Size;
	urom[26] = iconOffset;
	urom[32] = romOffset;
	urom[1024] = urom[32];

	if (finalRules["arm9_entry"].type == AVALUE) {
		urom[9] = std::stoul(finalRules["arm9_entry"].val, nullptr, 16);
	}

	if (finalRules["arm9_load"].type == AVALUE) {
		urom[10] = std::stoul(finalRules["arm9_load"].val, nullptr, 16);
	}

	if (finalRules["arm7_entry"].type == AVALUE) {
		urom[13] = std::stoul(finalRules["arm7_entry"].val, nullptr, 16);
	}

	if (finalRules["arm7_load"].type == AVALUE) {
		urom[14] = std::stoul(finalRules["arm7_load"].val, nullptr, 16);
	}

	rom[20] = static_cast<unsigned char>(std::log2(rom.size() >> 17));
	
	unsigned short* srom = reinterpret_cast<unsigned short*>(rom.data());
	srom[175] = crc16(rom.data(), 350);



	std::cout << DINFO << "Writing " << ndsOutputPath.string() << std::endl;



	if (fs::exists(ndsOutputPath) && fs::is_regular_file(ndsOutputPath)) {

		std::cout << DERROR << "File " << ndsOutputPath.string() << " already exists" << std::endl;
		return -1;

	}

	std::ofstream outputStream(ndsOutputPath, std::ios::binary | std::ios::out);
	
	if (!outputStream.is_open()) {

		std::cout << DERROR << "Failed to create output file " << ndsOutputPath.string() << std::endl;
		return -1;

	}

	outputStream.write(reinterpret_cast<const char*>(rom.data()), rom.size());
	outputStream.close();



	std::cout << DINFO << "Successfully written NDS image " << ndsOutputPath.filename().string() << std::endl;



	return 0;

}
