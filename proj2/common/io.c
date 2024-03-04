#include "io.h"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

int parse_uint(int fd, unsigned int *value, char *next) {
  char buf[16];

  int i = 0;
  while (1) {
    ssize_t read_bytes = read(fd, buf + i, 1);
    if (read_bytes == -1) {
      return 1;
    } else if (read_bytes == 0) {
      *next = '\0';
      break;
    }

    *next = buf[i];

    if (buf[i] > '9' || buf[i] < '0') {
      buf[i] = '\0';
      break;
    }

    i++;
  }

  unsigned long ul = strtoul(buf, NULL, 10);

  if (ul > UINT_MAX) {
    return 1;
  }

  *value = (unsigned int)ul;

  return 0;
}

int print_uint(int fd, unsigned int value) {
  char buffer[16];
  size_t i = 16;

  for (; value > 0; value /= 10) {
    buffer[--i] = '0' + (char)(value % 10);
  }

  if (i == 16) {
    buffer[--i] = '0';
  }

  while (i < 16) {
    ssize_t written = write(fd, buffer + i, 16 - i);
    if (written == -1) {
      return 1;
    }

    i += (size_t)written;
  }

  return 0;
}

int print_str(int fd, const char *str) {
  size_t len = strlen(str);
  while (len > 0) {
    ssize_t written = write(fd, str, len);
    if (written == -1) {
      return 1;
    }

    str += (size_t)written;
    len -= (size_t)written;
  }

  return 0;
}

//serve para passar size bytes de dados para o buffer
void store_data(void* buffer, void* data, size_t size) {
    memcpy(buffer, data, size);
}

//serve para passar size bytes do buffer para alguma variável(data)
void read_data(void* buffer, void* data, size_t size) {
    memcpy(data, buffer, size);
}

int lock_printf() {
    return pthread_mutex_lock(&print_mutex);
} 

int unlock_printf() {
    return pthread_mutex_unlock(&print_mutex);
}

ssize_t safe_write(int fd, void * buffer, size_t bytesToWrite){
  ssize_t bytesWritten = 0;
  ssize_t n;

  while(bytesWritten < (ssize_t)bytesToWrite){
    n = write(fd, ((char*)buffer) + bytesWritten, bytesToWrite - (size_t)bytesWritten);
    if(n == -1){
      return -1;
    }
    bytesWritten += n;
  }
  return bytesWritten;
}


ssize_t safe_read(int fd, void * buffer, size_t bytesToRead){
  ssize_t bytesRead = 0;
  ssize_t n;

  while(bytesRead < (ssize_t)bytesToRead){

    n = read(fd, ((char*)buffer) + bytesRead, bytesToRead - (size_t)bytesRead);
    if(n == -1){
      return -1;
    }
    bytesRead += n;
  }
  return bytesRead;
}