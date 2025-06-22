#pragma once

#ifndef FILEIO_H
#define FILEIO_H

#include <cstddef>

int get_last_file_size();
unsigned int get_last_zip_file_size();

unsigned char* load_file(const char* filename);
int save_file(const char* filename, const unsigned char* buf, int size);
unsigned char* load_zip_file(const char* archname, const char* filename);
bool save_zip_file(const char* archname, const char* filename, const unsigned char* data);

#endif // FILEIO_H
