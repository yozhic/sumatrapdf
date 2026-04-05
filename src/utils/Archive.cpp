/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "utils/BaseUtil.h"
#include "utils/FileUtil.h"
#include "utils/ScopedWin.h"
#include "utils/WinUtil.h"
#include "utils/CryptoUtil.h"

#include "utils/Archive.h"

#include "libarchive/archive.h"
#include "libarchive/archive_entry.h"

// TODO: set include path to ext/ dir
#include "../../ext/unrar/dll.hpp"

// we pad data read with 3 zeros for convenience. That way returned
// data is a valid null-terminated string or WCHAR*.
// 3 is for absolute worst case of WCHAR* where last char was partially written
#define ZERO_PADDING_COUNT 3

FILETIME MultiFormatArchive::FileInfo::GetWinFileTime() const {
    FILETIME ft = {(DWORD)-1, (DWORD)-1};
    LocalFileTimeToFileTime((FILETIME*)&fileTime, &ft);
    return ft;
}

MultiFormatArchive::MultiFormatArchive(MultiFormatArchive::Format format) : format(format) {
    if (format == Format::Tar) {
        loadOnOpen = true;
    }
}

MultiFormatArchive::~MultiFormatArchive() {
    for (auto& fi : fileInfos_) {
        free((void*)fi->data);
    }
    free(archivePath_);
}

bool MultiFormatArchive::ParseEntries(struct archive* a) {
    struct archive_entry* entry;
    size_t fileId = 0;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* name = archive_entry_pathname_utf8(entry);
        if (!name) {
            name = archive_entry_pathname(entry);
        }
        if (!name) {
            name = "";
        }
        FileInfo* i = allocator_.AllocStruct<FileInfo>();
        i->fileId = fileId;
        i->fileSizeUncompressed = (size_t)archive_entry_size(entry);
        i->filePos = (i64)fileId; // use fileId as position identifier
        i->fileTime = (i64)archive_entry_mtime(entry);
        i->name = str::Dup(&allocator_, name);
        i->data = nullptr;
        fileInfos_.Append(i);

        if (loadOnOpen) {
            size_t size = i->fileSizeUncompressed;
            if (size > 0) {
                i->data = AllocArray<char>(size + ZERO_PADDING_COUNT);
                if (i->data) {
                    la_ssize_t n = archive_read_data(a, (void*)i->data, size);
                    if (n < 0 || (size_t)n != size) {
                        free(i->data);
                        i->data = nullptr;
                    }
                }
            }
        } else {
            archive_read_data_skip(a);
        }
        fileId++;
    }
    return fileId > 0;
}

bool MultiFormatArchive::Open(const char* path) {
    if ((format == Format::Rar) && path) {
        bool ok = OpenUnrarFallback(path);
        if (ok) {
            return true;
        }
    }
    return OpenArchive(path);
}

bool MultiFormatArchive::Open(IStream* stream) {
    // for IStream, read all data into memory and open from there
    STATSTG stat;
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) {
        return false;
    }
    size_t size = (size_t)stat.cbSize.QuadPart;
    u8* data = AllocArray<u8>(size);
    if (!data) {
        return false;
    }
    LARGE_INTEGER zero = {};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    HRESULT hr = stream->Read(data, (ULONG)size, &read);
    if (FAILED(hr) || read != size) {
        free(data);
        return false;
    }

    struct archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    int r = archive_read_open_memory(a, data, size);
    if (r != ARCHIVE_OK) {
        archive_read_free(a);
        free(data);
        return false;
    }
    bool ok = ParseEntries(a);
    archive_read_free(a);
    free(data);
    return ok;
}

bool MultiFormatArchive::OpenArchive(const char* path) {
    struct archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    int r = archive_read_open_filename(a, path, 10240);
    if (r != ARCHIVE_OK) {
        archive_read_free(a);
        return false;
    }
    archivePath_ = str::Dup(path);
    bool ok = ParseEntries(a);
    archive_read_free(a);
    return ok;
}

Vec<MultiFormatArchive::FileInfo*> const& MultiFormatArchive::GetFileInfos() {
    return fileInfos_;
}

size_t getFileIdByName(Vec<MultiFormatArchive::FileInfo*>& fileInfos, const char* name) {
    for (auto fileInfo : fileInfos) {
        if (str::EqI(fileInfo->name, name)) {
            return fileInfo->fileId;
        }
    }
    return (size_t)-1;
}

size_t MultiFormatArchive::GetFileId(const char* fileName) {
    return getFileIdByName(fileInfos_, fileName);
}

ByteSlice MultiFormatArchive::GetFileDataByName(const char* fileName) {
    size_t fileId = getFileIdByName(fileInfos_, fileName);
    return GetFileDataById(fileId);
}

// the caller must free()
ByteSlice MultiFormatArchive::GetFileDataById(size_t fileId) {
    if (fileId == (size_t)-1) {
        return {};
    }
    ReportIf(fileId >= fileInfos_.size());

    auto* fileInfo = fileInfos_[fileId];
    ReportIf(fileInfo->fileId != fileId);

    if (fileInfo->data != nullptr) {
        // the caller takes ownership
        ByteSlice res{(u8*)fileInfo->data, fileInfo->fileSizeUncompressed};
        fileInfo->data = nullptr;
        return res;
    }

    if (LoadedUsingUnrarDll()) {
        return GetFileDataByIdUnarrDll(fileId);
    }

    return GetFileDataByIdLibarchive(fileId);
}

ByteSlice MultiFormatArchive::GetFileDataByIdLibarchive(size_t fileId) {
    if (!archivePath_) {
        return {};
    }
    auto* fileInfo = fileInfos_[fileId];

    // re-open the archive and skip to the right entry
    struct archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    int r = archive_read_open_filename(a, archivePath_, 10240);
    if (r != ARCHIVE_OK) {
        archive_read_free(a);
        return {};
    }

    struct archive_entry* entry;
    size_t idx = 0;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (idx == fileId) {
            size_t size = fileInfo->fileSizeUncompressed;
            if (addOverflows<size_t>(size, ZERO_PADDING_COUNT)) {
                archive_read_free(a);
                return {};
            }
            u8* data = AllocArray<u8>(size + ZERO_PADDING_COUNT);
            if (!data) {
                archive_read_free(a);
                return {};
            }
            la_ssize_t n = archive_read_data(a, data, size);
            archive_read_free(a);
            if (n < 0 || (size_t)n != size) {
                free(data);
                return {};
            }
            return {data, size};
        }
        archive_read_data_skip(a);
        idx++;
    }
    archive_read_free(a);
    return {};
}

const char* MultiFormatArchive::GetComment() {
    // libarchive doesn't support zip global comments
    return nullptr;
}

///// format specific handling /////

static MultiFormatArchive* open(MultiFormatArchive* archive, const char* path) {
    bool ok = archive->Open(path);
    if (!ok) {
        delete archive;
        return nullptr;
    }
    return archive;
}

static MultiFormatArchive* open(MultiFormatArchive* archive, IStream* stream) {
    bool ok = archive->Open(stream);
    if (!ok) {
        delete archive;
        return nullptr;
    }
    return archive;
}

MultiFormatArchive* OpenZipArchive(const char* path, bool /*deflatedOnly*/) {
    auto* archive = new MultiFormatArchive(MultiFormatArchive::Format::Zip);
    return open(archive, path);
}

MultiFormatArchive* Open7zArchive(const char* path) {
    auto* archive = new MultiFormatArchive(MultiFormatArchive::Format::SevenZip);
    return open(archive, path);
}

MultiFormatArchive* OpenTarArchive(const char* path) {
    auto* archive = new MultiFormatArchive(MultiFormatArchive::Format::Tar);
    return open(archive, path);
}

MultiFormatArchive* OpenRarArchive(const char* path) {
    auto* archive = new MultiFormatArchive(MultiFormatArchive::Format::Rar);
    return open(archive, path);
}

MultiFormatArchive* OpenZipArchive(IStream* stream, bool /*deflatedOnly*/) {
    auto* archive = new MultiFormatArchive(MultiFormatArchive::Format::Zip);
    return open(archive, stream);
}

MultiFormatArchive* Open7zArchive(IStream* stream) {
    auto* archive = new MultiFormatArchive(MultiFormatArchive::Format::SevenZip);
    return open(archive, stream);
}

MultiFormatArchive* OpenTarArchive(IStream* stream) {
    auto* archive = new MultiFormatArchive(MultiFormatArchive::Format::Tar);
    return open(archive, stream);
}

MultiFormatArchive* OpenRarArchive(IStream* stream) {
    auto* archive = new MultiFormatArchive(MultiFormatArchive::Format::Rar);
    return open(archive, stream);
}

struct Data {
    u8* d = nullptr;
    size_t sz = 0;
    u8* curr = nullptr;
};

static size_t DataLeft(const Data& d) {
    size_t consumed = (d.curr - d.d);
    ReportIf(consumed > d.sz);
    return d.sz - consumed;
}

// return 1 on success. Other values for msg that we don't handle: UCM_CHANGEVOLUME, UCM_NEEDPASSWORD
static int CALLBACK unrarCallback(UINT msg, LPARAM userData, LPARAM rarBuffer, LPARAM bytesProcessed) {
    if (UCM_PROCESSDATA != msg || !userData) {
        return -1;
    }
    Data* buf = (Data*)userData;
    size_t bytesGot = (size_t)bytesProcessed;
    if (bytesGot > DataLeft(*buf)) {
        return -1;
    }
    memcpy(buf->curr, (char*)rarBuffer, bytesGot);
    buf->curr += bytesGot;
    return 1;
}

static bool FindFile(HANDLE hArc, RARHeaderDataEx* rarHeader, const WCHAR* fileName) {
    int res;
    for (;;) {
        res = RARReadHeaderEx(hArc, rarHeader);
        if (0 != res) {
            return false;
        }
        str::TransCharsInPlace(rarHeader->FileNameW, L"\\", L"/");
        if (str::EqI(rarHeader->FileNameW, fileName)) {
            // don't support files whose uncompressed size is greater than 4GB
            return rarHeader->UnpSizeHigh == 0;
        }
        RARProcessFile(hArc, RAR_SKIP, nullptr, nullptr);
    }
}

ByteSlice MultiFormatArchive::GetFileDataByIdUnarrDll(size_t fileId) {
    ReportIf(!rarFilePath_);

    auto* fileInfo = fileInfos_[fileId];
    ReportIf(fileInfo->fileId != fileId);
    if (fileInfo->data != nullptr) {
        return {(u8*)fileInfo->data, fileInfo->fileSizeUncompressed};
    }

    auto rarPath = ToWStrTemp(rarFilePath_);

    Data uncompressedBuf;

    RAROpenArchiveDataEx arcData = {nullptr};
    arcData.ArcNameW = rarPath;
    arcData.OpenMode = RAR_OM_EXTRACT;
    arcData.Callback = unrarCallback;
    arcData.UserData = (LPARAM)&uncompressedBuf;

    HANDLE hArc = RAROpenArchiveEx(&arcData);
    if (!hArc || arcData.OpenResult != 0) {
        return {};
    }

    char* data = nullptr;
    size_t size = 0;
    auto fileName = ToWStrTemp(fileInfo->name);
    RARHeaderDataEx rarHeader{};
    int res;
    bool ok = FindFile(hArc, &rarHeader, fileName);
    if (!ok) {
        goto Exit;
    }
    size = fileInfo->fileSizeUncompressed;
    ReportIf(size != rarHeader.UnpSize);
    if (addOverflows<size_t>(size, ZERO_PADDING_COUNT)) {
        ok = false;
        goto Exit;
    }

    data = AllocArray<char>(size + ZERO_PADDING_COUNT);
    if (!data) {
        ok = false;
        goto Exit;
    }

    uncompressedBuf.d = (u8*)data;
    uncompressedBuf.curr = (u8*)data;
    uncompressedBuf.sz = size;
    res = RARProcessFile(hArc, RAR_TEST, nullptr, nullptr);
    ok = (res == 0) && (DataLeft(uncompressedBuf) == 0);

Exit:
    RARCloseArchive(hArc);
    if (!ok) {
        free(data);
        return {};
    }
    return {(u8*)data, size};
}

// asan build crashes in UnRAR code
// see https://codeeval.dev/gist/801ad556960e59be41690d0c2fa7cba0
bool MultiFormatArchive::OpenUnrarFallback(const char* rarPath) {
    if (!rarPath) {
        return false;
    }
    ReportIf(rarFilePath_);
    auto rarPathW = ToWStrTemp(rarPath);

    ByteSlice uncompressedBuf;

    RAROpenArchiveDataEx arcData = {nullptr};
    arcData.ArcNameW = (WCHAR*)rarPathW;
    arcData.OpenMode = RAR_OM_LIST;
    if (loadOnOpen) {
        arcData.OpenMode = RAR_OM_EXTRACT;
        arcData.Callback = unrarCallback;
        arcData.UserData = (LPARAM)&uncompressedBuf;
    }

    HANDLE hArc = RAROpenArchiveEx(&arcData);
    if (!hArc || arcData.OpenResult != 0) {
        return false;
    }

    size_t fileId = 0;
    while (true) {
        RARHeaderDataEx rarHeader{};
        int res = RARReadHeaderEx(hArc, &rarHeader);
        if (0 != res) {
            break;
        }

        str::TransCharsInPlace(rarHeader.FileNameW, L"\\", L"/");
        auto name = ToUtf8Temp(rarHeader.FileNameW);

        FileInfo* i = allocator_.AllocStruct<FileInfo>();
        i->fileId = fileId;
        i->fileSizeUncompressed = (size_t)rarHeader.UnpSize;
        i->filePos = 0;
        i->fileTime = (i64)rarHeader.FileTime;
        i->name = str::Dup(&allocator_, name);
        i->data = nullptr;
        if (loadOnOpen) {
            // +2 so that it's zero-terminated even when interprted as WCHAR*
            i->data = AllocArray<char>(i->fileSizeUncompressed + 2);
            uncompressedBuf.Set(i->data, i->fileSizeUncompressed);
        }
        fileInfos_.Append(i);

        fileId++;

        int op = RAR_SKIP;
        if (loadOnOpen) {
            op = RAR_EXTRACT;
        }
        RARProcessFile(hArc, op, nullptr, nullptr);
    }

    RARCloseArchive(hArc);

    rarFilePath_ = str::Dup(&allocator_, rarPath);
    return true;
}
