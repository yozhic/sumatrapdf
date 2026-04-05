/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct archive;
struct archive_entry;

class MultiFormatArchive {
  public:
    enum class Format {
        Zip,
        Rar,
        SevenZip,
        Tar
    };

    struct FileInfo {
        size_t fileId = 0;
        const char* name = nullptr;
        i64 fileTime = 0; // this is typedef'ed as time64_t in unrar.h
        size_t fileSizeUncompressed = 0;

        // internal use
        i64 filePos = 0;
        char* data = nullptr;

        FILETIME GetWinFileTime() const;
    };

    MultiFormatArchive(Format format);
    ~MultiFormatArchive();

    Format format;

    bool Open(const char* path);
    bool Open(IStream* stream);

    Vec<FileInfo*> const& GetFileInfos();

    size_t GetFileId(const char* fileName);

    ByteSlice GetFileDataByName(const char* filename);
    ByteSlice GetFileDataById(size_t fileId);
    ByteSlice GetFileDataPartById(size_t fileId, size_t sizeHint);

    const char* GetComment();

    // if true, will load and uncompress all files on open
    bool loadOnOpen = false;

  protected:
    // used for allocating strings that are referenced by ArchFileInfo::name
    PoolAllocator allocator_;
    Vec<FileInfo*> fileInfos_;

    char* archivePath_ = nullptr;

    // only set when we loaded file infos using unrar.dll fallback
    const char* rarFilePath_ = nullptr;

    bool OpenArchive(const char* path);
    bool OpenArchive(IStream* stream);
    bool ParseEntries(struct archive* a);

    bool OpenUnrarFallback(const char* rarPathUtf);
    ByteSlice GetFileDataByIdUnarrDll(size_t fileId);
    ByteSlice GetFileDataPartByIdUnarrDll(size_t fileId, size_t sizeHint);
    ByteSlice GetFileDataByIdLibarchive(size_t fileId);
    bool LoadedUsingUnrarDll() const { return rarFilePath_ != nullptr; }
};

MultiFormatArchive* OpenZipArchive(const char* path, bool deflatedOnly);
MultiFormatArchive* Open7zArchive(const char* path);
MultiFormatArchive* OpenTarArchive(const char* path);
MultiFormatArchive* OpenRarArchive(const char* path);

MultiFormatArchive* OpenZipArchive(IStream* stream, bool deflatedOnly);
MultiFormatArchive* Open7zArchive(IStream* stream);
MultiFormatArchive* OpenTarArchive(IStream* stream);
MultiFormatArchive* OpenRarArchive(IStream* stream);
