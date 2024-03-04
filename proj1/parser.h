#ifndef EMS_PARSER_H
#define EMS_PARSER_H

#include <stddef.h>
#include <unistd.h>
#include <pthread.h>

enum Command {
  CMD_CREATE,
  CMD_RESERVE,
  CMD_SHOW,
  CMD_LIST_EVENTS,
  CMD_BARRIER,
  CMD_WAIT,
  CMD_HELP,
  CMD_EMPTY,
  CMD_INVALID,
  EOC  // End of commands
};

typedef struct{
  size_t x;
  size_t y;
  pthread_mutex_t mutex;
}Coordinate;

/// Reads a line and returns the corresponding command.
/// @param fd File descriptor to read from.
/// @return The command read.
enum Command get_next(int fd);

/// Parses a CREATE command.
/// @param fd File descriptor to read from.
/// @param event_id Pointer to the variable to store the event ID in.
/// @param num_rows Pointer to the variable to store the number of rows in.
/// @param num_cols Pointer to the variable to store the number of columns in.
/// @return 0 if the command was parsed successfully, 1 otherwise.
int parse_create(int fd, unsigned int *event_id, size_t *num_rows, size_t *num_cols);

/// Parses a RESERVE command.
/// @param fd File descriptor to read from.
/// @param max Maximum number of coordinates to read.
/// @param event_id Pointer to the variable to store the event ID in.
/// @param xs Pointer to the array to store the X coordinates in.
/// @param ys Pointer to the array to store the Y coordinates in.
/// @return Number of coordinates read. 0 on failure.
size_t parse_reserve(int fd, size_t max, unsigned int *event_id, Coordinate *coords);

/// Parses a SHOW command.
/// @param fd File descriptor to read from.
/// @param event_id Pointer to the variable to store the event ID in.
/// @return 0 if the command was parsed successfully, 1 otherwise.
int parse_show(int fd, unsigned int *event_id);

/// Parses a WAIT command.
/// @param fd File descriptor to read from.
/// @param delay Pointer to the variable to store the wait delay in.
/// @param thread_id Pointer to the variable to store the thread ID in. May not be set.
/// @return 0 if no thread was specified, 1 if a thread was specified, -1 on error.
int parse_wait(int fd, unsigned int *delay, unsigned int *thread_id);

char* intToString(unsigned int num);

ssize_t safe_write(int fd, char* buffer, size_t bytesToWrite);

int compareCoordinates(const void* a, const void* b);

#endif  // EMS_PARSER_H
