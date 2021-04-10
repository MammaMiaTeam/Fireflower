#include <iostream>
#include <filesystem>
#include <fstream>

#include "common.h"

namespace fs = std::filesystem;


#define FSTREAM_VALID(x) \
			if(!x.good()){ \
				std::cout << DERROR << "Input file invalid, reached EOF" << std::endl; \
				x.close(); \
				return -1; \
			}
#define FSTREAM_OPEN_CHECK \
			if (!outFile.is_open()) { \
				std::cout << DERROR << "Failed to open file " << outputPath.string() << std::endl; \
				return -1; \
			}
#define FILE_EXISTS(x) \
			if(fs::exists(x)){ \
				std::cout << DWARNING << "File " << outputPath.string() << " already exists" << std::endl; \
			}

constexpr unsigned oneGB = 1073741824U;

typedef std::vector<unsigned char> NitroROM;




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




void dumpFntTree(const NitroROM& rom, const NDSDirectory& dir, const fs::path& p, unsigned fatOffset) {

	const unsigned* urom = reinterpret_cast<const unsigned*>(rom.data());


	for (unsigned i = 0; i < dir.files.size(); i++) {

		fs::path fp = p.string() + "\\" + dir.files[i];

		if(!fs::exists(fp)){

			V_PRINT("Extracting file " << fp.string())

			unsigned short fid = dir.firstFileID + i;
			unsigned start = urom[(fatOffset + fid * 8) / 4];
			unsigned size = urom[(fatOffset + fid * 8 + 4) / 4] - start;

			std::ofstream outFile(fp, std::ios::binary | std::ios::out);
		
			if (!outFile.is_open()) {

				std::cout << DERROR << "Failed to create file " << fp.string() << std::endl;
				std::exit(-1);

			}

			outFile.write(reinterpret_cast<const char*>(&rom[start]), size);
			outFile.close();

		} else {

			std::cout << DWARNING << "File " << fp.string() << " already exists" << std::endl;

		}

	}


	for (unsigned i = 0; i < dir.dirs.size(); i++) {

		fs::path sp = p.string() + "\\" + dir.dirs[i].dirName;

		if(!fs::exists(sp) || (fs::exists(sp) && !fs::is_directory(sp))){

			V_PRINT("Creating data directory " << sp.string())

			try {

				if (!fs::create_directory(sp)) {

					std::cout << DERROR << "Failed to create directory " << sp.string() << std::endl;
					std::exit(-1);

				}


			} catch (std::exception& e) {

				std::cout << DERROR << e.what() << std::endl;
				std::exit(-1);

			}
			
		} else {

			std::cout << DWARNING << "Directory " << sp.string() << " already exists" << std::endl;

		}

		dumpFntTree(rom, dir.dirs[i], sp, fatOffset);

	}

}





int main(int argc, char** argv){

	if (argc != 3) {
		std::cout << "Invalid arguments. Call with .\\nds-extract.exe <nds_in> <filesystem_dir>" << std::endl;
		return -1;
	}

	fs::path ndsInputPath(argv[1]);
	fs::path fsOutputPath(argv[2]);
	fs::path ov9Path = fsOutputPath.string() + "\\overlay9";
	fs::path ov7Path = fsOutputPath.string() + "\\overlay7";
	fs::path dataPath = fsOutputPath.string() + "\\root";
	unsigned ndsFileSize = fs::file_size(ndsInputPath);

	if (!fs::exists(ndsInputPath) || !fs::is_regular_file(ndsInputPath)) {
		std::cout << DERROR << "NDS input file " << ndsInputPath.string() << " is not a valid file" << std::endl;
		return -1;
	}

	if (ndsFileSize > oneGB) {
		std::cout << DERROR << "NDS input file " << ndsInputPath.string() << " is larger than 1GB" << std::endl;
		return -1;
	}

	if (fs::exists(fsOutputPath) && fs::is_directory(fsOutputPath)) {
		std::cout << DWARNING << "Filesystem output path " << fsOutputPath.string() << " already exists, extracting missing files" << std::endl;
	}

	std::ofstream outFile;
	std::ifstream ndsFile;
	fs::path outputPath;
	ndsFile.open(ndsInputPath, std::ios::in | std::ios::binary);

	if (!ndsFile.is_open()) {
		std::cout << DERROR << "Failed to open file " << ndsInputPath.string() << std::endl;
		return -1;
	}

	FSTREAM_VALID(ndsFile)

	NitroROM rom;
	rom.resize(ndsFileSize);

	std::cout << DINFO << "Reading NDS file " << ndsInputPath.string() << std::endl;

	ndsFile.read(reinterpret_cast<char*>(rom.data()), ndsFileSize);
	ndsFile.close();

	unsigned* urom = reinterpret_cast<unsigned*>(rom.data());
	unsigned short* srom = reinterpret_cast<unsigned short*>(rom.data());

	unsigned arm9Offset = urom[8];
	unsigned arm9Size = urom[11];
	unsigned arm7Offset = urom[12];
	unsigned arm7Size = urom[15];
	unsigned ovt9Offset = urom[20];
	unsigned ovt9Size = urom[21];
	unsigned ovt7Offset = urom[22];
	unsigned ovt7Size = urom[23];
	unsigned fntOffset = urom[16];
	unsigned fntSize = urom[17];
	unsigned fatOffset = urom[18];
	unsigned fatSize = urom[19];
	unsigned iconOffset = urom[26];
	unsigned rsaOffset = urom[32];
	unsigned iconSize = 0x840;
	unsigned rsaSize = 136;


	std::cout << DINFO << "Creating directories" << std::endl;

	try {

		if (!fs::exists(fsOutputPath) || (fs::exists(fsOutputPath) && !fs::is_directory(fsOutputPath))) {

			if (!fs::create_directory(fsOutputPath)) {

				std::cout << DERROR << "Failed to create directory " << fsOutputPath.string() << std::endl;
				return -1;

			}

		}

		if (!fs::exists(ov9Path) || (fs::exists(ov9Path) && !fs::is_directory(ov9Path))) {

			if (!fs::create_directory(ov9Path)) {

				std::cout << DERROR << "Failed to create directory " << ov9Path.string() << std::endl;
				return -1;

			}

		}

		if (!fs::exists(ov7Path) || (fs::exists(ov7Path) && !fs::is_directory(ov7Path))) {

			if (!fs::create_directory(ov7Path)) {

				std::cout << DERROR << "Failed to create directory " << ov7Path.string() << std::endl;
				return -1;

			}

		}

		if (!fs::exists(dataPath) || (fs::exists(dataPath) && !fs::is_directory(dataPath))) {

			if (!fs::create_directory(dataPath)) {

				std::cout << DERROR << "Failed to create directory " << dataPath.string() << std::endl;
				return -1;

			}

		}

	} catch (std::exception& e) {

		std::cout << DERROR << e.what() << std::endl;
		return -1;

	}


	outputPath = fsOutputPath.string() + "\\" + "header.bin";

	if (!fs::exists(outputPath)) {

		std::cout << DINFO << "Extracting ROM header " << outputPath.string() << std::endl;

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(rom.data()), 0x4000);
		outFile.close();

	} else {
		
		std::cout << DWARNING << "File " << outputPath.string() << " already exists" << std::endl;

	}


	outputPath = fsOutputPath.string() + "\\" + "arm9.bin";

	if (!fs::exists(outputPath)) {

		std::cout << DINFO << "Extracting ARM9 binary " << outputPath.string() << std::endl;

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK


		outFile.write(reinterpret_cast<const char*>(&rom[arm9Offset]), arm9Size);
		outFile.close();

	} else {

		std::cout << DWARNING << "File " << outputPath.string() << " already exists" << std::endl;

	}


	outputPath = fsOutputPath.string() + "\\" + "arm7.bin";

	if (!fs::exists(outputPath)) {

		std::cout << DINFO << "Extracting ARM7 binary " << outputPath.string() << std::endl;

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[arm7Offset]), arm7Size);
		outFile.close();

	} else {

		std::cout << DWARNING << "File " << outputPath.string() << " already exists" << std::endl;

	}


	outputPath = fsOutputPath.string() + "\\" + "arm9ovt.bin";

	if (!fs::exists(outputPath)) {

		std::cout << DINFO << "Extracting ARM9 Overlay Table " << outputPath.string() << std::endl;

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[ovt9Offset]), ovt9Size);
		outFile.close();
		
	} else {

		std::cout << DWARNING << "File " << outputPath.string() << " already exists" << std::endl;

	}


	outputPath = fsOutputPath.string() + "\\" + "arm7ovt.bin";

	if (!fs::exists(outputPath)) {

		std::cout << DINFO << "Extracting ARM7 Overlay Table " << outputPath.string() << std::endl;

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[ovt7Offset]), ovt7Size);
		outFile.close();
		
	} else {

		std::cout << DWARNING << "File " << outputPath.string() << " already exists" << std::endl;

	}


	outputPath = fsOutputPath.string() + "\\" + "banner.bin";


	if (!fs::exists(outputPath)) {

		std::cout << DINFO << "Extracting Icon / Title " << outputPath.string() << std::endl;


		if (iconOffset) {

			switch (*reinterpret_cast<unsigned short*>(&rom[iconOffset])) {

				default:
					std::cout << DWARNING << "Invalid Icon / Title ID, defaulting to 0x840" << std::endl;
				case 0x0001:
					iconSize = 0x0840;
					break;
				case 0x0002:
					iconSize = 0x0940;
					break;
				case 0x0003:
					iconSize = 0x0A40;
					break;
				case 0x0103:
					iconSize = 0x23C0;
					break;

			}

		} else {

			std::cout << DINFO << "No Icon / Title found" << std::endl;
			iconSize = 0;

		}


		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[iconOffset]), iconSize);
		outFile.close();
		
	} else {

		std::cout << DWARNING << "File " << outputPath.string() << " already exists" << std::endl;

	}

	
	outputPath = fsOutputPath.string() + "\\" + "fnt.bin";

	if (!fs::exists(outputPath)) {

		std::cout << DINFO << "Extracting File Name Table " << outputPath.string() << std::endl;

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[fntOffset]), fntSize);
		outFile.close();
		
	} else {

		std::cout << DWARNING << "File " << outputPath.string() << " already exists" << std::endl;

	}


	outputPath = fsOutputPath.string() + "\\" + "fat.bin";

	if (!fs::exists(outputPath)) {

		std::cout << DINFO << "Extracting File Allocation Table " << outputPath.string() << std::endl;

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[fatOffset]), fatSize);
		outFile.close();

	} else {

		std::cout << DWARNING << "File " << outputPath.string() << " already exists" << std::endl;

	}


	outputPath = fsOutputPath.string() + "\\" + "rsasig.bin";

	if (!fs::exists(outputPath)) {

		std::cout << DINFO << "Extracting RSA signature " << outputPath.string() << std::endl;

		outFile.open(outputPath, std::ios::binary | std::ios::out);
		FSTREAM_OPEN_CHECK

		outFile.write(reinterpret_cast<const char*>(&rom[rsaOffset]), rsaSize);
		outFile.close();
		
	} else {

		std::cout << DWARNING << "File " << outputPath.string() << " already exists" << std::endl;

	}


	std::cout << DINFO << "Extracting ARM9 Overlays" << std::endl;

	if (ovt9Size) {

		for (unsigned i = 0; i < ovt9Size / 32; i++) {

			unsigned short fid = srom[(ovt9Offset + i * 32 + 24) / 2];
			unsigned ovStart = urom[(fatOffset + fid * 8) / 4];
			unsigned ovSize = urom[(fatOffset + fid * 8 + 4) / 4] - ovStart;
			
			outputPath = ov9Path.string() + "\\" + "overlay9_" + std::to_string(urom[(ovt9Offset + i * 32) / 4]) + ".bin";

			if (!fs::exists(outputPath)) {

				V_PRINT("Extracting ARM9 Overlay " << outputPath.string())

				outFile.open(outputPath, std::ios::binary | std::ios::out);
				FSTREAM_OPEN_CHECK

				outFile.write(reinterpret_cast<const char*>(&rom[ovStart]), ovSize);
				outFile.close();

			} else {

				std::cout << DWARNING << "ARM9 Overlay " << outputPath.string() << " already exists" << std::endl;

			}

		}

	}


	std::cout << DINFO << "Extracting ARM7 Overlays" << std::endl;

	if (ovt7Size) {

		for (unsigned i = 0; i < ovt7Size / 32; i++) {

			unsigned short fid = srom[(ovt7Offset + i * 32 + 24) / 2];
			unsigned ovStart = urom[(fatOffset + fid * 8) / 4];
			unsigned ovSize = urom[(fatOffset + fid * 8 + 4) / 4] - ovStart;

			outputPath = ov7Path.string() + "\\" + "overlay7_" + std::to_string(urom[(ovt7Offset + i * 32) / 4]) + ".bin";

			if (!fs::exists(outputPath)) {

				V_PRINT("Extracting ARM7 Overlay " << outputPath.string())

				outFile.open(outputPath, std::ios::binary | std::ios::out);
				FSTREAM_OPEN_CHECK

				outFile.write(reinterpret_cast<const char*>(&rom[ovStart]), ovSize);
				outFile.close();

			} else {

				std::cout << DWARNING << "ARM7 Overlay " << outputPath.string() << " already exists" << std::endl;

			}

		}

	}

	std::cout << DINFO << "Extracting Data files" << std::endl;

	NDSDirectory rootDir = buildFntTree(&rom[fntOffset], 0xF000, fntSize);
	dumpFntTree(rom, rootDir, dataPath, fatOffset);

	std::cout << DINFO << "Done" << std::endl;


	return 0;

}
