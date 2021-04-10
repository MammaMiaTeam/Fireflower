#pragma once

#ifdef _WIN32
    #ifdef NFSFSH_EXPORTS
	    #define NFSFSH_API __declspec(dllexport)
    #else
	    #define NFSFSH_API __declspec(dllimport)
    #endif
#elif defined __linux__
    #define NFSFSH_API
#else
    #error "Architecture not compatible"
#endif

#include <vector>
#include <string>
#include <filesystem>



struct NDSDirectory {

	unsigned short firstFileID;
	unsigned short directoryID;
	std::string dirName;
	std::vector<std::string> files;
	std::vector<NDSDirectory> dirs;

};



namespace NFSFSH {

    constexpr unsigned short badFileID = 0xEFFF;

    /*
        Reads the FNT and creates a recursive NDSDirectory for the root path
    */
    NFSFSH_API NDSDirectory readFntTree(const std::vector<unsigned char>& fnt, unsigned dirID = 0xF000);

    /*
        Returns the next free file ID constrained by the directory node dir
    */
    NFSFSH_API unsigned short findNextFreeFileID(const NDSDirectory& dir);

    /*
        Returns the next free dir ID constrained by the directory node dir
    */
    NFSFSH_API unsigned short findNextFreeDirID(const NDSDirectory& dir);

    /*
        Adds files to dir that exist in the data directory given by dataPath and assigns freeFileID and freeDirID appropriately
    */
    NFSFSH_API void addNewFiles(NDSDirectory& dir, const std::filesystem::path& dataPath, unsigned short& freeFileID, unsigned short& freeDirID);

    /*
        Prints all files contained in dir
    */
    NFSFSH_API void printDirs(const NDSDirectory& dir, const std::string& path);

    /*
        Returns the number of directories contained in dir (recursive)
    */
    NFSFSH_API unsigned directoryCount(const NDSDirectory& dir);

    /*
        Returns the number of bytes that dir (and its subdirectories) occupies in the fnt
    */
    NFSFSH_API unsigned fntByteCount(const NDSDirectory& dir);

    /*
        Returns the number of bytes occupied by the FNT header
    */
    NFSFSH_API unsigned fntByteCountHeader(const NDSDirectory& root);

    /*
        Writes dir to the FNT. fnOffset is the offset to the first filename (after the header). For the root dir, parentID is the number of total directories, else the parentID. Returns the last written offset.
    */
    NFSFSH_API unsigned writeFntTree(const NDSDirectory& dir, std::vector<unsigned char>& fnt, unsigned fnOffset, unsigned parentID);

    /*
        Rebuilds the FNT from root
    */
    NFSFSH_API void rebuildFnt(const NDSDirectory& root, std::vector<unsigned char>& fnt);

    /*
        Returns the file ID corresponding to path relative to dir
    */
    NFSFSH_API unsigned short getFileID(const NDSDirectory& dir, const std::filesystem::path& path);

}