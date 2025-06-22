#include "fileio.h"
#include "log.h"
#include "miniz.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

// Persistent storage for last loaded sizes
static long last_file_size = 0;
static std::size_t last_zip_uncompressed_size = 0;

int get_last_file_size() {
    return last_file_size;
}

unsigned int get_last_zip_file_size() {
    if (last_zip_uncompressed_size > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())) {
        LOG_ERROR("Warning: Zip file too large to fit in unsigned int. Truncating size.");
        return std::numeric_limits<unsigned int>::max();
    }
    return static_cast<unsigned int>(last_zip_uncompressed_size);
}

static int get_file_size(FILE* file) {
    fseek(file, 0, SEEK_END);
    int size = static_cast<int>(ftell(file));
    fseek(file, 0, SEEK_SET);
    return size;
}

unsigned char* load_file(const char* filename) {
    FILE* fd = nullptr;
    if (fopen_s(&fd, filename, "rb") != 0 || !fd) {
        LOG_ERROR("Failed to open file: %s", filename);
        return nullptr;
    }

    last_file_size = get_file_size(fd);
    auto* buf = static_cast<unsigned char*>(std::malloc(last_file_size));
    if (!buf) {
        LOG_ERROR("Failed to allocate memory for file: %s", filename);
        fclose(fd);
        return nullptr;
    }

    std::fread(buf, 1, last_file_size, fd);
    std::fclose(fd);
    return buf;
}

int save_file(const char* filename, const unsigned char* buf, int size) {
    FILE* fd = nullptr;
    if (fopen_s(&fd, filename, "wb") != 0 || !fd) {
        LOG_ERROR("Failed to save file: %s", filename);
        return 0;
    }

    std::fwrite(buf, 1, size, fd);
    std::fclose(fd);
    return 1;
}

unsigned char* load_zip_file(const char* archname, const char* filename) {
    mz_zip_archive zip_archive = {};
   // mz_bool status;
    mz_uint file_index;
    mz_zip_archive_file_stat file_stat;

    LOG_INFO("Opening archive: %s", archname);
    if (!mz_zip_reader_init_file(&zip_archive, archname, 0)) {
        LOG_ERROR("Failed to open archive: %s", archname);
        return nullptr;
    }

    file_index = mz_zip_reader_locate_file(&zip_archive, filename, nullptr, 0);
    if (file_index == -1) {
        LOG_ERROR("File not found in archive: %s", filename);
        mz_zip_reader_end(&zip_archive);
        return nullptr;
    }

    if (!mz_zip_reader_file_stat(&zip_archive, file_index, &file_stat)) {
        LOG_ERROR("Failed to get file stats for: %s", filename);
        mz_zip_reader_end(&zip_archive);
        return nullptr;
    }

    last_zip_uncompressed_size = static_cast<std::size_t>(file_stat.m_uncomp_size);
    auto* buf = static_cast<unsigned char*>(std::malloc(last_zip_uncompressed_size));
    if (!buf) {
        LOG_ERROR("Failed to allocate buffer for ZIP file: %s", filename);
        mz_zip_reader_end(&zip_archive);
        return nullptr;
    }

    if (!mz_zip_reader_extract_to_mem(&zip_archive, file_index, buf, last_zip_uncompressed_size, 0)) {
        LOG_ERROR("Failed to extract file: %s", filename);
        std::free(buf);
        mz_zip_reader_end(&zip_archive);
        return nullptr;
    }

    LOG_INFO("Successfully loaded file: %s from archive: %s", filename, archname);
    mz_zip_reader_end(&zip_archive);
    return buf;
}

bool save_zip_file(const char* archname, const char* filename, const unsigned char* data) {
    if (!archname || !filename || !data) {
        LOG_ERROR("Invalid arguments to save_generic_zip");
        return false;
    }

    bool status = mz_zip_add_mem_to_archive_file_in_place(
        archname, filename, data, std::strlen(reinterpret_cast<const char*>(data)) + 1,
        nullptr, 0, MZ_BEST_COMPRESSION
    );

    if (!status) {
        LOG_ERROR("Failed to save data to archive: %s", archname);
        return false;
    }

    return true;
}
