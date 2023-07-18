#include <string>
#include <iostream>
#include <fstream>
#include <regex>
#include <vector>
#include <limits.h>

#include "lib/easylogging/easylogging++.h"
#include "lib/json/json.hpp"
#include "helpers.h"
#include "errors.h"
#include "settings.h"
#include "resources.h"
#include "api/debug/debug.h"
#include "api/filesystem/filesystem.h"

#if defined(_WIN32)
// ifstream and ofstream do not support UTF-8 file paths on Windows.
// However there is a non-standard extension which allows the use of wide strings.
// So, before we pass the path string to the constructor, we have to convert it to a UTF-16 std::wstring.
#define CONVSTR(S) helpers::str2wstr(S)
#include <windows.h>
#else
#define CONVSTR(S) S
#endif

#define NEU_APP_RES_FILE "/resources.neu"

using namespace std;
using json = nlohmann::json;

namespace resources {

json fileTree = nullptr;
char* embeddedResourcesData = NULL;
unsigned int asarHeaderSize;
resources::ResourceMode mode = resources::ResourceModeBundle;

pair<int, string> __seekFilePos(const string &path, json node, const string &curpath) {
    vector<string> pathSegments = helpers::split(path, '/');
    string filename = pathSegments[pathSegments.size() - 1];
    json json = node;
    for(const auto &pathSegment: pathSegments) {
        if(pathSegment.length() == 0 || json.is_null() || json["files"].is_null())
            continue;
        json = json["files"][pathSegment];
    }
    if(!json.is_null())
        return make_pair<int, string>(json["size"].get<int>(), json["offset"].get<string>());
    return make_pair<int, string>(-1, "");
}

// Needs explicit close later
ifstream __openResourceFile() {
    ifstream asarArchive;
    string resFileName = NEU_APP_RES_FILE;
    resFileName = settings::joinAppPath(resFileName);
    asarArchive.open(CONVSTR(resFileName), ios::binary);
    if(!asarArchive) {
        debug::log(debug::LogTypeError, errors::makeErrorMsg(errors::NE_RS_TREEGER, resFileName));
    }
    return asarArchive;
}

char* __openEmbeddedResourceFile() {
#if defined(_WIN32)
    // test for existing embedded resources.neu file
    HRSRC hresinfo = FindResource(NULL, MAKEINTRESOURCE(IDR_EMBEDDED_RESFILE), RT_RCDATA);
    if (hresinfo) {
        HGLOBAL hRes = LoadResource(NULL, hresinfo);
        DWORD datasize = SizeofResource(NULL, hresinfo);
        char* data = (char*)LockResource(hRes);
        return data;
    }
#endif
    return NULL;
}

fs::FileReaderResult __getFileFromBundle(const string &filename) {
    fs::FileReaderResult fileReaderResult;
    pair<int, string> p = __seekFilePos(filename, fileTree, "");
    if(p.first != -1) {
        unsigned int uSize = p.first;
        unsigned int uOffset = stoi(p.second);

        if (embeddedResourcesData) {
            fileReaderResult.data.assign(embeddedResourcesData + asarHeaderSize + uOffset, uSize);
        }
        else {
            ifstream asarArchive = __openResourceFile();
            if (!asarArchive) {
                fileReaderResult.status = errors::NE_RS_TREEGER;
                return fileReaderResult;
            }
            vector<char>fileBuf ( uSize );
            asarArchive.seekg(asarHeaderSize + uOffset);
            asarArchive.read(fileBuf.data(), uSize);
            string fileContent(fileBuf.begin(), fileBuf.end());
            fileReaderResult.data = fileContent;
            asarArchive.close();
        }
   }
   else {
        fileReaderResult.status = errors::NE_RS_TREEGER;
   }
   return fileReaderResult;
}

bool __makeFileTree() {

    ifstream asarArchive;

    // test if binary has resources.neu embedded 
    embeddedResourcesData = __openEmbeddedResourceFile();
    if (!embeddedResourcesData) {
        // fallback to "real" resource file
        asarArchive = __openResourceFile();
    }

    if (!asarArchive && !embeddedResourcesData) {
        return false;
    }

    char *sizeBuf = new char[8];
    unsigned int uSize = 0;
    if (embeddedResourcesData) {
        std::memcpy(sizeBuf, embeddedResourcesData, 8);
    }
    else {
        asarArchive.read(sizeBuf, 8);
    }
    uSize = *(unsigned int *)(sizeBuf + 4) - 8;
    delete[] sizeBuf;

    asarHeaderSize = uSize + 16;
    vector<char> headerBuf(uSize);

    if (embeddedResourcesData) {
        std::memcpy(headerBuf.data(), embeddedResourcesData + 16, uSize);        
    }
    else {
        asarArchive.seekg(16);
        asarArchive.read(headerBuf.data(), uSize);
        asarArchive.close();
    }

    json files;
    string headerContent(headerBuf.begin(), headerBuf.end());

    try {
        files = json::parse(headerContent);
    }
    catch(exception e) {
        debug::log(debug::LogTypeError, e.what());
    }
    fileTree = files;
    return fileTree != nullptr;
}

void extractFile(const string &filename, const string &outputFilename) {
    fs::FileReaderResult fileReaderResult = resources::getFile(filename);
    fs::FileWriterOptions fileWriterOptions;
    fileWriterOptions.filename = outputFilename;
    fileWriterOptions.data = fileReaderResult.data;
    fs::writeFile(fileWriterOptions);
}

fs::FileReaderResult getFile(const string &filename) {
    if(resources::getMode() == resources::ResourceModeBundle) {
        return __getFileFromBundle(filename);
    }
    return fs::readFile(settings::joinAppPath(filename));
}

void init() {
    if(resources::getMode() == resources::ResourceModeDir) {
        return;
    }
    bool resourceLoaderStatus = __makeFileTree();
    if(!resourceLoaderStatus) {
        resources::setMode(resources::ResourceModeDir); // fallback to directory mode
    }
}

void setMode(const resources::ResourceMode m) {
    mode = m;
}

resources::ResourceMode getMode() {
    return mode;
}

string getModeString() {
    return mode == resources::ResourceModeDir ? "directory" : "bundle";
}

} // namespace resources
