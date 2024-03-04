#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "api.h"
#include "common/io.h"
#include "common/constants.h"

int server_fd;
int req_fd;
int resp_fd;
int session_id;
char req_pipe[MAX_PIPE_NAME_SIZE];
char resp_pipe[MAX_PIPE_NAME_SIZE];


int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
  char OP_CODE = '1';

  size_t buf_size = sizeof(char)+2*MAX_PIPE_NAME_SIZE*sizeof(char);
  char buf[buf_size];
  
  strcpy(req_pipe, req_pipe_path);
  for(size_t i = strlen(req_pipe); i < MAX_PIPE_NAME_SIZE; i++){
    req_pipe[i] = '\0';
  }

  strcpy(resp_pipe, resp_pipe_path);
  for(size_t i = strlen(resp_pipe); i < MAX_PIPE_NAME_SIZE; i++){
    resp_pipe[i] = '\0';
  }
  
  if (unlink(req_pipe_path) == -1 && errno != ENOENT) {
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", req_pipe_path,
            strerror(errno));
    return 1;
  }

  if(mkfifo(req_pipe_path, 0666) ==-1){
    fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
    return 1;
  }
  
  if (unlink(resp_pipe_path) == -1 && errno != ENOENT) {
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", resp_pipe_path,
            strerror(errno));
    return 1;
  }

  if(mkfifo(resp_pipe_path, 0666) ==-1){
    fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
    return 1;
  }

  server_fd = open(server_pipe_path, O_WRONLY);
  if(server_fd ==-1){
    fprintf(stderr, "[ERR]: open server pipe failed: %s\n", strerror(errno));
    return 1;
  }

  store_data(buf, &OP_CODE, sizeof(char));
  store_data(buf + sizeof(char), req_pipe, MAX_PIPE_NAME_SIZE*sizeof(char));
  store_data(buf + sizeof(char) + MAX_PIPE_NAME_SIZE, resp_pipe, MAX_PIPE_NAME_SIZE*sizeof(char));
  if(safe_write(server_fd, buf, buf_size) == -1){
    fprintf(stderr, "[ERR]: write to server pipe failed: %s\n", strerror(errno));
    return 1;
  }
  strcpy(buf, "");


  req_fd = open(req_pipe_path, O_WRONLY);
  if(req_fd ==-1){
    fprintf(stderr, "[ERR]: open request pipe failed: %s\n", strerror(errno));
    return 1;
  }

  resp_fd = open(resp_pipe_path, O_RDONLY);
  if(resp_fd ==-1){
    fprintf(stderr, "[ERR]: open response pipe failed: %s\n", strerror(errno));
    return 1;
  }
  
  if(safe_read(resp_fd, &session_id, sizeof(int)) == -1){
    fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
    return 1;
  }

  close(server_fd);

  return 0;
}

int ems_quit(void) { 
  char OP_CODE ='2';
  size_t buf_size = sizeof(char) + sizeof(int);
  char buf[buf_size];


  store_data(buf, &OP_CODE, sizeof(char));
  store_data(buf + sizeof(char), &session_id, sizeof(int));

  if(safe_write(req_fd, buf, buf_size) == -1){
    fprintf(stderr, "[ERR]: write to server pipe failed: %s\n", strerror(errno));
    return 1;
  }
  if(close(req_fd) == -1){
    fprintf(stderr, "[ERR]: close request pipe failed: %s\n", strerror(errno));
    return 1;
  }
  if(close(resp_fd) == -1){
    fprintf(stderr, "[ERR]: close response pipe failed: %s\n", strerror(errno));
    return 1;
  }

  if(unlink(req_pipe) == -1){
    fprintf(stderr, "[ERR]: unlink request pipe failed: %s\n", strerror(errno));
    return 1;
  }
  if(unlink(resp_pipe) == -1){
    fprintf(stderr, "[ERR]: unlink response pipe failed: %s\n", strerror(errno));
    return 1;
  }

  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  char OP_CODE = '3';
  size_t buf_size = sizeof(char) + sizeof(int) + sizeof(unsigned int) + 2*sizeof(size_t);
  char buf[buf_size];


  store_data(buf, &OP_CODE, sizeof(char));
  store_data(buf + sizeof(OP_CODE), &session_id, sizeof(int));

  store_data(buf + sizeof(OP_CODE) + sizeof(session_id), &event_id, sizeof(unsigned int));
  
  store_data(buf + sizeof(OP_CODE) + sizeof(session_id) + sizeof(event_id), &num_rows, sizeof(size_t));

  store_data(buf + sizeof(OP_CODE) + sizeof(session_id) + sizeof(event_id) + sizeof(num_rows), &num_cols, sizeof(size_t));
 

  if(safe_write(req_fd, buf, buf_size) == -1){
    fprintf(stderr, "[ERR]: write to server pipe failed: %s\n", strerror(errno));
    return 1;
  }

  int ret;
  if(safe_read(resp_fd, &ret, sizeof(int)) == -1){
    fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
    return 1;
  }


  return ret;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  char OP_CODE = '4';
  size_t buf_size = sizeof(char) + sizeof(int) + sizeof(unsigned int) + sizeof(size_t) + 2*sizeof(size_t)*MAX_RESERVATION_SIZE;
  char buf[buf_size];
  memset(buf, 0, buf_size);
  

  store_data(buf, &OP_CODE, sizeof(char));
  store_data(buf + sizeof(char), &session_id, sizeof(int));
  store_data(buf + sizeof(char) + sizeof(int), &event_id, sizeof(unsigned int));
  store_data(buf + sizeof(char) + sizeof(int) + sizeof(unsigned int), &num_seats, sizeof(size_t));
  store_data(buf + sizeof(char) + sizeof(int) + sizeof(unsigned int) + sizeof(size_t), xs, sizeof(size_t)*num_seats);
  store_data(buf + sizeof(char) + sizeof(int) + sizeof(unsigned int) + sizeof(size_t) + sizeof(size_t)*num_seats, ys, sizeof(size_t)*num_seats);
  

  if(safe_write(req_fd, buf, buf_size) == -1){
    fprintf(stderr, "[ERR]: write to request pipe failed: %s\n", strerror(errno));
    return 1;
  }


  int ret;
  if(safe_read(resp_fd, &ret, sizeof(int)) == -1){
    fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
    return 1;
  }

  return ret;
}

int ems_show(int out_fd, unsigned int event_id) {
  char OP_CODE = '5';
  size_t buf_size = sizeof(char) + sizeof(int) + sizeof(unsigned int);
  char buf[buf_size];


  store_data(buf, &OP_CODE, sizeof(char));
  store_data(buf + sizeof(char), &session_id, sizeof(int));
  store_data(buf + sizeof(char) + sizeof(int), &event_id, sizeof(unsigned int));
    
 
  if(safe_write(req_fd, buf, buf_size) == -1){
    fprintf(stderr, "[ERR]: write to request pipe failed: %s\n", strerror(errno));
    return 1;
  }

  int retptr;

  if(safe_read(resp_fd, &retptr, sizeof(int)) == -1){
    fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
    return 1;
  }

  if(retptr){
    return 1;
  }

  size_t event_rows;

  if(safe_read(resp_fd, &event_rows, sizeof(size_t)) == -1){
    fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
    return 1;
  }

  size_t event_cols;

  if(safe_read(resp_fd, &event_cols, sizeof(size_t)) == -1){
    fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
    return 1;
  }

  unsigned int* event_data = (unsigned int*) malloc(sizeof(unsigned int) * (event_rows)*(event_cols));
  if (event_data == NULL) {
    fprintf(stderr, "Error allocating memory for buffer\n");
    return 1;
  }
  
  if(safe_read(resp_fd, event_data, sizeof(unsigned int) *(event_rows)*(event_cols)) == -1){
    fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
    free(event_data);
    return 1;
  }



  for (size_t i = 1; i <= event_rows; i++) {
    for (size_t j = 1; j <= event_cols; j++) {
      char buffer[16];
      sprintf(buffer, "%u", event_data[(i - 1) * event_cols + j - 1]);
      if (print_str(out_fd, buffer)) {
        perror("Error writing to file descriptor");
        free(event_data); 
           
        return 1;
      }

      if (j < event_cols) {
        if (print_str(out_fd, " ")) {
          perror("Error writing to file descriptor");
          free(event_data);    
          
          return 1;
        }
      }
    }

    if (print_str(out_fd, "\n")) {
      perror("Error writing to file descriptor");
      free(event_data);  
     
      return 1;
    }
  }
  free(event_data);  

  return 0;

}

int ems_list_events(int out_fd) {
  int OP_CODE = '6';
  size_t buf_size = sizeof(char) + sizeof(int);
  char buf[buf_size];

  store_data(buf, &OP_CODE, sizeof(char));
  store_data(buf + sizeof(char), &session_id, sizeof(int));

  if(safe_write(req_fd, buf, buf_size) == -1){
    fprintf(stderr, "[ERR]: write to request pipe failed: %s\n", strerror(errno));
    return 1;
  }

  int ret;
  if(safe_read(resp_fd, &ret, sizeof(int)) == -1){
    fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno)); 
    return 1;
  }


  if(ret){
    return 1;
  }

  size_t num_events;

  if(safe_read(resp_fd, &num_events, sizeof(size_t)) == -1){
    fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
    return 1;
  }
  if(num_events == 0){
    char buff[] = "No events\n";
    if (print_str(out_fd, buff)) {
      perror("Error writing to file descriptor");
      return 1;
    }
    return 0;
  }
  unsigned int* ids = (unsigned int*) malloc(sizeof(unsigned int)*num_events);
  if (ids == NULL) {
    fprintf(stderr, "Error allocating memory for buffer\n");
    return 1;
  }

  if(safe_read(resp_fd, ids, sizeof(unsigned int)*num_events) == -1){
    fprintf(stderr, "[ERR]: read from request pipe failed: %s\n", strerror(errno));
    free(ids);
    return 1;
  }

  for(size_t i = 0; i < num_events; i++){
    char buff[] = "Event: ";
    if (print_str(out_fd, buff)) {
      perror("Error writing to file descriptor");
      free(ids);
      return 1;
    }
    char id[16];
    sprintf(id, "%u\n", ids[i]);
    if (print_str(out_fd, id)) {
      perror("Error writing to file descriptor");
      free(ids);
      return 1;
    }
  }
  free(ids);
  return 0;
}
